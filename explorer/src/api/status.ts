import type { Pool } from "pg";
import type { Config } from "../config";
import { buildCompliancePayload, EQUIVALENCE_CLAIMED } from "../compliance";
import { getOrCreateChainState } from "../db";
import { CIRCULATING_ORU_SQL_PREDICATE, COINBASE_MATURITY_DISPLAY, matureSpendableOruSql, opConvergeScriptSql, plainDestroyOruSql, unlockedOruSql } from "../circulating";
import { atomsFromSql, reconcileOruSupplySplit } from "../accounting";
import { atomsToOruDisplay, atomsToResDisplay } from "../amount";
import { rpc } from "../rpcCli";
import { rfc018FeeBurnView } from "../rfc018FeeBurn";
import {
  confirmHeightForTip,
  formatDerivedMaxSupply,
  formatRateOruPerRes,
  RES_DERIVED_MAX_SUPPLY,
  resDestroyCostForHeight,
} from "../resIssuanceSchedule";
import { backfillMissingRfc018FeeBurn, getRfc018FeeBurnAtHeight } from "../rfc018FeeBurnStore";
import { estimateNetworkHashrate } from "../networkHashrate";

/** FVR-M compliance display — read-only, never computed at runtime. */
export const DRAW_STATUS_DISPLAY = "ORU destroyed. RES minted.";

export const RPC_AUTHORITY_DISCLAIMER =
  "All values are authoritative from rigord RPC. No local consensus computation is performed.";

export type StatusDashboard = {
  generated_at: string;
  rpc_authority_disclaimer: string;
  chain_identity: Record<string, unknown>;
  block_health: Record<string, unknown>;
  convergence_health: Record<string, unknown>;
  economic_snapshot: Record<string, unknown>;
  mining_activity: Record<string, unknown>;
  system_integrity: Record<string, unknown>;
};

/** getblockchaininfo / getruntimesupply amounts are ValueFromAmount decimals; asAtoms treats the value as integer atoms. */
function formatOruAmount(value: unknown, asAtoms = false): string | null {
  if (value == null || value === "") return null;
  if (asAtoms) {
    try {
      return atomsToOruDisplay(String(Math.trunc(Number(value))));
    } catch {
      return null;
    }
  }
  const n = typeof value === "number" ? value : Number(value);
  if (!Number.isFinite(n)) return null;
  return `${n.toFixed(8)} ORU`;
}

/** Display-only copy of consensus coinbase maturity. Not a consensus rule. */
export { COINBASE_MATURITY_DISPLAY };

/** Coinbase is transferable when confirmations >= COINBASE_MATURITY (tip - height + 1). */
export function isCoinbaseMature(tipHeight: number, outputHeight: number, maturity = COINBASE_MATURITY_DISPLAY): boolean {
  if (!Number.isInteger(tipHeight) || !Number.isInteger(outputHeight)) return false;
  if (tipHeight < 0 || outputHeight < 0 || outputHeight > tipHeight) return false;
  return tipHeight - outputHeight + 1 >= maturity;
}

/** Average block interval from indexed `blocks.time` (last N rows). */
export function averageBlockTimeSeconds(times: number[]): number | null {
  if (times.length < 2) return null;
  const sorted = [...times].sort((a, b) => a - b);
  let total = 0;
  for (let i = 1; i < sorted.length; i++) total += sorted[i] - sorted[i - 1];
  return total / (sorted.length - 1);
}

/** Blocks until the newest candidate ages out of the sliding draw window. Display only. */
export function drawWindowRemaining(args: {
  tipHeight: number;
  windowSize: number;
  sourceHeights: number[];
}): { remaining: number | null; completesAt: number | null } {
  if (!Number.isFinite(args.tipHeight) || args.tipHeight < 0) {
    return { remaining: null, completesAt: null };
  }
  if (!Number.isFinite(args.windowSize) || args.windowSize <= 0 || args.sourceHeights.length === 0) {
    return { remaining: null, completesAt: null };
  }
  const completesAt = Math.max(...args.sourceHeights.map((s) => Number(s) + args.windowSize - 1));
  if (!Number.isFinite(completesAt)) return { remaining: null, completesAt: null };
  return { remaining: Math.max(0, completesAt - args.tipHeight), completesAt };
}

/** Histogram of |F| sizes from per-block candidate counts. */
export function fSetDistribution(counts: number[]): Record<string, number> {
  const dist: Record<string, number> = {};
  for (const n of counts) {
    const key = String(n);
    dist[key] = (dist[key] ?? 0) + 1;
  }
  return dist;
}

async function fetchWalletObserverStatus(): Promise<Record<string, unknown>> {
  const base = process.env.WALLET_OBSERVER_URL?.trim();
  if (!base) {
    return { status: "not_configured", message: "WALLET_OBSERVER_URL not set" };
  }
  try {
    const res = await fetch(`${base.replace(/\/$/, "")}/api/observer/chain`, {
      signal: AbortSignal.timeout(5000),
    });
    if (!res.ok) return { status: "unreachable", http_status: res.status };
    const body = (await res.json()) as { ok?: boolean; syncLag?: number | null };
    return {
      status: body.ok ? "ok" : "error",
      sync_lag_blocks: body.syncLag ?? null,
    };
  } catch (e) {
    return { status: "unreachable", error: String(e) };
  }
}

export async function buildStatusDashboard(config: Config, pool: Pool): Promise<StatusDashboard> {
  const chainState = await getOrCreateChainState(pool, config);
  const compliance = await buildCompliancePayload(config, chainState);

  let blockchainInfo: Record<string, unknown> = {};
  let networkInfo: Record<string, unknown> = {};
  let convStatus: Record<string, unknown> = {};
  let runtimeSupply: Record<string, unknown> = {};
  let mempoolTxids: string[] = [];
  let rpcError: string | null = null;

  try {
    const [bci, net, mempool, status, supply] = await Promise.all([
      rpc.getblockchaininfo(config),
      rpc.getnetworkinfo(config),
      rpc.getrawmempool(config),
      rpc.getconvergencestatus(config).catch(() => ({})),
      rpc.getruntimesupply(config).catch(() => ({})),
    ]);
    blockchainInfo = (bci ?? {}) as Record<string, unknown>;
    networkInfo = (net ?? {}) as Record<string, unknown>;
    convStatus = (status ?? {}) as Record<string, unknown>;
    runtimeSupply = (supply ?? {}) as Record<string, unknown>;
    if (Array.isArray(mempool)) mempoolTxids = mempool as string[];
    else if (mempool && typeof mempool === "object") mempoolTxids = Object.keys(mempool as object);
  } catch (e) {
    rpcError = String(e);
  }

  const nodeHeight = Number(blockchainInfo.blocks ?? -1);
  const indexerHeight = Number(chainState.last_height);

  const blockWindow = await pool.query(
    `
      SELECT height, time, bits, opus_hash, rigor_winner, rigor_res_minted,
             committed_root, computed_root, mismatch, draw_candidate_root
      FROM blocks
      ORDER BY height DESC
      LIMIT 100
    `,
  );

  const blocks = blockWindow.rows;
  const times = blocks.map((b) => Number(b.time)).filter((t) => Number.isFinite(t));
  const avgBlockTime = averageBlockTimeSeconds(times);
  // Same 100-block read already used for average interval. No extra history scan.
  const networkHashrate = estimateNetworkHashrate(blocks, Math.floor(Date.now() / 1000));

  const tipBlock = blocks[0] ?? null;

  const fCounts = await pool.query(
    `
      SELECT b.height, COUNT(ce.convergence_txid)::int AS f_size
      FROM blocks b
      LEFT JOIN convergence_events ce ON ce.block_hash = b.block_hash
      WHERE b.height > GREATEST(0, (SELECT COALESCE(MAX(height), 0) FROM blocks) - 100)
      GROUP BY b.height
      ORDER BY b.height DESC
    `,
  );
  const fSizes = fCounts.rows.map((r) => Number(r.f_size ?? 0));
  const mintBlocks = blocks.filter((b) => Number(b.rigor_res_minted ?? 0) > 0).length;
  const rootChanges = new Set(blocks.map((b) => String(b.committed_root ?? ""))).size;
  const mismatchBlocks = blocks.filter((b) => b.mismatch).length;

  const economic = await pool.query(
    `
      SELECT
        (SELECT COALESCE(SUM(amount_atoms), 0) FROM oru_utxos WHERE ${CIRCULATING_ORU_SQL_PREDICATE}) AS oru_unspent_unlocked_atoms,
        (SELECT COALESCE(SUM(u.amount_atoms), 0)
           FROM oru_utxos u
           JOIN transactions t ON t.txid = u.txid
          WHERE ${matureSpendableOruSql()}
        ) AS oru_utxo_atoms,
        (SELECT COUNT(*)::int
           FROM oru_utxos u
           JOIN transactions t ON t.txid = u.txid
          WHERE ${matureSpendableOruSql()}
        ) AS oru_utxo_count,
        (SELECT COALESCE(SUM(u.amount_atoms), 0)
           FROM oru_utxos u
           JOIN transactions t ON t.txid = u.txid
          WHERE ${matureSpendableOruSql()}
        ) AS oru_mature_transferable_atoms,
        (SELECT COUNT(*)::int
           FROM oru_utxos u
           JOIN transactions t ON t.txid = u.txid
          WHERE ${matureSpendableOruSql()}
        ) AS oru_mature_transferable_count,
        (SELECT COALESCE(SUM(u.amount_atoms), 0)
           FROM oru_utxos u
           JOIN transactions t ON t.txid = u.txid
          WHERE ${unlockedOruSql("u")}
            AND COALESCE(t.is_coinbase, FALSE) = TRUE
            AND ((SELECT COALESCE(MAX(height), 0) FROM blocks) - u.height + 1) < ${COINBASE_MATURITY_DISPLAY}
        ) AS oru_immature_coinbase_atoms,
        (SELECT COALESCE(SUM(o.value_atoms), 0)
           FROM transactions t
           JOIN outputs o ON o.txid = t.txid AND o.asset_kind = 'ORU'
          WHERE t.block_tx_index = 0) AS oru_coinbase_total_atoms,
        (SELECT COALESCE(SUM(o.value_atoms), 0)
           FROM transactions t
           JOIN outputs o ON o.txid = t.txid AND o.asset_kind = 'ORU'
          WHERE t.block_tx_index = 0
            AND t.block_hash = (SELECT block_hash FROM blocks ORDER BY height DESC LIMIT 1)
        ) AS oru_subsidy_tip_atoms,
        (SELECT COUNT(DISTINCT o.txid)::int
           FROM outputs o
          WHERE o.asset_kind = 'ORU'
            AND ${opConvergeScriptSql("o.script_pubkey")}
            AND o.value_atoms > 0
        ) AS convergence_event_count,
        (SELECT COUNT(*)::int FROM convergence_events) AS convergence_eligible_count,
        (SELECT COALESCE(SUM(mint_amount), 0) FROM res_mints) AS res_mint_total_atoms,
        (SELECT COUNT(*)::int FROM res_mints) AS res_mint_count,
        (SELECT COALESCE(SUM(rm.mint_amount), 0)
           FROM res_mints rm
           JOIN blocks b ON b.block_hash = rm.block_hash
          WHERE b.height > GREATEST(0, (SELECT COALESCE(MAX(height), 0) FROM blocks) - 100)
        ) AS res_mint_last_100_atoms,
        (SELECT MAX(block_height) FROM res_mints) AS last_res_mint_height,
        (SELECT mint_txid FROM res_mints ORDER BY block_height DESC, mint_vout_index ASC LIMIT 1) AS last_res_mint_txid,
        (SELECT COUNT(*)::int
           FROM res_mints rm
           JOIN blocks b ON b.block_hash = rm.block_hash
          WHERE b.height > GREATEST(0, (SELECT COALESCE(MAX(height), 0) FROM blocks) - 100)
        ) AS res_mint_count_last_100,
        (SELECT COUNT(DISTINCT o.txid)::int
           FROM outputs o
           JOIN transactions t ON t.txid = o.txid
          WHERE o.asset_kind = 'ORU'
            AND ${opConvergeScriptSql("o.script_pubkey")}
            AND o.value_atoms > 0
            AND t.block_height > GREATEST(0, (SELECT COALESCE(MAX(height), 0) FROM blocks) - 100)
        ) AS convergence_burns_last_100,
        (SELECT COALESCE(SUM(o.value_atoms), 0)
           FROM outputs o
          WHERE ${plainDestroyOruSql("o")}
        ) AS oru_plain_destroy_atoms
    `,
  );
  const eco = economic.rows[0] ?? {};
  const oruSupplyAccounting = reconcileOruSupplySplit({
    matureAtoms: atomsFromSql(eco.oru_mature_transferable_atoms),
    immatureAtoms: atomsFromSql(eco.oru_immature_coinbase_atoms),
    unspentUnlockedAtoms: atomsFromSql(eco.oru_unspent_unlocked_atoms),
  });
  if (!oruSupplyAccounting.consistent) {
    console.error(
      JSON.stringify({
        level: "ERROR",
        component: "oru-supply-accounting",
        indexer_height: indexerHeight,
        errors: oruSupplyAccounting.errors,
      }),
    );
  }
  const tipForRate = nodeHeight >= 0 ? nodeHeight : indexerHeight;
  const rpcConfirm = Number(convStatus.confirm_height);
  const confirmHeight =
    Number.isFinite(rpcConfirm) && rpcConfirm >= 0
      ? rpcConfirm
      : confirmHeightForTip(Number.isFinite(tipForRate) ? tipForRate : 0);
  const resRateAtoms = resDestroyCostForHeight(confirmHeight);
  let feeBurnAtTip: Awaited<ReturnType<typeof getRfc018FeeBurnAtHeight>> = null;
  try {
    if (indexerHeight >= 0) {
      feeBurnAtTip = await getRfc018FeeBurnAtHeight(pool, indexerHeight);
      if (feeBurnAtTip == null) {
        await backfillMissingRfc018FeeBurn(pool, 600);
        feeBurnAtTip = await getRfc018FeeBurnAtHeight(pool, indexerHeight);
      }
    }
  } catch {
    feeBurnAtTip = null;
  }
  const rfc018FeeBurn = {
    ...rfc018FeeBurnView(
      feeBurnAtTip?.burned_oru ?? 0n,
      feeBurnAtTip?.cumulative_burned_oru ?? 0n,
      atomsToOruDisplay,
    ),
    indexed: feeBurnAtTip != null,
    height: feeBurnAtTip != null && indexerHeight >= 0 ? String(indexerHeight) : null,
  };
  const totalIssuedDisplay =
    formatOruAmount(runtimeSupply.oru_subsidy_cumulative_derived) ??
    formatOruAmount(runtimeSupply.oru_supply_cumulative);
  const circulatingDisplay = atomsToOruDisplay(String(eco.oru_utxo_atoms ?? "0"));
  const convergenceDestroyedDisplay = formatOruAmount(blockchainInfo.cumulative_convergence);
  const plainDestroyAtoms = atomsFromSql(eco.oru_plain_destroy_atoms);
  const plainDestroyDisplay = atomsToOruDisplay(plainDestroyAtoms);
  const oruSubsidyScheduleDisplay = "Genesis 0 ORU; then 128 ORU/block forever";

  const reorgWindow = await pool.query(
    `
      SELECT COUNT(*)::int AS mismatch_blocks
      FROM blocks
      WHERE mismatch = TRUE
        AND height > GREATEST(0, (SELECT COALESCE(MAX(height), 0) FROM blocks) - 1000)
    `,
  );

  const indexerUpdatedAt = chainState.updated_at instanceof Date ? chainState.updated_at.getTime() : Date.now();
  const indexerLagBlocks =
    nodeHeight >= 0 && indexerHeight >= 0 ? Math.max(0, nodeHeight - indexerHeight) : null;
  const indexerLagSeconds =
    indexerLagBlocks === 0
      ? 0
      : Math.max(0, Math.floor((Date.now() - indexerUpdatedAt) / 1000));

  const walletObserver = await fetchWalletObserverStatus();

  const networkLabel =
    compliance.network === "testnet"
      ? "testnet"
      : compliance.network === "mainnet"
        ? "mainnet"
        : compliance.network;

  return {
    generated_at: new Date().toISOString(),
    rpc_authority_disclaimer: RPC_AUTHORITY_DISCLAIMER,
    chain_identity: {
      chain_name: compliance.chain_name,
      product: compliance.product,
      codename: compliance.codename,
      network: networkLabel,
      network_type: compliance.network_type,
      genesis_hash: compliance.genesis_hash,
      genesis_hash_short: compliance.genesis_hash_short,
      node_subversion: compliance.node_subversion ?? networkInfo.subversion ?? null,
      node_release_version: compliance.node_release_version,
      node_version: networkInfo.version ?? null,
      explorer_version: compliance.explorer_version,
      equivalence_claimed: EQUIVALENCE_CLAIMED,
    },
    block_health: {
      current_height_node: nodeHeight >= 0 ? nodeHeight : null,
      current_height_indexer: indexerHeight >= 0 ? indexerHeight : null,
      best_block_hash: blockchainInfo.bestblockhash ?? chainState.last_block_hash,
      avg_block_time_seconds_last_100: avgBlockTime,
      difficulty: blockchainInfo.difficulty ?? null,
      difficulty_label: "ASERT / OpuHash (node-reported)",
      bits: tipBlock?.bits ?? null,
      network_hashrate: networkHashrate,
      opus_hash: tipBlock?.opus_hash ?? null,
      pow_hash: tipBlock?.opus_hash ?? null,
      orphan_rate: null,
      orphan_rate_note: "Orphan blocks are not persisted in indexer schema",
      reorg_signal_blocks_mismatch_last_1000: reorgWindow.rows[0]?.mismatch_blocks ?? 0,
      reorg_count_note:
        "Full reorg count is not persisted; mismatch block count shown as integrity signal only",
      initialblockdownload: blockchainInfo.initialblockdownload ?? null,
      verificationprogress: blockchainInfo.verificationprogress ?? null,
    },
    convergence_health: {
      issuance_status: DRAW_STATUS_DISPLAY,
      mint_blocks_last_100: mintBlocks,
      convergence_burns_last_100: fSizes.filter((n) => n > 0).length,
      committed_root_stability: {
        distinct_committed_roots_last_100: rootChanges,
        mismatch_blocks_last_100: mismatchBlocks,
        stable: mismatchBlocks === 0,
      },
      source: "indexed convergence_events + getconvergencestatus",
    },
    economic_snapshot: {
      tip_height: nodeHeight >= 0 ? nodeHeight : indexerHeight,
      confirm_height: confirmHeight,
      res_destroy_cost_atoms: resRateAtoms.toString(),
      res_destroy_cost_display: formatRateOruPerRes(resRateAtoms),
      res_derived_max_supply_atoms: RES_DERIVED_MAX_SUPPLY.toString(),
      res_derived_max_supply_display: formatDerivedMaxSupply(),
      res_mint_count_last_100: eco.res_mint_count_last_100 ?? 0,
      convergence_burns_last_100: eco.convergence_burns_last_100 ?? 0,
      network_res_minted_display: atomsToResDisplay(String(eco.res_mint_total_atoms ?? "0")),
      oru_burned_lifetime_display: convergenceDestroyedDisplay,
      cumulative_convergence_display: convergenceDestroyedDisplay,
      oru_plain_destroy_atoms: plainDestroyAtoms.toString(),
      oru_plain_destroy_display: plainDestroyDisplay,
      rfc018_fee_burn: rfc018FeeBurn,
      supply_chart: {
        total_issued_oru: {
          display: totalIssuedDisplay,
          source: "getruntimesupply.oru_subsidy_cumulative_derived (Σ R); ledger S_ORU is not this series",
        },
        cumulative_convergence: {
          display: convergenceDestroyedDisplay,
          rpc: blockchainInfo.cumulative_convergence ?? null,
          source: "getblockchaininfo.cumulative_convergence (Convergence destroyed ORU)",
        },
        plain_destroy: {
          atoms: plainDestroyAtoms.toString(),
          display: plainDestroyDisplay,
          source: "indexed OP_CONVERGE with no RES mint; subset of Convergence destroyed, not a fifth series",
        },
        rfc018_fee_burn: {
          block_burned_oru: rfc018FeeBurn.block_burned_oru,
          cumulative_burned_oru: rfc018FeeBurn.cumulative_burned_oru,
          block_burned_oru_display: rfc018FeeBurn.block_burned_oru_display,
          cumulative_burned_oru_display: rfc018FeeBurn.cumulative_burned_oru_display,
          source: "explorer rfc018_fee_burn_stats (not consensus)",
        },
        oru_circulating_spendable: {
          atoms: String(eco.oru_utxo_atoms ?? "0"),
          display: circulatingDisplay,
          source:
            "indexed oru_utxos: unspent, not OP_CONVERGE, not retired CVG2, coinbase mature (indexer height)",
        },
        never_merged: true,
        note: "Total issued, Convergence destroyed, fees destroyed, and circulating spendable are four separate series. Do not add or subtract them into one explorer figure.",
        equivalence_claimed: EQUIVALENCE_CLAIMED,
      },
      oru_since_last_mint_display: formatOruAmount(blockchainInfo.current_convergence_sum),
      oru_indexed_utxo_total_display: atomsToOruDisplay(String(eco.oru_utxo_atoms ?? "0")),
      oru_circulating_spendable_display: atomsToOruDisplay(String(eco.oru_utxo_atoms ?? "0")),
      oru_utxo_count: eco.oru_utxo_count ?? 0,
      oru_mature_transferable_display: atomsToOruDisplay(String(eco.oru_mature_transferable_atoms ?? "0")),
      oru_mature_transferable_count: eco.oru_mature_transferable_count ?? 0,
      oru_immature_coinbase_display: atomsToOruDisplay(String(eco.oru_immature_coinbase_atoms ?? "0")),
      oru_coinbase_total_display: atomsToOruDisplay(String(eco.oru_coinbase_total_atoms ?? "0")),
      oru_subsidy_tip_display:
        formatOruAmount(runtimeSupply.oru_subsidy_block) ?? atomsToOruDisplay(String(eco.oru_subsidy_tip_atoms ?? "0")),
      oru_subsidy_schedule_display: oruSubsidyScheduleDisplay,
      accounting: {
        indexer_height: indexerHeight >= 0 ? indexerHeight : null,
        node_height: nodeHeight >= 0 ? nodeHeight : null,
        issued_source: "node getruntimesupply at node height",
        circulating_source: "indexer UTXO set at indexer height; mature only",
        oru_supply_consistent: oruSupplyAccounting.consistent,
        oru_supply_errors: oruSupplyAccounting.errors,
      },
      oru_supply_cumulative_display: formatOruAmount(runtimeSupply.oru_supply_cumulative),
      oru_supply_parent_display: formatOruAmount(runtimeSupply.oru_supply_parent),
      oru_burn_block_display: formatOruAmount(runtimeSupply.oru_converge_burn_block),
      convergence_event_count: eco.convergence_event_count ?? 0,
      convergence_burn_count: eco.convergence_eligible_count ?? 0,
      res_mint_rate_last_100_blocks_display: atomsToResDisplay(String(eco.res_mint_last_100_atoms ?? "0")),
      res_mint_count: eco.res_mint_count ?? 0,
      last_res_height: convStatus.last_res_height ?? eco.last_res_mint_height ?? null,
      last_res_mint_txid: eco.last_res_mint_txid ?? null,
      oru_indexed_utxo_total_atoms: String(eco.oru_utxo_atoms ?? "0"),
      oru_cumulative_convergence_rpc: blockchainInfo.cumulative_convergence ?? null,
      res_minted_total_atoms: String(eco.res_mint_total_atoms ?? "0"),
      res_minted_total_display: atomsToResDisplay(String(eco.res_mint_total_atoms ?? "0")),
      res_mint_rate_last_100_blocks_atoms: String(eco.res_mint_last_100_atoms ?? "0"),
    },
    mining_activity: {
      mempool_tx_count: mempoolTxids.length,
      connections: networkInfo.connections ?? null,
      active_miners: null,
      pending_blocks_active_jobs: null,
      stale_job_count: null,
      availability_note:
        "Mempool from rigord getrawmempool; Stratum/share-validator metrics not wired in explorer MVP",
    },
    system_integrity: {
      rpc_authority: "rigord: authoritative",
      rpc_error: rpcError,
      indexer_sync_lag_blocks: indexerLagBlocks,
      indexer_sync_lag_seconds: indexerLagSeconds,
      indexer_last_updated_at:
        chainState.updated_at instanceof Date
          ? chainState.updated_at.toISOString()
          : chainState.updated_at,
      wallet_observer: walletObserver,
      explorer_compliance: {
        frozen: true,
        equivalence_claimed: EQUIVALENCE_CLAIMED,
        mismatch_active: compliance.mismatch_active,
        mismatches: compliance.mismatches,
        mismatch_messages: compliance.mismatch_messages,
        rpc_degraded: compliance.rpc_degraded,
        rpc_messages: compliance.rpc_messages,
      },
    },
  };
}
