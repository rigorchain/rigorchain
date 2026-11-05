import type { Pool } from "pg";
import type { Config } from "../config";
import { atomsFromSql } from "../accounting";
import { oruStringToAtoms } from "../amount";
import { confirmHeightForTip } from "../resIssuanceSchedule";
import { rpc } from "../rpcCli";
import { buildOruEconomics, type OruEconomicsInput, type OruEconomicsView } from "../oruEconomics";
import { oruEconomicsAggregateSql } from "../oruEconomicsQuery";

function parseRpcOru(value: unknown): bigint | null {
  if (value == null || value === "") return null;
  try {
    if (typeof value === "number") {
      if (!Number.isFinite(value)) return null;
      return oruStringToAtoms(value.toFixed(8));
    }
    return oruStringToAtoms(String(value));
  } catch {
    return null;
  }
}

function parseOptionalHeight(value: unknown): number | null {
  if (value == null || value === "") return null;
  const n = Number(value);
  if (!Number.isInteger(n) || n < 0) return null;
  return n;
}

export async function buildOruEconomicsView(config: Config, pool: Pool): Promise<OruEconomicsView> {
  const aggregate = await pool.query(oruEconomicsAggregateSql());
  const row = aggregate.rows[0] ?? {};

  let nodeHeight: number | null = null;
  let nodeBestBlockHash: string | null = null;
  let nodeIssuedAtoms: bigint | null = null;
  let nodeConvergenceAtoms: bigint | null = null;
  let confirmHeight: number | null = null;

  try {
    const [bci, supply, conv] = await Promise.all([
      rpc.getblockchaininfo(config),
      rpc.getruntimesupply(config).catch(() => ({})),
      rpc.getconvergencestatus(config).catch(() => ({})),
    ]);
    const info = (bci ?? {}) as Record<string, unknown>;
    const runtime = (supply ?? {}) as Record<string, unknown>;
    const status = (conv ?? {}) as Record<string, unknown>;
    const height = Number(info.blocks);
    nodeHeight = Number.isInteger(height) && height >= 0 ? height : null;
    nodeBestBlockHash = typeof info.bestblockhash === "string" ? info.bestblockhash : null;
    nodeIssuedAtoms = parseRpcOru(runtime.oru_subsidy_cumulative_derived ?? runtime.oru_supply_cumulative);
    nodeConvergenceAtoms = parseRpcOru(info.cumulative_convergence);
    const rpcConfirm = Number(status.confirm_height);
    const tipForRate = nodeHeight ?? parseOptionalHeight(row.indexer_height) ?? -1;
    confirmHeight =
      Number.isFinite(rpcConfirm) && rpcConfirm >= 0
        ? rpcConfirm
        : tipForRate >= 0
          ? confirmHeightForTip(tipForRate)
          : null;
  } catch {
    const indexed = parseOptionalHeight(row.indexer_height);
    confirmHeight = indexed == null ? null : confirmHeightForTip(indexed);
  }

  const feeRaw = row.fee_atoms;

  const input: OruEconomicsInput = {
    indexerHeight: parseOptionalHeight(row.indexer_height),
    indexerBlockHash: row.indexer_block_hash == null ? null : String(row.indexer_block_hash),
    nodeHeight,
    nodeBestBlockHash,
    confirmHeight,
    nodeIssuedAtoms,
    nodeConvergenceAtoms,
    headerConvergenceAtoms: parseRpcOru(row.tip_cumulative_convergence),
    indexedConvergenceAtoms: atomsFromSql(row.convergence_atoms),
    plainAtoms: atomsFromSql(row.plain_atoms),
    attributableOruAtoms: atomsFromSql(row.attributable_oru_atoms),
    attributableResAtoms: atomsFromSql(row.attributable_res_atoms),
    totalResAtoms: atomsFromSql(row.total_res_atoms),
    feeAtoms: feeRaw == null || feeRaw === "" ? null : atomsFromSql(feeRaw),
    eventCount: Number(row.event_count ?? 0),
    lastMintHeight: parseOptionalHeight(row.last_mint_height),
    recent: [],
  };
  return buildOruEconomics(input);
}
