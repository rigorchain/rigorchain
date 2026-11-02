import { describe, it, after } from "node:test";
import assert from "node:assert/strict";
import { once } from "node:events";
import { readFileSync } from "node:fs";
import { createServer } from "node:http";
import type { AddressInfo } from "node:net";
import { join } from "node:path";
import express from "express";
import type { Pool } from "pg";
import { atomsToResDisplay } from "./amount";
import { formatRateOruPerRes, resDestroyCostForHeight } from "./resIssuanceSchedule";
import {
  assembleStatusDashboard,
  loadingStatusCache,
  type EconomicAggregates,
  type LiveStatusParts,
} from "./api/status";
import type { Config } from "./config";
import { registerApiRoutes, registerUiRoutes } from "./registerRoutes";
import { SharedSnapshot } from "./sharedSnapshot";
import {
  installExplorerCachesForTests,
  readCachedOruEconomics,
  readCachedStatus,
  startExplorerCaches,
  stopExplorerCaches,
} from "./statusCache";

const MINT_ATOMS = "300000000";

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

function liveParts(): LiveStatusParts {
  return {
    chainState: {
      indexer_name: "test",
      network: "mainnet",
      last_height: 1900n,
      last_block_hash: "ab".repeat(32),
      critical_root_mismatch: false,
      issuance_generation: null,
      updated_at: new Date("2026-11-02T00:00:00.000Z"),
    },
    compliance: {
      chain_name: "RIGOR",
      network_type: "mainnet",
      product: "RIGOR",
      codename: "test",
      network: "mainnet",
      genesis_hash: "aa".repeat(32),
      genesis_hash_short: "aa",
      node_subversion: null,
      node_release_version: null,
      explorer_version: "0.2.0",
      build_timestamp: "test",
      equivalence_claimed: false,
      mismatches: {
        root_mismatch: false,
        chain_mismatch: false,
        genesis_mismatch: false,
        indexer_mismatch: false,
      },
      mismatch_active: false,
      mismatch_messages: [],
      catchup_active: false,
      catchup_messages: [],
      rpc_degraded: false,
      rpc_messages: [],
    },
    blockchainInfo: { blocks: 1900, cumulative_convergence: 1.5, bestblockhash: "cd".repeat(32) },
    networkInfo: { connections: 3, subversion: "/RigorCore:25.0.0/" },
    convStatus: { confirm_height: 1901 },
    runtimeSupply: { oru_subsidy_cumulative_derived: 12.5, oru_subsidy_block: 128 },
    mempoolTxids: ["tx"],
    rpcError: null,
    blocks: [],
    avgBlockTime: 60,
    networkHashrate: { available: false, display: "", note: "" },
    tipBlock: null,
    fSizes: [],
    mintBlocks: 0,
    rootChanges: 1,
    mismatchBlocks: 0,
    reorgMismatchBlocks: 0,
    walletObserver: { status: "not_configured" },
    indexerHeight: 1900,
    nodeHeight: 1900,
  } as unknown as LiveStatusParts;
}

function economicParts(): EconomicAggregates {
  return {
    indexerHeight: 1900,
    indexerBlockHash: "ab".repeat(32),
    eco: {
      res_mint_total_atoms: MINT_ATOMS,
      oru_utxo_atoms: "100",
      oru_plain_destroy_atoms: "7",
    },
    oruSupplyConsistent: true,
    oruSupplyErrors: [],
    rfc018FeeBurn: {
      block_burned_oru: "10",
      cumulative_burned_oru: "457",
      block_burned_oru_display: "0.00000010 ORU",
      cumulative_burned_oru_display: "0.00000457 ORU",
      equivalence_claimed: false,
      indexed: true,
      height: "1900",
    },
    plainDestroyAtoms: 7n,
    plainDestroyDisplay: "0.00000007 ORU",
  };
}

describe("status assembly", () => {
  it("does not turn a missing economic snapshot into zeros", () => {
    const dashboard = assembleStatusDashboard(liveParts(), null, loadingStatusCache());
    const eco = dashboard.economic_snapshot;
    assert.equal(eco.network_res_minted_display, null);
    assert.equal(eco.res_minted_total_atoms, null);
    assert.equal(eco.oru_plain_destroy_atoms, null);
    assert.equal(eco.oru_plain_destroy_display, null);
    assert.equal(eco.oru_indexed_utxo_total_atoms, null);
    assert.equal(eco.oru_utxo_count, null);
    assert.equal(dashboard.cache.economic_status, "loading");
    assert.equal(
      dashboard.economic_snapshot.res_destroy_cost_display,
      formatRateOruPerRes(resDestroyCostForHeight(1901)),
    );
    assert.equal(dashboard.block_health.current_height_node, 1900);
    const minted = JSON.stringify(eco.network_res_minted_display);
    assert.equal(minted, "null");
  });

  it("keeps a computed economic snapshot unchanged", () => {
    const dashboard = assembleStatusDashboard(liveParts(), economicParts(), {
      ...loadingStatusCache(),
      economic_status: "ready",
      economic_generated_at: "2026-11-02T00:00:00.000Z",
      snapshot_height: 1900,
      cache_age_seconds: 12,
      stale: false,
      live_status: "ready",
    });
    assert.equal(dashboard.economic_snapshot.network_res_minted_display, atomsToResDisplay(MINT_ATOMS));
    assert.equal(dashboard.economic_snapshot.res_minted_total_atoms, MINT_ATOMS);
    assert.equal(dashboard.economic_snapshot.oru_plain_destroy_atoms, "7");
    assert.equal(dashboard.economic_snapshot.oru_plain_destroy_display, "0.00000007 ORU");
    const fee = dashboard.economic_snapshot.rfc018_fee_burn as { cumulative_burned_oru: string };
    assert.equal(fee.cumulative_burned_oru, "457");
    assert.equal(dashboard.cache.snapshot_height, 1900);
    assert.equal(dashboard.cache.cache_age_seconds, 12);
    assert.equal(dashboard.cache.stale, false);
  });
});

describe("cached status HTTP", () => {
  let queries = 0;
  let economicLoads = 0;
  const pool = {
    query: async () => {
      queries += 1;
      throw new Error("request path must not query");
    },
  } as unknown as Pool;

  after(() => {
    stopExplorerCaches();
  });

  it("answers homepage and status from memory, including 100 concurrent readers", async () => {
    const economic = new SharedSnapshot<EconomicAggregates>({
      intervalMs: 45_000,
      minIntervalMs: 30_000,
      load: async () => {
        economicLoads += 1;
        return { value: economicParts(), snapshotHeight: 1900 };
      },
    });
    const live = new SharedSnapshot<LiveStatusParts>({
      intervalMs: 10_000,
      minIntervalMs: 5_000,
      load: async () => ({ value: liveParts(), snapshotHeight: 1900 }),
    });
    await economic.refresh(true);
    await live.refresh(true);
    installExplorerCachesForTests({ live, economic });
    assert.equal(economicLoads, 1);

    const app = express();
    const publicDir = join(__dirname, "..", "public");
    const config = { network: "mainnet" } as Config;
    registerUiRoutes(app, pool, publicDir, config);
    registerApiRoutes(app, pool, config);
    const server = createServer(app);
    server.listen(0, "127.0.0.1");
    await once(server, "listening");
    const port = (server.address() as AddressInfo).port;
    const base = `http://127.0.0.1:${port}`;

    try {
      const cold = readCachedStatus();
      assert.equal(cold.cache.economic_status, "ready");

      const homeStarted = performance.now();
      const home = await fetch(base + "/", { headers: { Accept: "text/html" } });
      const homeHtml = await home.text();
      const homeMs = performance.now() - homeStarted;
      assert.equal(home.status, 200);
      assert.match(homeHtml, /data-ssr="1"/);
      assert.match(homeHtml, /3\.00000000 RES/);
      assert.ok(homeMs < 300, `homepage ${homeMs.toFixed(1)}ms`);

      const oneStarted = performance.now();
      const one = await fetch(base + "/api/status");
      const oneBody = (await one.json()) as { cache: { economic_status: string; snapshot_height: number } };
      const oneMs = performance.now() - oneStarted;
      assert.equal(one.status, 200);
      assert.equal(oneBody.cache.economic_status, "ready");
      assert.equal(oneBody.cache.snapshot_height, 1900);
      assert.ok(oneMs < 200, `status ${oneMs.toFixed(1)}ms`);

      const batchStarted = performance.now();
      const bodies = await Promise.all(
        Array.from({ length: 100 }, async () => {
          const response = await fetch(base + "/api/status");
          return response.json() as Promise<{ economic_snapshot: { res_minted_total_atoms: string } }>;
        }),
      );
      const batchMs = performance.now() - batchStarted;
      assert.equal(bodies.length, 100);
      for (const body of bodies) {
        assert.equal(body.economic_snapshot.res_minted_total_atoms, MINT_ATOMS);
      }
      assert.equal(economicLoads, 1);
      assert.equal(queries, 0);
      console.log(
        JSON.stringify({
          homepage_ms: Number(homeMs.toFixed(2)),
          status_ms: Number(oneMs.toFixed(2)),
          status_100_ms: Number(batchMs.toFixed(2)),
          economic_loads: economicLoads,
          queries,
        }),
      );
    } finally {
      server.close();
      await delay(20);
    }
  });

  it("returns loading instead of zeros when no snapshot exists", async () => {
    stopExplorerCaches();
    const dashboard = readCachedStatus();
    assert.equal(dashboard.cache.economic_status, "loading");
    assert.equal(dashboard.cache.live_status, "loading");
    assert.equal(dashboard.economic_snapshot.network_res_minted_display, null);
    assert.equal(dashboard.economic_snapshot.res_minted_total_atoms, null);
    const oru = readCachedOruEconomics();
    assert.equal(oru.status, "loading");
    assert.equal(oru.view, null);
  });

  it("does not start a second cache after one is installed", () => {
    const economic = new SharedSnapshot<EconomicAggregates>({
      intervalMs: 45_000,
      minIntervalMs: 30_000,
      load: async () => {
        economicLoads += 1;
        return { value: economicParts(), snapshotHeight: 1 };
      },
    });
    installExplorerCachesForTests({ economic });
    const before = economicLoads;
    startExplorerCaches({} as Config, pool);
    assert.equal(economicLoads, before);
    assert.equal(queries, 0);
  });

  it("keeps request handlers off the aggregation functions", () => {
    const routes = readFileSync(join(__dirname, "..", "src", "registerRoutes.ts"), "utf8");
    assert.doesNotMatch(routes, /buildStatusDashboard|loadEconomicAggregates|buildOruEconomicsView/);
    const index = readFileSync(join(__dirname, "..", "src", "index.ts"), "utf8");
    assert.match(index, /startExplorerCaches/);
    assert.match(index, /stopExplorerCaches/);
    const sql = readFileSync(join(__dirname, "..", "src", "resTransferQuery.ts"), "utf8");
    assert.match(sql, /i\.txid IN \(SELECT txid FROM page\)/);
    assert.match(sql, /ORDER BY b.height DESC, t.block_tx_index DESC, o.vout_index ASC/);
  });
});
