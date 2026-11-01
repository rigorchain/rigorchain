import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { runInNewContext } from "node:vm";
import { buildOruEconomics, type OruEconomicsInput } from "./oruEconomics";

function readPublic(name: string): string {
  return readFileSync(join(__dirname, "../public", name), "utf8");
}

const HASH = "c0a2f4ef1de70c8d38c1e2b891d994d3a18877993b7022b4e9d8bc5b70b1c984";

function sampleView() {
  const input: OruEconomicsInput = {
    indexerHeight: 1956,
    indexerBlockHash: HASH,
    nodeHeight: 1956,
    nodeBestBlockHash: HASH,
    confirmHeight: 1957,
    nodeIssuedAtoms: 25036800000000n,
    nodeConvergenceAtoms: 1713252258735n,
    headerConvergenceAtoms: 1713252258735n,
    indexedConvergenceAtoms: 1713252258735n,
    plainAtoms: 0n,
    attributableOruAtoms: 1713252258735n,
    attributableResAtoms: 1672968358n,
    totalResAtoms: 1672968358n,
    feeAtoms: 0n,
    eventCount: 79,
    lastMintHeight: 1947,
    recent: [
      {
        height: 1947,
        txid: "ab".repeat(32),
        blockHash: HASH,
        burnAtoms: 102411883889n,
        resAtoms: 100000000n,
      },
    ],
  };
  return buildOruEconomics(input);
}

function loadPage() {
  const sandbox: Record<string, unknown> = {
    window: {},
    ExplorerRoutes: {
      txPath: (txid: string) => "/tx/" + txid,
      blockPath: (hash: string) => "/block/" + hash,
    },
  };
  sandbox.window = sandbox;
  runInNewContext(readPublic("oru.js"), sandbox, { filename: "oru.js" });
  return sandbox.OruPage as {
    REFRESH_MS: number;
    renderOruPage: (data: unknown) => string;
  };
}

describe("ORU page presentation", () => {
  const html = readPublic("oru.html");
  const page = loadPage();
  const rendered = page.renderOruPage(sampleView());

  it("uses the cumulative page title and the three primary sections", () => {
    assert.match(html, /<title>ORU Convergence<\/title>/);
    assert.match(html, /Cumulative ORU Destruction and RES Generation — RIGOR Mainnet/);
    assert.match(rendered, /id="oru-destruction"/);
    assert.match(rendered, /id="res-generation"/);
    assert.match(rendered, /id="oru-res-relationship"/);
    assert.match(rendered, /Total ORU Issued/);
    assert.match(rendered, /Total ORU Destroyed/);
    assert.match(rendered, /Convergence ORU Destroyed/);
    assert.match(rendered, /Plain ORU Destroyed/);
    assert.match(rendered, /Transaction Fees Destroyed/);
    assert.match(rendered, /Convergence Destruction Ratio/);
    assert.match(rendered, /Total ORU Destruction Ratio/);
    assert.match(rendered, /Total RES Minted/);
    assert.match(rendered, /RES Maximum Supply/);
    assert.match(rendered, /Remaining RES Issuance Capacity/);
    assert.match(rendered, /RES Issuance Progress/);
    assert.match(rendered, /Total Convergence Events/);
    assert.match(rendered, /Historical Effective Conversion Ratio/);
    assert.match(rendered, /RES Generated per 1,000 ORU Destroyed/);
    assert.match(rendered, /Current \/ Latest Mint Conversion Information/);
    assert.match(rendered, /1024\.07929627 ORU per 1 RES/);
    assert.match(rendered, /1024\.11944928 ORU destroyed to mint 1 RES/);
    assert.match(rendered, /cumulative confirmed mainnet activity/);
  });

  it("keeps block-level counters out of the primary statistics", () => {
    assert.doesNotMatch(html, /Destroyed this block|Fees destroyed this block|Since last mint/);
    assert.doesNotMatch(readPublic("oru.js"), /Destroyed this block|Fees destroyed this block|Since last mint/);
    assert.doesNotMatch(rendered, /Destroyed this block|Fees destroyed this block|Since last mint/);
  });

  it("links recent confirmed events and stays readable on a narrow viewport", () => {
    assert.match(rendered, new RegExp(`/tx/${"ab".repeat(32)}`));
    assert.match(rendered, new RegExp(`/block/${HASH}`));
    assert.match(rendered, /Recent confirmed Convergence transactions/);
    assert.match(html, /name="viewport"/);
    assert.match(html, /@media \(max-width: 800px\)/);
    assert.match(html, /href="\/oru\.html"/);
    assert.match(html, /href="\/"/);
    assert.equal(page.REFRESH_MS, 60000);
    assert.match(html, /\/api\/oru\/economics|OruPage\.start/);
    assert.doesNotMatch(readPublic("oru.js"), /\/api\/status/);
  });
});

describe("homepage regression", () => {
  const status = readPublic("status.js");
  const home = readPublic("index.html");
  const ssr = readFileSync(join(__dirname, "../src/uiSsr.ts"), "utf8");
  const hashrate = readFileSync(join(__dirname, "../src/networkHashrate.ts"), "utf8");

  it("keeps chain identity, block production, indexer integrity, and issuance", () => {
    const labels = [
      "Chain identity",
      "Chain",
      "Network",
      "Genesis",
      "Node",
      "Subversion",
      "Best block",
      "Block production",
      "Height (node)",
      "Avg block time",
      "Difficulty",
      "nBits",
      "OpuHash",
      "Peers",
      "Mempool tx count",
      "Indexer integrity",
      "Issuance",
      "Issued ORU",
      "RES minted",
      "Convergence destroyed ORU",
      "ORU destroyed to mint 1 RES",
      "Confirming height",
      "ORU subsidy",
      "Last mint height",
    ];
    for (const label of labels) {
      const pattern = new RegExp(label.replace(/[()]/g, "\\$&"));
      assert.match(status, pattern);
      assert.match(ssr, pattern);
    }
    assert.match(status, /Network Hashrate \(50 blocks\)/);
    assert.match(hashrate, /Network Hashrate \(50 blocks\)/);
    assert.match(ssr, /HASHRATE_LABEL/);
    assert.match(home, /href="\/oru\.html"/);
    assert.match(home, /StatusDashboard\.startAutoRefresh/);
    assert.match(status, /HOME_REFRESH_MS = 60000/);
    assert.match(status, /\/api\/status/);
  });
});
