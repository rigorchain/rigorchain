import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { renderTxRoot } from "./uiSsr";
import { resTransferListSql } from "./resTransferQuery";
import {
  addressResCounterparty,
  canonicalTransferRows,
  isOrdinaryResTransfer,
  pageTransferRows,
  senderAttribution,
  type ResTransferTx,
} from "./resTransferHistory";

const HASH = "aa".repeat(32);

function tx(partial: Partial<ResTransferTx> & Pick<ResTransferTx, "txid">): ResTransferTx {
  return {
    height: 1,
    blockHash: HASH,
    txIndex: 0,
    time: 1_700_000_000,
    canonical: true,
    isCoinbase: false,
    isResMint: false,
    inputs: [],
    outputs: [],
    ...partial,
  };
}

describe("RES transfer classification", () => {
  it("lists one row for a single-recipient transfer", () => {
    const rows = canonicalTransferRows([
      tx({
        txid: "one",
        height: 1185,
        inputs: [{ address: "rg1from", atoms: 49803372n }],
        outputs: [{ vout: 0, address: "rg1to", atoms: 49803372n }],
      }),
    ]);
    assert.equal(rows.length, 1);
    assert.equal(rows[0].amountAtoms, 49803372n);
    assert.deepEqual(rows[0].sender, { kind: "address", address: "rg1from" });
    assert.equal(rows[0].recipient, "rg1to");
  });

  it("keeps every recipient output and does not double-count the inputs", () => {
    const rows = canonicalTransferRows([
      tx({
        txid: "many-out",
        inputs: [{ address: "rg1from", atoms: 97653880n }],
        outputs: [
          { vout: 1, address: "rg1b", atoms: 87653880n },
          { vout: 0, address: "rg1a", atoms: 10000000n },
        ],
      }),
    ]);
    assert.deepEqual(rows.map((row) => row.vout), [0, 1]);
    assert.equal(rows.reduce((sum, row) => sum + row.amountAtoms, 0n), 97653880n);
  });

  it("does not invent one sender when several RES inputs pay the transfer", () => {
    const sender = senderAttribution([
      { address: "rg1a", atoms: 37108631n },
      { address: "rg1b", atoms: 12694741n },
    ]);
    assert.deepEqual(sender, { kind: "multiple" });
  });

  it("excludes a RES mint and an ORU-only destruction", () => {
    const mint = tx({
      txid: "mint",
      isResMint: true,
      inputs: [{ address: "rg1oru", atoms: 0n }],
      outputs: [{ vout: 2, address: "rg1res", atoms: 37108631n }],
    });
    const destroy = tx({
      txid: "destroy",
      inputs: [{ address: "rg1oru", atoms: 0n }],
      outputs: [],
    });
    assert.equal(isOrdinaryResTransfer(mint), false);
    assert.equal(isOrdinaryResTransfer({ ...destroy, inputs: [{ address: "rg1res", atoms: 10n }], outputs: [] }), false);
    assert.equal(canonicalTransferRows([mint, destroy]).length, 0);
  });

  it("keeps a RES transfer that also pays an ORU fee output", () => {
    const rows = canonicalTransferRows([
      tx({
        txid: "fee-leg",
        inputs: [
          { address: "rg1res", atoms: 49803372n },
          { address: "rg1fee", atoms: 0n },
        ],
        outputs: [
          { vout: 0, address: "rg1to", atoms: 49803372n },
          { vout: 1, address: "rg1change", atoms: 0n },
        ],
      }),
    ]);
    assert.equal(rows.length, 1);
    assert.equal(rows[0].amountAtoms, 49803372n);
    assert.equal(rows[0].recipient, "rg1to");
  });

  it("marks a missing sender address unavailable and a mixed set ambiguous", () => {
    assert.deepEqual(senderAttribution([{ address: null, atoms: 10n }]), { kind: "unavailable" });
    assert.deepEqual(senderAttribution([
      { address: "rg1a", atoms: 10n },
      { address: null, atoms: 5n },
    ]), { kind: "multiple" });
  });

  it("ignores a transaction with no RES movement", () => {
    assert.equal(canonicalTransferRows([
      tx({ txid: "oru", inputs: [{ address: "rg1", atoms: 0n }], outputs: [{ vout: 0, address: "rg1b", atoms: 0n }] }),
    ]).length, 0);
  });
});

describe("RES transfer history ordering and canonical filtering", () => {
  const rows = canonicalTransferRows([
    tx({ txid: "old", height: 10, txIndex: 1, canonical: true, inputs: [{ address: "a", atoms: 1n }], outputs: [{ vout: 0, address: "b", atoms: 1n }] }),
    tx({ txid: "new-late", height: 20, txIndex: 2, inputs: [{ address: "a", atoms: 2n }], outputs: [{ vout: 0, address: "b", atoms: 2n }] }),
    tx({ txid: "new-early", height: 20, txIndex: 1, inputs: [{ address: "a", atoms: 3n }], outputs: [{ vout: 1, address: "c", atoms: 1n }, { vout: 0, address: "b", atoms: 2n }] }),
    tx({ txid: "orphan", height: 30, canonical: false, inputs: [{ address: "a", atoms: 9n }], outputs: [{ vout: 0, address: "b", atoms: 9n }] }),
  ]);

  it("orders newest height first and keeps output order inside a transaction", () => {
    assert.deepEqual(rows.map((row) => [row.txid, row.vout]), [
      ["new-late", 0],
      ["new-early", 0],
      ["new-early", 1],
      ["old", 0],
    ]);
  });

  it("does not duplicate or skip rows across pages", () => {
    const first = pageTransferRows(rows, 1, 2);
    const second = pageTransferRows(rows, 2, 2);
    const keys = [...first, ...second].map((row) => `${row.txid}:${row.vout}`);
    assert.equal(new Set(keys).size, keys.length);
    assert.deepEqual(keys, rows.slice(0, 4).map((row) => `${row.txid}:${row.vout}`));
  });

  it("returns an empty page when nothing qualifies", () => {
    assert.deepEqual(pageTransferRows([], 1, 50), []);
  });
});

describe("address counterparty and transaction detail", () => {
  it("links a single counterparty and refuses to collapse several", () => {
    assert.deepEqual(addressResCounterparty({
      sentAtoms: 10n,
      receivedAtoms: 0n,
      senderAddressCount: 1,
      senderAddress: "rg1from",
      senderMissing: false,
      otherRecipientCount: 1,
      otherRecipient: "rg1to",
    }), { kind: "address", address: "rg1to" });
    assert.equal(addressResCounterparty({
      sentAtoms: 10n,
      receivedAtoms: 0n,
      senderAddressCount: 2,
      senderAddress: "rg1from",
      senderMissing: false,
      otherRecipientCount: 2,
      otherRecipient: "rg1to",
    }).kind, "multiple");
  });

  it("shows RES inputs separately from the ORU fee line", () => {
    const html = renderTxRoot(
      {
        summary: {
          tx_kind: "res_transfer",
          tx_kind_label: "RES transfer",
          sender: "rg1from",
          receiver: "rg1to",
          amount_display: "0.49803372 RES",
          amount_total_display: "0.49803372 RES",
          fee: { fee_display: "N/A", fee_kind: "na" },
        },
        vin: [{ vin_index: 0, prev_asset_kind: "RES", prev_value_atoms: "49803372", prev_address: "rg1from" }],
        vout: [{ vout_index: 0, asset_kind: "RES", value_atoms: "49803372", address: "rg1to" }],
      },
      "cc".repeat(32),
    );
    assert.match(html, /RES transfer/);
    assert.match(html, /<h2>Inputs<\/h2>/);
    assert.match(html, /rg1from/);
    assert.match(html, /rg1to/);
    assert.match(html, /<th>Fee<\/th><td>N\/A <span class="muted">ORU fee unavailable<\/span><\/td>/);
    assert.match(html, /0\.49803372 RES/);
    assert.doesNotMatch(html, /<th>Fee<\/th><td>0/);
  });
});

describe("RES transfer query and page responsibilities", () => {
  it("reads canonical blocks and excludes mints", () => {
    const sql = resTransferListSql();
    assert.match(sql, /JOIN blocks b ON b.block_hash = t.block_hash/);
    assert.match(sql, /NOT EXISTS \(SELECT 1 FROM res_mints/);
    assert.match(sql, /ORDER BY b.height DESC, t.block_tx_index DESC, o.vout_index ASC/);
    assert.doesNotMatch(sql, /CREATE TABLE|INSERT INTO|UPDATE /);
  });

  it("removes the duplicated convergence list and keeps the RES event page", () => {
    const oru = readFileSync(join(__dirname, "../public/oru.js"), "utf8");
    const events = readFileSync(join(__dirname, "../public/res-events.html"), "utf8");
    const res = readFileSync(join(__dirname, "../public/res.html"), "utf8");
    assert.doesNotMatch(oru, /Recent confirmed Convergence transactions/);
    assert.match(events, /\/api\/res\/events/);
    assert.match(events, /href="\/res\.html"/);
    assert.match(res, /RES Transfer History/);
    assert.match(res, /\/api\/res\/transfers/);
  });

  it("does not let an active reader use first-input res_transfers attribution", () => {
    const roots = [join(__dirname, "..", "src"), join(__dirname, "..", "public")];
    const files: string[] = [];
    const walk = (dir: string) => {
      for (const entry of readdirSync(dir, { withFileTypes: true })) {
        const path = join(dir, entry.name);
        if (entry.isDirectory()) walk(path);
        else if (/\.(ts|js|html|sql)$/.test(entry.name)) files.push(path);
      }
    };
    roots.forEach(walk);
    const readers = files.filter((path) =>
      /\b(?:FROM|JOIN|UPDATE|DELETE\s+FROM)\s+res_transfers\b/.test(readFileSync(path, "utf8")),
    );
    assert.deepEqual(readers, []);
  });
});
