import { describe, it } from "node:test";
import assert from "node:assert/strict";
import {
  CONVERGENCE_LOCK_DESTINATION_LABEL,
  classifyTxKind,
  convergeOutputNote,
  destinationLabel,
  pickPrimaryOruOutput,
  presentBlockTx,
  txKindLabel,
  txKindNotice,
} from "./txSummary";

const OP_CONVERGE_HEX = "14" + "00".repeat(20) + "bd";

describe("txSummary", () => {
  it("selects the 65536 ORU payment, not the change remainder", () => {
    const pick = pickPrimaryOruOutput([
      {
        vout_index: 0,
        value_atoms: "6553600000000",
        asset_kind: "ORU",
        address: "rg1qy5cey7tehsdm54agj2ya7dw5rkfddx8fcrme4y",
        witness_version: 0,
      },
      {
        vout_index: 1,
        value_atoms: "45399989371",
        asset_kind: "ORU",
        address: "rg1qchange",
        witness_version: 0,
      },
    ]);
    assert.ok(pick);
    assert.equal(pick.role, "payment");
    assert.equal(String(pick.output.value_atoms), "6553600000000");
    assert.equal(pick.output.address, "rg1qy5cey7tehsdm54agj2ya7dw5rkfddx8fcrme4y");
  });

  it("prefers OP_CONVERGE burn over larger change", () => {
    const pick = pickPrimaryOruOutput([
      {
        vout_index: 0,
        value_atoms: "45299988660",
        asset_kind: "ORU",
        address: "rg1qchange",
        witness_version: 0,
      },
      {
        vout_index: 1,
        value_atoms: "6553600000000",
        asset_kind: "ORU",
        address: null,
        witness_version: null,
        script_pubkey: OP_CONVERGE_HEX,
      },
    ]);
    assert.ok(pick);
    assert.equal(pick.role, "convergence_lock");
    assert.equal(String(pick.output.value_atoms), "6553600000000");
    assert.equal(classifyTxKind({ isCoinbase: false, primaryRole: pick.role, hasOru: true }), "oru_destroy");
    assert.equal(txKindLabel("oru_destroy"), "ORU destroyed");
    assert.equal(txKindNotice("oru_destroy"), "ORU destroyed. No RES minted.");
    assert.equal(convergeOutputNote("oru_destroy"), "OP_CONVERGE · ORU destroyed · no RES minted");
    assert.equal(txKindLabel("convergence_lock"), CONVERGENCE_LOCK_DESTINATION_LABEL);
    assert.equal(
      classifyTxKind({
        isCoinbase: false,
        primaryRole: "payment",
        hasOru: true,
        hasRes: true,
        isResMint: true,
      }),
      "res_mint",
    );
    assert.equal(txKindLabel("res_mint"), "RES mint");
    assert.equal(txKindNotice("res_mint"), "ORU destroyed. RES minted.");
    assert.equal(
      classifyTxKind({
        isCoinbase: true,
        primaryRole: "output",
        hasOru: true,
        hasRes: true,
        isResMint: true,
      }),
      "res_mint",
    );
    assert.equal(
      classifyTxKind({
        isCoinbase: false,
        primaryRole: null,
        hasOru: false,
        hasRes: true,
        isResMint: false,
      }),
      "other",
    );
    assert.equal(
      classifyTxKind({
        isCoinbase: false,
        primaryRole: "output",
        hasOru: true,
        hasRes: true,
        hasResInput: true,
        isResMint: false,
      }),
      "res_transfer",
    );
    assert.equal(txKindLabel("res_transfer"), "RES transfer");
    assert.notEqual(
      classifyTxKind({
        isCoinbase: false,
        primaryRole: "payment",
        hasOru: true,
        hasRes: false,
        hasResInput: true,
        isResMint: false,
      }),
      "res_transfer",
    );
    assert.equal(destinationLabel("convergence_lock"), CONVERGENCE_LOCK_DESTINATION_LABEL);
    assert.equal(destinationLabel("payment"), "Receiver");
  });

  it("does not treat Witness v1 RES-shaped rows as Convergence", () => {
    const pick = pickPrimaryOruOutput([
      {
        vout_index: 0,
        value_atoms: "100000000",
        asset_kind: "ORU",
        address: "rg1qchange",
        witness_version: 0,
      },
      {
        vout_index: 1,
        value_atoms: "200000000",
        asset_kind: "ORU",
        address: "rg1pnotconverge",
        witness_version: 1,
      },
    ]);
    assert.ok(pick);
    assert.equal(pick.role, "payment");
  });
});

const oru = (atoms: string, address = "rg1oru"): Parameters<typeof presentBlockTx>[0]["outputs"][number] => ({
  vout_index: 0,
  value_atoms: atoms,
  asset_kind: "ORU",
  address,
});

const res = (atoms: string, vout: number, address: string | null): Parameters<typeof presentBlockTx>[0]["outputs"][number] => ({
  vout_index: vout,
  value_atoms: atoms,
  asset_kind: "RES",
  address,
});

describe("block transaction classification", () => {
  it("keeps an ORU-only payment labeled as ORU", () => {
    const row = presentBlockTx({
      isCoinbase: false,
      isResMint: false,
      hasPositiveResInput: false,
      outputs: [oru("6553600000000", "rg1pay"), oru("1000", "rg1change")],
    });
    assert.equal(row.asset_movement, "oru_only");
    assert.equal(row.tx_kind, "oru_transfer");
    assert.equal(row.tx_kind_label, "ORU transfer");
    assert.equal(row.asset_kind, "ORU");
    assert.equal(row.amount_atoms, "6553600000000");
  });

  it("labels a RES transfer from indexed RES inputs and the RES output sum", () => {
    const row = presentBlockTx({
      isCoinbase: false,
      isResMint: false,
      hasPositiveResInput: true,
      outputs: [res("49803372", 0, "rg1to")],
    });
    assert.equal(row.asset_movement, "res_transfer");
    assert.equal(row.tx_kind, "res_transfer");
    assert.equal(row.tx_kind_label, "RES transfer");
    assert.equal(row.amount_atoms, "49803372");
    assert.equal(row.asset_kind, "RES");
    assert.equal(row.receiver, "rg1to");
  });

  it("does not hide RES movement when the same transaction also pays ORU", () => {
    const row = presentBlockTx({
      isCoinbase: false,
      isResMint: false,
      hasPositiveResInput: true,
      outputs: [res("49803372", 0, "rg1to"), oru("89998346", "rg1change")],
    });
    assert.equal(row.asset_movement, "res_and_oru");
    assert.equal(row.tx_kind, "res_transfer");
    assert.equal(row.tx_kind_label, "RES transfer · ORU");
    assert.equal(row.asset_kind, "RES, ORU");
    assert.equal(row.amount_atoms, "49803372");
    assert.notEqual(row.tx_kind_label, "ORU transfer");
  });

  it("keeps a RES mint distinct from a transfer", () => {
    const row = presentBlockTx({
      isCoinbase: false,
      isResMint: true,
      hasPositiveResInput: false,
      outputs: [
        { vout_index: 0, value_atoms: "38000000000", asset_kind: "ORU", script_pubkey: OP_CONVERGE_HEX },
        res("37108631", 2, "rg1mint"),
      ],
    });
    assert.equal(row.asset_movement, "res_mint");
    assert.equal(row.tx_kind, "res_mint");
    assert.equal(row.tx_kind_label, "RES mint");
    assert.equal(row.amount_atoms, "37108631");
    assert.notEqual(row.tx_kind, "res_transfer");
  });

  it("does not label RES destruction without a RES output as a transfer", () => {
    const row = presentBlockTx({
      isCoinbase: false,
      isResMint: false,
      hasPositiveResInput: true,
      outputs: [oru("1000", "rg1change")],
    });
    assert.equal(row.asset_movement, "res_destroyed");
    assert.notEqual(row.tx_kind, "res_transfer");
    assert.match(row.tx_kind_label, /RES destroyed/);
    assert.doesNotMatch(row.tx_kind_label, /RES transfer/);
  });

  it("sums every positive RES output once when several inputs fund the transfer", () => {
    const row = presentBlockTx({
      isCoinbase: false,
      isResMint: false,
      hasPositiveResInput: true,
      outputs: [res("10000000", 1, "rg1a"), res("87653880", 0, "rg1b"), oru("1", "rg1fee")],
    });
    assert.equal(row.asset_movement, "res_and_oru");
    assert.equal(row.amount_atoms, "97653880");
    assert.equal(row.receiver, null);
    assert.notEqual(row.amount_atoms, "10000000");
    assert.notEqual(row.amount_atoms, String(97653880n + 37108631n + 12694741n));
  });

  it("does not infer RES movement from an address on an ORU output", () => {
    const row = presentBlockTx({
      isCoinbase: false,
      isResMint: false,
      hasPositiveResInput: false,
      outputs: [oru("100000000", "rg1pw0938smwssukxd3qxyjrrjzdthdu7u0dxcsh340edlg7pstl9d6spdh3us")],
    });
    assert.equal(row.asset_movement, "oru_only");
    assert.equal(row.tx_kind, "oru_transfer");
  });

  it("classifies a transaction with no asset movement as none", () => {
    const row = presentBlockTx({
      isCoinbase: false,
      isResMint: false,
      hasPositiveResInput: false,
      outputs: [],
    });
    assert.equal(row.asset_movement, "none");
    assert.equal(row.tx_kind, "other");
    assert.equal(row.amount_atoms, null);
  });
});
