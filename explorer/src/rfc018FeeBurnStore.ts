/**
 * Persist RFC-018 explorer fee-burn rows. Reorg uses DELETE FROM blocks
 * (CASCADE). Remaining prefix cumulative_burned_oru stays correct.
 */

import type { Pool, PoolClient } from "pg";
import {
  nextCumulativeBurn,
  rfc018DestroyedFeeOru,
  type IndexedAsset,
  type IndexedOutput,
  type IndexedPrevout,
} from "./rfc018FeeBurn";

export async function upsertRfc018FeeBurnStats(
  pool: Pool | PoolClient,
  height: bigint,
  blockHash: string,
  burnedOru: bigint,
): Promise<{ burned_oru: bigint; cumulative_burned_oru: bigint }> {
  let parentCum = 0n;
  if (height > 0n) {
    const parent = await pool.query<{ cumulative_burned_oru: string }>(
      `SELECT cumulative_burned_oru::text AS cumulative_burned_oru
       FROM rfc018_fee_burn_stats WHERE height = $1`,
      [height - 1n],
    );
    if (parent.rows[0]) parentCum = BigInt(parent.rows[0].cumulative_burned_oru);
  }
  const cumulative = nextCumulativeBurn(parentCum, burnedOru);
  await pool.query(
    `
      INSERT INTO rfc018_fee_burn_stats(height, block_hash, burned_oru, cumulative_burned_oru)
      VALUES ($1, $2, $3, $4)
      ON CONFLICT (height) DO UPDATE SET
        block_hash = EXCLUDED.block_hash,
        burned_oru = EXCLUDED.burned_oru,
        cumulative_burned_oru = EXCLUDED.cumulative_burned_oru
    `,
    [height, blockHash, burnedOru.toString(), cumulative.toString()],
  );
  return { burned_oru: burnedOru, cumulative_burned_oru: cumulative };
}

export async function getRfc018FeeBurnAtHeight(
  pool: Pool,
  height: bigint | number | string,
): Promise<{ burned_oru: bigint; cumulative_burned_oru: bigint; block_hash: string } | null> {
  const res = await pool.query<{ burned_oru: string; cumulative_burned_oru: string; block_hash: string }>(
    `
      SELECT burned_oru::text AS burned_oru,
             cumulative_burned_oru::text AS cumulative_burned_oru,
             block_hash
      FROM rfc018_fee_burn_stats
      WHERE height = $1::bigint
    `,
    [String(height)],
  );
  const row = res.rows[0];
  if (!row) return null;
  return {
    burned_oru: BigInt(row.burned_oru),
    cumulative_burned_oru: BigInt(row.cumulative_burned_oru),
    block_hash: row.block_hash,
  };
}

export async function getRfc018FeeBurnTip(
  pool: Pool,
): Promise<{ burned_oru: bigint; cumulative_burned_oru: bigint; height: bigint } | null> {
  const res = await pool.query<{ burned_oru: string; cumulative_burned_oru: string; height: string }>(
    `
      SELECT burned_oru::text AS burned_oru,
             cumulative_burned_oru::text AS cumulative_burned_oru,
             height::text AS height
      FROM rfc018_fee_burn_stats
      ORDER BY height DESC
      LIMIT 1
    `,
  );
  const row = res.rows[0];
  if (!row) return null;
  return {
    burned_oru: BigInt(row.burned_oru),
    cumulative_burned_oru: BigInt(row.cumulative_burned_oru),
    height: BigInt(row.height),
  };
}

function asAsset(kind: string | null | undefined): IndexedAsset {
  if (kind === "ORU" || kind === "RES" || kind === "INVALID") return kind;
  return "NONE";
}

export async function computeRfc018BurnForStoredBlock(pool: Pool, blockHash: string): Promise<bigint> {
  const mint = await pool.query<{ mint_txid: string }>(
    `SELECT mint_txid FROM res_mints WHERE block_hash = $1 LIMIT 1`,
    [blockHash],
  );
  const mintTxid = mint.rows[0]?.mint_txid ?? null;

  const txs = await pool.query<{ txid: string; is_coinbase: boolean }>(
    `SELECT txid, is_coinbase FROM transactions WHERE block_hash = $1 ORDER BY block_tx_index ASC`,
    [blockHash],
  );

  let burned = 0n;
  for (const tx of txs.rows) {
    const vins = await pool.query<{ prev_txid: string | null; prev_vout: number | null }>(
      `SELECT prev_txid, prev_vout FROM inputs WHERE txid = $1 ORDER BY vin_index ASC`,
      [tx.txid],
    );
    const vouts = await pool.query<{ asset_kind: string; value_atoms: string }>(
      `SELECT asset_kind, value_atoms::text AS value_atoms FROM outputs WHERE txid = $1 ORDER BY vout_index ASC`,
      [tx.txid],
    );
    const inputs: Array<IndexedPrevout | null> = [];
    for (const vin of vins.rows) {
      if (vin.prev_txid == null || vin.prev_vout == null) {
        inputs.push(null);
        continue;
      }
      const prev = await pool.query<{ asset_kind: string; value_atoms: string }>(
        `SELECT asset_kind, value_atoms::text AS value_atoms FROM outputs WHERE txid = $1 AND vout_index = $2`,
        [vin.prev_txid, vin.prev_vout],
      );
      const p = prev.rows[0];
      inputs.push(
        p
          ? { asset_kind: asAsset(p.asset_kind), value_atoms: BigInt(p.value_atoms) }
          : null,
      );
    }
    const outputs: IndexedOutput[] = vouts.rows.map((o) => ({
      asset_kind: asAsset(o.asset_kind),
      value_atoms: BigInt(o.value_atoms),
    }));
    burned += rfc018DestroyedFeeOru({
      isCoinbase: Boolean(tx.is_coinbase),
      isResMint: mintTxid != null && tx.txid === mintTxid,
      inputs,
      outputs,
    });
  }
  return burned;
}

export async function backfillMissingRfc018FeeBurn(pool: Pool, limit = 600): Promise<number> {
  const missing = await pool.query<{ height: string; block_hash: string }>(
    `
      SELECT b.height::text AS height, b.block_hash
      FROM blocks b
      LEFT JOIN rfc018_fee_burn_stats s ON s.height = b.height
      WHERE s.height IS NULL
      ORDER BY b.height ASC
      LIMIT $1
    `,
    [limit],
  );
  let n = 0;
  for (const row of missing.rows) {
    const burned = await computeRfc018BurnForStoredBlock(pool, row.block_hash);
    await upsertRfc018FeeBurnStats(pool, BigInt(row.height), row.block_hash, burned);
    n++;
  }
  if (n > 0) await recomputeRfc018CumulativePrefix(pool);
  return n;
}

/** Rebuild prefix sums after gap backfill. Reorg CASCADE already leaves a valid prefix. */
export async function recomputeRfc018CumulativePrefix(pool: Pool): Promise<void> {
  await pool.query(`
    WITH recomputed AS (
      SELECT height,
             SUM(burned_oru) OVER (ORDER BY height ASC ROWS BETWEEN UNBOUNDED PRECEDING AND CURRENT ROW) AS cum
      FROM rfc018_fee_burn_stats
    )
    UPDATE rfc018_fee_burn_stats s
    SET cumulative_burned_oru = r.cum
    FROM recomputed r
    WHERE s.height = r.height
  `);
}
