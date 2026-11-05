/**
 * Canonical RES transfer rows from indexed inputs and outputs.
 * Reorg deletion of blocks removes the joined transactions. No new table.
 */

import type { Pool } from "pg";
import type { Config } from "./config";
import { atomsToResDisplay } from "./amount";
import { blockListPageMeta, type BlockListQuery } from "./blocksList";
import { formatDerivedMaxSupply } from "./resIssuanceSchedule";
import type { SenderState } from "./resTransferHistory";

export const RES_OUTPUT_SPENT_SQL = `
  EXISTS (
    SELECT 1
    FROM inputs i
    JOIN transactions st ON st.txid = i.txid
    JOIN blocks sb ON sb.block_hash = st.block_hash
    WHERE i.prev_txid = o.txid
      AND i.prev_vout = o.vout_index
  )
`;

/**
 * Canonical RES outputs that also spend RES. The sender aggregate is applied
 * only to the page of txids, not to every historical RES input.
 */
const TRANSFER_FROM = `
  FROM transactions t
  JOIN blocks b ON b.block_hash = t.block_hash
  JOIN outputs o ON o.txid = t.txid AND o.asset_kind = 'RES' AND o.value_atoms > 0
  WHERE NOT t.is_coinbase
    AND NOT EXISTS (SELECT 1 FROM res_mints rm WHERE rm.mint_txid = t.txid)
    AND EXISTS (
      SELECT 1
      FROM inputs i
      JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
      WHERE i.txid = t.txid
        AND prev.asset_kind = 'RES'
        AND prev.value_atoms > 0
    )
`;

export function resTransferListSql(): string {
  return `
    WITH page AS (
      SELECT b.height::text AS height,
             b.block_hash,
             b.time::text AS time,
             t.block_tx_index::text AS tx_index,
             t.txid,
             o.vout_index::text AS vout,
             o.value_atoms::text AS amount_atoms,
             o.address AS recipient
      ${TRANSFER_FROM}
      ORDER BY b.height DESC, t.block_tx_index DESC, o.vout_index ASC, t.txid ASC
      LIMIT $1 OFFSET $2
    )
    SELECT page.height,
           page.block_hash,
           page.time,
           page.tx_index,
           page.txid,
           page.vout,
           page.amount_atoms,
           page.recipient,
           senders.sender_addresses::text AS sender_addresses,
           senders.sender_missing,
           senders.sender_address
    FROM page
    JOIN (
      SELECT i.txid,
             COUNT(DISTINCT prev.address) FILTER (
               WHERE prev.asset_kind = 'RES' AND prev.value_atoms > 0 AND prev.address IS NOT NULL
             )::int AS sender_addresses,
             BOOL_OR(prev.asset_kind = 'RES' AND prev.value_atoms > 0 AND prev.address IS NULL) AS sender_missing,
             MIN(prev.address) FILTER (
               WHERE prev.asset_kind = 'RES' AND prev.value_atoms > 0 AND prev.address IS NOT NULL
             ) AS sender_address
      FROM inputs i
      JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
      WHERE i.txid IN (SELECT txid FROM page)
        AND prev.asset_kind = 'RES'
        AND prev.value_atoms > 0
      GROUP BY i.txid
    ) senders ON senders.txid = page.txid
    ORDER BY page.height DESC, page.tx_index DESC, page.vout ASC, page.txid ASC
  `;
}

function senderState(addresses: number, missing: boolean, address: string | null): SenderState {
  if (addresses > 1 || (addresses === 1 && missing)) return { kind: "multiple" };
  if (addresses === 1 && address) return { kind: "address", address };
  return { kind: "unavailable" };
}

export async function listResTransferPage(pool: Pool, config: Config, query: BlockListQuery) {
  const [total, minted, tip] = await Promise.all([
    pool.query<{ n: number }>(`SELECT COUNT(*)::int AS n ${TRANSFER_FROM}`),
    pool.query<{ atoms: string }>(
      `SELECT COALESCE(SUM(rm.mint_amount), 0)::text AS atoms
       FROM res_mints rm
       JOIN blocks b ON b.block_hash = rm.block_hash`,
    ),
    pool.query<{ height: string; block_hash: string }>(
      `SELECT height::text AS height, block_hash FROM blocks ORDER BY height DESC LIMIT 1`,
    ),
  ]);
  const page = blockListPageMeta(query, Number(total.rows[0]?.n ?? 0));
  const rows = await pool.query(resTransferListSql(), [page.limit, page.offset]);
  const transfers = rows.rows.map((row) => {
    const sender = senderState(Number(row.sender_addresses ?? 0), Boolean(row.sender_missing), row.sender_address ?? null);
    return {
      height: Number(row.height),
      block_hash: String(row.block_hash),
      time: row.time == null ? null : Number(row.time),
      tx_index: Number(row.tx_index),
      txid: String(row.txid),
      vout: Number(row.vout),
      amount_atoms: String(row.amount_atoms),
      amount_display: atomsToResDisplay(String(row.amount_atoms)),
      sender_kind: sender.kind,
      sender_address: sender.kind === "address" ? sender.address : null,
      recipient: row.recipient == null ? null : String(row.recipient),
    };
  });
  return {
    snapshot: {
      indexer_height: tip.rows[0] ? Number(tip.rows[0].height) : null,
      indexer_block_hash: tip.rows[0]?.block_hash ?? null,
    },
    overview: {
      asset: "RES",
      network: config.network,
      max_supply_display: formatDerivedMaxSupply(),
      minted_atoms: minted.rows[0]?.atoms ?? "0",
      minted_display: atomsToResDisplay(minted.rows[0]?.atoms ?? "0"),
    },
    transfers,
    page: page.page,
    limit: page.limit,
    offset: page.offset,
    total: page.total,
    pages: page.pages,
    has_prev: page.has_prev,
    has_next: page.has_next,
  };
}
