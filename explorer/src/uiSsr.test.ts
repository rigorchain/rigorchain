import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { injectSsrResPage, injectSsrRoot, renderBlockRoot, renderBlocksRoot, renderResPageSsr, renderStatusRoot, renderTxRoot } from "./uiSsr";
import { formatBlockHeaderTimeUtc } from "./relativeTime";
import { CONVERGENCE_LOCK_DESTINATION_LABEL } from "./txSummary";
import { OP_CONVERGE_SCRIPT_NAME } from "./script";

describe("uiSsr", () => {
  it("replaces the Loading placeholder with rendered block amounts", () => {
    const template = `<div id="root">Loading...</div>`;
    const html = injectSsrRoot(
      template,
      renderBlockRoot(
        {
          block: { height: "168", block_hash: "aa" },
          summary: { height: "168", hash: "aa", oru_subsidy_display: "1000.00010391" },
          convergence_panel: { status: "No Convergence Activity", candidate_set_size: 0, candidate_txids: [] },
          txs: [
            {
              txid: "bcd9ed2ed8dadd6ef93f631fb8cc8d746e4c5df910799d5e7feac645d91c5536",
              tx_kind_label: "ORU transfer",
              amount_display: "65536.00000000",
              asset_kind: "ORU",
            },
          ],
        },
        "aa",
      ),
    );
    assert.match(html, /data-ssr="1"/);
    assert.match(html, /65536\.00000000/);
    assert.doesNotMatch(html, />Loading\.\.\.</);
  });

  it("renders convergence burns without lottery draw-root labels or legacy C", () => {
    const html = renderBlockRoot(
      {
        block: {
          height: "305",
          block_hash: "d8",
          c_root: "04644ee72744a48d226f795d7cc4d692cbf763975275b7054dec59baedfaa7ba",
        },
        summary: { height: "305", hash: "d8" },
        convergence_panel: {
          status: "Convergence Candidate Block",
          candidate_set_size: 2,
          eligible_count: 1,
          c_root: "04644ee72744a48d226f795d7cc4d692cbf763975275b7054dec59baedfaa7ba",
          draw_candidate_root_committed: "56944c5d3f98413ef45cf54545538103cc9f298e0575820ad3591376e2e0f65d",
          draw_candidate_root_computed: "5371455808011a2a80f870425e923d25eded85fb65246af20b06b8ebd2634a39",
          winner_txid: "a80d8c17",
          candidate_txids: ["aa", "bb"],
        },
        txs: [],
      },
      "d8",
    );
    assert.doesNotMatch(html, /C \(convergence_root\)/);
    assert.doesNotMatch(html, /04644ee72744a48d/);
    assert.match(html, /Convergence destroys/);
    assert.match(html, /Convergence Destroy Block/);
    assert.doesNotMatch(html, /Convergence Candidate Block/);
    assert.doesNotMatch(html, /draw_candidate_root_committed/);
    assert.doesNotMatch(html, /draw_candidate_root_computed/);
    assert.doesNotMatch(html, /pays parent draw/);
    assert.doesNotMatch(html, /winner_txid/);
  });

  it("renders Block identity with Not available when fingerprint is missing", () => {
    const html = renderBlockRoot(
      {
        block: { height: "0", block_hash: "e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468" },
        summary: {
          height: "0",
          hash: "e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468",
          consensus_fingerprint_display: "Not available",
          consensus_fingerprint_status: "Not available",
        },
        txs: [],
      },
      "e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468",
    );
    assert.match(html, /Block identity/);
    assert.match(html, /Observational fingerprint:/);
    assert.match(html, /Not available/);
    assert.match(html, /e2ce92cfef740f92e8cc84a8541d8e760c776c668afd9b61740b3a4315241468/);
  });

  it("does not invent a next-block RES award note", () => {
    const html = renderBlockRoot(
      {
        block: { height: "305", block_hash: "award" },
        summary: {
          height: "305",
          hash: "award",
          res_minted_display: "0",
          res_destroy_cost_display: "1024.00000000 ORU per RES",
        },
        txs: [],
      },
      "award",
    );
    assert.match(html, /ORU destroyed to mint 1 RES/);
    assert.match(html, /1024\.00000000 ORU per RES/);
    assert.doesNotMatch(html, /RES award/);
    assert.doesNotMatch(html, /next block/);
  });

  it("labels a plain destroy as ORU destroyed with no RES minted", () => {
    const html = renderTxRoot(
      {
        summary: {
          sender: "rg1qk4v6avkzfu3rmzaph2jrhep8w4vvwj9xjrlpn9",
          receiver: null,
          amount_role: "convergence_lock",
          amount_display: "0.00001000",
          tx_kind: "oru_destroy",
          tx_kind_label: "ORU destroyed",
          kind_notice: "ORU destroyed. No RES minted.",
          converge_output_note: "OP_CONVERGE · ORU destroyed · no RES minted",
        },
        vout: [
          {
            vout_index: 0,
            asset_kind: "ORU",
            value_atoms: "100000000000",
            witness_version: null,
            address: null,
            script_pubkey: "14" + "00".repeat(20) + "bd",
            is_convergence: true,
          },
        ],
      },
      "6383575272fbc1a5d78ae2ea8ece4ce714e1aa3ee692f441b2a53d83e9140f2d",
    );
    assert.match(html, new RegExp(CONVERGENCE_LOCK_DESTINATION_LABEL.replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
    assert.match(html, new RegExp(OP_CONVERGE_SCRIPT_NAME));
    assert.match(html, /ORU destroyed\. No RES minted\./);
    assert.match(html, /ORU destroyed · no RES minted/);
    assert.match(html, /<th>Type<\/th>/);
    assert.doesNotMatch(html, /<th>Receiver<\/th>/);
    assert.match(html, /<th>Amount<\/th>/);
    assert.match(html, /Observational fingerprint/);
    assert.doesNotMatch(html, /Chain Fingerprint/);
  });

  it("labels a Convergence mint as ORU destroyed with RES minted", () => {
    const html = renderTxRoot(
      {
        summary: {
          sender: "rg1qk4v6avkzfu3rmzaph2jrhep8w4vvwj9xjrlpn9",
          receiver: null,
          amount_role: "convergence_lock",
          amount_display: "1024.00000000",
          tx_kind: "res_mint",
          tx_kind_label: "RES mint",
          kind_notice: "ORU destroyed. RES minted.",
          converge_output_note: "OP_CONVERGE / Unspendable destroy output",
        },
        vout: [
          {
            vout_index: 0,
            asset_kind: "ORU",
            value_atoms: "102400000000",
            script_pubkey: "14" + "00".repeat(20) + "bd",
            is_convergence: true,
          },
          {
            vout_index: 1,
            asset_kind: "RES",
            value_atoms: "100000000",
            address: "rgrt1pxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
          },
        ],
      },
      "aaa",
    );
    assert.match(html, /ORU destroyed\. RES minted\./);
    assert.match(html, /<th>Type<\/th>/);
    assert.match(html, />RES mint</);
    assert.doesNotMatch(html, /No RES minted/);
  });

  it("keeps homepage modules plus RFC-020 issuance and hides draw leftovers", () => {
    const html = renderStatusRoot({
      generated_at: "2026-09-10T00:00:00.000Z",
      chain_identity: {
        product: "RIGOR v0.2",
        network: "regtest",
        genesis_hash: "3070f47e2c1b15a3288382f3258396bb48b016eab76eae9f235d006dc2ef63",
        node_subversion: "/RigorCore:25.0.0/",
        explorer_version: "0.2.0",
        equivalence_claimed: false,
      },
      block_health: {
        current_height_node: 704,
        current_height_indexer: 461,
        verificationprogress: 1,
      },
      convergence_health: {
        winner_blocks_last_100: 2,
        winner_frequency_last_100: 0.02,
        active_convergences: 1,
        pending_windows: 3,
        c_root: "04644ee72744a48d226f795d7cc4d692cbf763975275b7054dec59baedfaa7ba",
        draw_candidate_root_committed: "56944c5d3f98413ef45cf54545538103cc9f298e0575820ad3591376e2e0f65d",
        draw_candidate_root_computed: "5371455808011a2a80f870425e923d25eded85fb65246af20b06b8ebd2634a39",
        f_set_distribution_last_100_blocks: { "1": 12, "2": 3 },
      },
      economic_snapshot: {
        network_res_minted_display: "200.00 RES",
        res_destroy_cost_display: "1024.00000000 ORU per RES",
        confirm_height: 705,
        last_res_height: 700,
        oru_subsidy_tip_display: "128.00000000 ORU",
        cumulative_convergence_display: "12.00000000 ORU",
        supply_chart: { total_issued_oru: { display: "90112.00000000 ORU" } },
        res_mint_count_last_100: 2,
        convergence_burns_last_100: 4,
      },
      mining_activity: { mempool_tx_count: 0, connections: 0 },
      system_integrity: {
        indexer_sync_lag_blocks: 243,
        indexer_last_updated_at: "2026-09-10T00:00:00.000Z",
        rpc_authority: "rigord: authoritative",
        explorer_compliance: { mismatch_active: false },
      },
    });
    assert.match(html, /Chain identity/);
    assert.match(html, /RIGOR v0\.2/);
    assert.match(html, /Block production/);
    assert.match(html, /Indexer integrity/);
    assert.doesNotMatch(html, /Node snapshot/);
    assert.doesNotMatch(html, /S1 —/);
    assert.doesNotMatch(html, /S2 —/);
    assert.doesNotMatch(html, /S3 —/);
    assert.doesNotMatch(html, /S4 —/);
    assert.match(html, /status-grid--pair/);
    assert.match(html, /Height \(node\)/);
    assert.match(html, /Height \(indexer\)/);
    assert.doesNotMatch(html, /Connections/);
    assert.doesNotMatch(html, /href="\/res\.html"/);
    assert.doesNotMatch(html, /href="\/oru\.html"/);
    assert.doesNotMatch(html, /Codename/);
    assert.doesNotMatch(html, /Network type/);
    assert.doesNotMatch(html, /RFC-019 body-tx RATE\(k\)/);
    assert.doesNotMatch(html, /Current RATE\(k\)/);
    assert.match(html, /Issuance/);
    assert.match(html, /RES minted/);
    assert.match(html, /200\.00 RES/);
    assert.match(html, /Issued ORU/);
    assert.match(html, /90112\.00000000 ORU/);
    assert.match(html, /ORU destroyed to mint 1 RES/);
    assert.match(html, /1024\.00000000 ORU per RES/);
    assert.match(html, /Confirming height/);
    assert.doesNotMatch(html, /Of which plain destroy/);
    assert.doesNotMatch(html, /Circulating spendable ORU/);
    assert.doesNotMatch(html, /Winner frequency/);
    assert.doesNotMatch(html, /draw_candidate_root_committed/);
    assert.doesNotMatch(html, /\|F\| distribution/);
    assert.doesNotMatch(html, /04644ee72744a48d/);
  });

  it("does not dump circulating or fee-burn series on the homepage", () => {
    const html = renderStatusRoot({
      chain_identity: {},
      block_health: {},
      convergence_health: {},
      economic_snapshot: {
        network_res_minted_display: "40.00000000 RES",
        oru_supply_cumulative_display: "1000.00000000 ORU",
        cumulative_convergence_display: "12.00000000 ORU",
        oru_circulating_spendable_display: "980.00000000 ORU",
        oru_plain_destroy_display: "1.00001000 ORU",
        rfc018_fee_burn: {
          block_burned_oru: "500",
          cumulative_burned_oru: "1500",
          cumulative_burned_oru_display: "0.00001500 ORU",
        },
        supply_chart: {
          total_issued_oru: { display: "1250.00000000 ORU" },
        },
      },
      mining_activity: {},
      system_integrity: { explorer_compliance: {} },
    });
    assert.doesNotMatch(html, /Node snapshot/);
    assert.match(html, /Issuance/);
    assert.match(html, /Issued ORU/);
    assert.match(html, /1250\.00000000 ORU/);
    assert.doesNotMatch(html, /Deflationary Fee Burn ORU/);
    assert.doesNotMatch(html, /0\.00001500 ORU/);
    assert.doesNotMatch(html, /Circulating spendable ORU/);
    assert.doesNotMatch(html, /980\.00000000 ORU/);
    assert.doesNotMatch(html, /Of which plain destroy/);
    assert.doesNotMatch(html, /1\.00001000 ORU/);
    assert.doesNotMatch(html, /ORU burned \(lifetime\)/);
  });

  it("omits draw and winner fields from the block header dump", () => {
    const html = renderBlockRoot(
      {
        block: {
          height: "527",
          block_hash: "aa",
          bits: "207fffff",
          draw_candidate_root: "dd".repeat(32),
          rigor_winner: "winner-txid",
          winner_txid: "winner-txid",
          opus_hash: "ee".repeat(32),
          c_root: "04644ee72744a48d226f795d7cc4d692cbf763975275b7054dec59baedfaa7ba",
        },
        summary: { height: "527", hash: "aa" },
        txs: [],
      },
      "aa",
    );
    assert.doesNotMatch(html, /draw_candidate_root/);
    assert.doesNotMatch(html, /winner-txid/);
    assert.doesNotMatch(html, /rigor_winner/);
    assert.doesNotMatch(html, /c_root/);
    assert.doesNotMatch(html, /C \(convergence_root\)/);
    assert.match(html, /207fffff/);
  });

  it("renders per-block RFC-018 fee burn on the block page without merging Convergence", () => {
    const html = renderBlockRoot(
      {
        block: { height: "10", block_hash: "bb" },
        summary: { height: "10", hash: "bb", oru_subsidy_display: "50.00000000" },
        rfc018_fee_burn: {
          block_burned_oru: "500",
          cumulative_burned_oru: "1500",
          block_burned_oru_display: "0.00000500 ORU",
          cumulative_burned_oru_display: "0.00001500 ORU",
        },
        txs: [],
      },
      "bb",
    );
    assert.match(html, /Fees destroyed \(this block\)/);
    assert.match(html, /0\.00000500 ORU/);
    assert.match(html, /Fees destroyed \(cumulative\)/);
    assert.match(html, /0\.00001500 ORU/);
  });

  it("server-renders the RES page instead of leaving Loading placeholders", () => {
    const { curvesHtml, rootHtml } = renderResPageSsr({
      confirmHeight: 529,
      eco: {
        res_destroy_cost_display: "1024.03228785 ORU destroyed to mint 1 RES",
        confirm_height: 529,
        res_derived_max_supply_display: "4194304.00000000 RES",
        network_res_minted_display: "40.30762289 RES",
        res_mint_count: 9,
        last_res_height: 527,
        last_res_mint_txid: "aa".repeat(32),
      },
      mints: [{ block_height: 527, mint_txid: "bb".repeat(32), mint_amount: "1" }],
    });
    const html = injectSsrResPage(
      '<div id="curves" class="status-dashboard">Loading schedule…</div><div id="root" class="status-dashboard">Loading…</div>',
      curvesHtml,
      rootHtml,
    );
    assert.match(html, /data-ssr="1"/);
    assert.match(html, /ORU destroyed to mint 1 RES/);
    assert.match(html, /status-grid--charts/);
    assert.match(html, /RES mint history/);
    assert.match(html, /40\.30762289 RES/);
    assert.doesNotMatch(html, /Loading schedule/);
    assert.doesNotMatch(html, />Loading…</);
  });

  it("injects both RFC-020 curves into the public RES template", () => {
    const template = readFileSync(join(__dirname, "..", "public", "res.html"), "utf8");
    assert.match(template, /id="curves"/);
    const { curvesHtml, rootHtml } = renderResPageSsr({
      confirmHeight: 0,
      eco: { network_res_minted_display: "0.00000000 RES", res_mint_count: 0 },
      mints: [],
    });
    const html = injectSsrResPage(template, curvesHtml, rootHtml);
    assert.match(html, /status-grid--charts/);
    assert.match(html, /ORU destroyed to mint 1 RES/);
    assert.match(html, /RES mint ceiling/);
    assert.equal((html.match(/<svg class="res-curve"/g) || []).length, 2);
    assert.doesNotMatch(html, /does not publish the issuance equation/);
  });

  it("shows the original UTC header time, with relative time only as a supplement", () => {
    const now = Math.floor(Date.now() / 1000);
    const pastTime = now - 7 * 60 - 30;
    const past = renderBlockRoot(
      { block: { height: 1, block_hash: "aa", time: pastTime }, summary: { height: 1, hash: "aa", time: pastTime }, txs: [] },
      "aa",
    );
    assert.match(past, /Block header time \(UTC\)/);
    assert.match(past, new RegExp(formatBlockHeaderTimeUtc(pastTime).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
    assert.match(past, /7 minutes ago/);
    assert.doesNotMatch(past, /Relative time is calculated from the explorer's current clock/);

    const future = now + 16 * 60 + 30;
    const ahead = renderBlockRoot(
      { block: { height: 1450, block_hash: "a8b82b03", time: future }, summary: { height: 1450, hash: "a8b82b03", time: future }, txs: [] },
      "a8b82b03",
    );
    assert.match(ahead, /Block header time \(UTC\)/);
    assert.match(ahead, new RegExp(formatBlockHeaderTimeUtc(future).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
    assert.match(ahead, /in 16 minutes/);
    assert.doesNotMatch(ahead, /just now/);
    assert.doesNotMatch(ahead, /INVALID BLOCK|BAD TIMESTAMP|MINER ERROR/);
  });

  it("keeps height order and each block's own header time when a later height is earlier", () => {
    const html = renderBlocksRoot([
      { height: 1452, block_hash: "cdaf09f0", time: 1793102162 },
      { height: 1451, block_hash: "48a50e33", time: 1793102138 },
      { height: 1450, block_hash: "a8b82b03", time: 1793104113 },
    ]);
    const i1452 = html.indexOf(">1452<");
    const i1451 = html.indexOf(">1451<");
    const i1450 = html.indexOf(">1450<");
    assert.ok(i1452 >= 0 && i1451 > i1452 && i1450 > i1451);
    const row1451 = html.slice(i1451, html.indexOf("</tr>", i1451));
    assert.match(row1451, /2026-10-27 11:55:38 UTC/);
    assert.doesNotMatch(row1451, /12:28:33/);
    const row1450 = html.slice(i1450, html.indexOf("</tr>", i1450));
    assert.match(row1450, /2026-10-27 12:28:33 UTC/);
    assert.match(html, /2026-10-27 11:56:02 UTC/);

    const detail1450 = renderBlockRoot(
      { block: { height: 1450, time: 1793104113 }, summary: { height: 1450, hash: "a8", time: 1793104113 }, txs: [] },
      "a8",
    );
    const detail1451 = renderBlockRoot(
      { block: { height: 1451, time: 1793102138 }, summary: { height: 1451, hash: "48", time: 1793102138 }, txs: [] },
      "48",
    );
    assert.match(detail1450, /2026-10-27 12:28:33 UTC/);
    assert.doesNotMatch(detail1450, /11:55:38/);
    assert.match(detail1451, /2026-10-27 11:55:38 UTC/);
    assert.doesNotMatch(detail1451, /12:28:33/);
  });
});
