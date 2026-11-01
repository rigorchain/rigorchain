/**
 * Read-only aggregates over the canonical indexer tables.
 * Reorg deletion is the existing blocks FK cascade. This query does not
 * create a lifetime counter and does not scan a rolling window.
 */

import { opConvergeScriptSql, oruTxMintedResSql } from "./circulating";

const CONVERGE_OUTPUT = `
  o.asset_kind = 'ORU'
  AND ${opConvergeScriptSql("o.script_pubkey")}
  AND o.value_atoms > 0
`;

export function oruEconomicsAggregateSql(): string {
  const mintLinked = oruTxMintedResSql("o.txid");
  return `
    WITH tip AS (
      SELECT height, block_hash, cumulative_convergence
      FROM blocks
      ORDER BY height DESC
      LIMIT 1
    )
    SELECT
      (SELECT height::text FROM tip) AS indexer_height,
      (SELECT block_hash FROM tip) AS indexer_block_hash,
      (SELECT cumulative_convergence FROM tip) AS tip_cumulative_convergence,
      (
        SELECT COALESCE(SUM(o.value_atoms), 0)::text
        FROM outputs o
        JOIN transactions t ON t.txid = o.txid
        JOIN blocks b ON b.block_hash = t.block_hash
        WHERE ${CONVERGE_OUTPUT}
      ) AS convergence_atoms,
      (
        SELECT COALESCE(SUM(o.value_atoms) FILTER (WHERE NOT (${mintLinked})), 0)::text
        FROM outputs o
        JOIN transactions t ON t.txid = o.txid
        JOIN blocks b ON b.block_hash = t.block_hash
        WHERE ${CONVERGE_OUTPUT}
      ) AS plain_atoms,
      (
        SELECT COALESCE(SUM(o.value_atoms) FILTER (WHERE ${mintLinked}), 0)::text
        FROM outputs o
        JOIN transactions t ON t.txid = o.txid
        JOIN blocks b ON b.block_hash = t.block_hash
        WHERE ${CONVERGE_OUTPUT}
      ) AS attributable_oru_atoms,
      (
        SELECT COUNT(DISTINCT o.txid)::int
        FROM outputs o
        JOIN transactions t ON t.txid = o.txid
        JOIN blocks b ON b.block_hash = t.block_hash
        WHERE ${CONVERGE_OUTPUT}
      ) AS event_count,
      (
        SELECT COALESCE(SUM(rm.mint_amount), 0)::text
        FROM res_mints rm
        JOIN blocks b ON b.block_hash = rm.block_hash
      ) AS total_res_atoms,
      (
        SELECT COALESCE(SUM(rm.mint_amount), 0)::text
        FROM res_mints rm
        JOIN blocks b ON b.block_hash = rm.block_hash
        WHERE EXISTS (
          SELECT 1 FROM outputs o
          WHERE o.txid = rm.mint_txid
            AND ${CONVERGE_OUTPUT}
        )
      ) AS attributable_res_atoms,
      (
        SELECT CASE
          WHEN EXISTS (
            SELECT 1
            FROM blocks b
            WHERE NOT EXISTS (
              SELECT 1
              FROM rfc018_fee_burn_stats s
              WHERE s.height = b.height
                AND s.block_hash = b.block_hash
            )
          ) THEN NULL
          ELSE (
            SELECT COALESCE(SUM(s.burned_oru), 0)::text
            FROM rfc018_fee_burn_stats s
            JOIN blocks b ON b.height = s.height AND b.block_hash = s.block_hash
          )
        END
      ) AS fee_atoms,
      (
        SELECT MAX(rm.block_height)::text
        FROM res_mints rm
        JOIN blocks b ON b.block_hash = rm.block_hash
      ) AS last_mint_height
  `;
}

export function oruEconomicsRecentSql(): string {
  return `
    SELECT
      t.block_height::text AS height,
      t.txid,
      t.block_hash,
      COALESCE(SUM(o.value_atoms), 0)::text AS burn_atoms,
      COALESCE((
        SELECT SUM(rm.mint_amount)::text
        FROM res_mints rm
        WHERE rm.mint_txid = t.txid
      ), '0') AS res_atoms
    FROM transactions t
    JOIN outputs o ON o.txid = t.txid
    JOIN blocks b ON b.block_hash = t.block_hash
    WHERE ${CONVERGE_OUTPUT}
    GROUP BY t.block_height, t.txid, t.block_hash
    ORDER BY t.block_height DESC, t.txid ASC
    LIMIT 15
  `;
}
