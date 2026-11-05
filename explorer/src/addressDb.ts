import type { Pool } from "pg";
import type { Config } from "./config";
import { buildCompliancePayload } from "./compliance";
import {
  ADDRESS_READONLY_NOTICE,
  atomsToOruDisplay,
  atomsToResDisplay,
  buildAddressIntegrity,
  classifyAddressWithHint,
  addressMatchesExplorerHrp,
  isValidAddressFormat,
} from "./addressIntelligence";
import { getOrCreateChainState } from "./db";
import { rpc } from "./rpcCli";
import { matureSpendableOruSql, unlockedOruSql } from "./circulating";
import { atomsFromSql, reconcileIndexedAsset } from "./accounting";
import { isConvergenceScriptPubKey } from "./script";
import { addressResCounterparty } from "./resTransferHistory";
import { RES_OUTPUT_SPENT_SQL } from "./resTransferQuery";
import { CONVERGENCE_IS_MINT_SQL } from "./rfc020MintSql";

export type AddressIntelligence = Record<string, unknown>;

export async function getAddressIntelligence(
  pool: Pool,
  config: Config,
  address: string,
  page: number,
  limit: number,
): Promise<AddressIntelligence | null> {
  const addr = address.trim();
  if (!isValidAddressFormat(addr)) return null;
  if (!addressMatchesExplorerHrp(addr, config.bech32Hrp)) return null;

  const offset = (Math.max(1, page) - 1) * limit;

  const oruUtxos = await pool.query(
    `
      SELECT txid, vout_index, amount_atoms, height, spent, spending_txid, script_pubkey
      FROM oru_utxos
      WHERE address = $1
      ORDER BY height DESC, txid ASC, vout_index ASC
    `,
    [addr],
  );

  const resOutputs = await pool.query(
    `
      SELECT o.txid, o.vout_index, o.value_atoms, b.height, b.time,
             ${RES_OUTPUT_SPENT_SQL} AS spent
      FROM outputs o
      JOIN transactions t ON t.txid = o.txid
      JOIN blocks b ON b.block_hash = t.block_hash
      WHERE o.address = $1 AND o.asset_kind = 'RES'
      ORDER BY b.height DESC, o.txid ASC, o.vout_index ASC
    `,
    [addr],
  );

  const oruSummary = await pool.query(
    `
      SELECT
        COUNT(*)::int AS lifetime_count,
        COUNT(*) FILTER (WHERE ${unlockedOruSql("u")})::int AS utxo_count,
        COUNT(*) FILTER (WHERE ${matureSpendableOruSql()})::int AS spendable_count,
        COALESCE(SUM(u.amount_atoms), 0)::text AS total_received_atoms,
        COALESCE(SUM(u.amount_atoms) FILTER (WHERE u.spent = TRUE), 0)::text AS total_spent_atoms,
        COALESCE(SUM(u.amount_atoms) FILTER (WHERE ${unlockedOruSql("u")}), 0)::text AS balance_atoms,
        COALESCE(SUM(u.amount_atoms) FILTER (WHERE u.spent = FALSE AND NOT (${unlockedOruSql("u")})), 0)::text AS locked_atoms
      FROM oru_utxos u
      LEFT JOIN transactions t ON t.txid = u.txid
      WHERE u.address = $1
    `,
    [addr],
  );

  const resSummary = await pool.query(
    `
      WITH res_out AS (
        SELECT o.txid, o.vout_index, o.value_atoms,
               ${RES_OUTPUT_SPENT_SQL} AS spent
        FROM outputs o
        WHERE o.address = $1 AND o.asset_kind = 'RES'
      )
      SELECT
        COUNT(*)::int AS lifetime_count,
        COUNT(*) FILTER (WHERE NOT spent)::int AS utxo_count,
        COUNT(*) FILTER (WHERE NOT spent)::int AS spendable_count,
        COALESCE(SUM(value_atoms), 0)::text AS total_received_atoms,
        COALESCE(SUM(value_atoms) FILTER (WHERE spent), 0)::text AS total_spent_atoms,
        COALESCE(SUM(value_atoms) FILTER (WHERE NOT spent), 0)::text AS balance_atoms
      FROM res_out
    `,
    [addr],
  );

  const activity = await pool.query(
    `
      SELECT MIN(height) AS first_seen, MAX(height) AS latest_activity
      FROM (
        SELECT height FROM oru_utxos WHERE address = $1
        UNION ALL
        SELECT b.height
        FROM outputs o
        JOIN transactions t ON t.txid = o.txid
        JOIN blocks b ON b.block_hash = t.block_hash
        WHERE o.address = $1
        UNION ALL
        SELECT t.block_height
        FROM oru_utxos u
        JOIN transactions t ON t.txid = u.spending_txid
        WHERE u.address = $1 AND u.spending_txid IS NOT NULL
        UNION ALL
        SELECT t.block_height AS height
        FROM inputs i
        JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
        JOIN transactions t ON t.txid = i.txid
        JOIN blocks b ON b.block_hash = t.block_hash
        WHERE prev.address = $1 AND prev.asset_kind = 'RES' AND prev.value_atoms > 0
      ) heights
    `,
    [addr],
  );

  const historyCount = await pool.query(
    `
      SELECT COUNT(DISTINCT txid)::int AS total
      FROM (
        SELECT o.txid
        FROM outputs o
        WHERE o.address = $1
        UNION
        SELECT u.spending_txid AS txid
        FROM oru_utxos u
        WHERE u.address = $1 AND u.spent = TRUE AND u.spending_txid IS NOT NULL
        UNION
        SELECT i.txid
        FROM inputs i
        JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
        JOIN transactions t ON t.txid = i.txid
        JOIN blocks b ON b.block_hash = t.block_hash
        WHERE prev.address = $1 AND prev.asset_kind = 'RES' AND prev.value_atoms > 0
      ) txids
    `,
    [addr],
  );

  const history = await pool.query(
    `
      WITH txids AS (
        SELECT DISTINCT txid FROM (
          SELECT o.txid FROM outputs o WHERE o.address = $1
          UNION
          SELECT u.spending_txid FROM oru_utxos u
          WHERE u.address = $1 AND u.spent = TRUE AND u.spending_txid IS NOT NULL
          UNION
          SELECT i.txid
          FROM inputs i
          JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
          JOIN transactions st ON st.txid = i.txid
          JOIN blocks sb ON sb.block_hash = st.block_hash
          WHERE prev.address = $1 AND prev.asset_kind = 'RES' AND prev.value_atoms > 0
        ) all_tx
      ),
      recv AS (
        SELECT o.txid, o.asset_kind, SUM(o.value_atoms)::text AS received_atoms
        FROM outputs o
        WHERE o.address = $1
        GROUP BY o.txid, o.asset_kind
      ),
      oru_sent AS (
        SELECT u.spending_txid AS txid, SUM(u.amount_atoms)::text AS sent_atoms
        FROM oru_utxos u
        WHERE u.address = $1 AND u.spent = TRUE AND u.spending_txid IS NOT NULL
        GROUP BY u.spending_txid
      ),
      res_sent AS (
        SELECT i.txid, SUM(prev.value_atoms)::text AS sent_atoms
        FROM inputs i
        JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
        JOIN transactions st ON st.txid = i.txid
        JOIN blocks sb ON sb.block_hash = st.block_hash
        WHERE prev.address = $1 AND prev.asset_kind = 'RES' AND prev.value_atoms > 0
        GROUP BY i.txid
      )
      SELECT t.txid, t.block_height, b.time,
             COALESCE(r_oru.received_atoms, '0') AS oru_received_atoms,
             COALESCE(r_res.received_atoms, '0') AS res_received_atoms,
             COALESCE(os.sent_atoms, '0') AS oru_sent_atoms,
             COALESCE(rs.sent_atoms, '0') AS res_sent_atoms,
             (
               SELECT COUNT(DISTINCT prev.address) FILTER (WHERE prev.address IS NOT NULL)::int
               FROM inputs i
               JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
               WHERE i.txid = t.txid AND prev.asset_kind = 'RES' AND prev.value_atoms > 0
             ) AS res_sender_address_count,
             (
               SELECT BOOL_OR(prev.address IS NULL)
               FROM inputs i
               JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
               WHERE i.txid = t.txid AND prev.asset_kind = 'RES' AND prev.value_atoms > 0
             ) AS res_sender_missing,
             (
               SELECT MIN(prev.address)
               FROM inputs i
               JOIN outputs prev ON prev.txid = i.prev_txid AND prev.vout_index = i.prev_vout
               WHERE i.txid = t.txid AND prev.asset_kind = 'RES' AND prev.value_atoms > 0 AND prev.address IS NOT NULL
             ) AS res_sender_address,
             (
               SELECT COUNT(DISTINCT o.address) FILTER (WHERE o.address IS NOT NULL AND o.address <> $1)::int
               FROM outputs o
               WHERE o.txid = t.txid AND o.asset_kind = 'RES' AND o.value_atoms > 0
             ) AS res_other_recipient_count,
             (
               SELECT MIN(o.address)
               FROM outputs o
               WHERE o.txid = t.txid AND o.asset_kind = 'RES' AND o.value_atoms > 0
                 AND o.address IS NOT NULL AND o.address <> $1
             ) AS res_other_recipient
      FROM txids
      JOIN transactions t ON t.txid = txids.txid
      JOIN blocks b ON b.block_hash = t.block_hash
      LEFT JOIN recv r_oru ON r_oru.txid = t.txid AND r_oru.asset_kind = 'ORU'
      LEFT JOIN recv r_res ON r_res.txid = t.txid AND r_res.asset_kind = 'RES'
      LEFT JOIN oru_sent os ON os.txid = t.txid
      LEFT JOIN res_sent rs ON rs.txid = t.txid
      ORDER BY t.block_height DESC, t.txid ASC
      LIMIT $2 OFFSET $3
    `,
    [addr, limit, offset],
  );

  const convergence = await pool.query(
    `
      SELECT ce.convergence_txid,
             MIN(ce.block_height) AS block_height,
             BOOL_OR(${CONVERGENCE_IS_MINT_SQL}) AS is_winner
      FROM convergence_events ce
      JOIN blocks b ON b.block_hash = ce.block_hash
      WHERE ce.convergence_txid IN (
        SELECT o.txid FROM outputs o WHERE o.address = $1
        UNION
        SELECT u.spending_txid FROM oru_utxos u
        WHERE u.address = $1 AND u.spending_txid IS NOT NULL
      )
      GROUP BY ce.convergence_txid
      ORDER BY MIN(ce.block_height) DESC
    `,
    [addr],
  );

  const resMints = await pool.query(
    `
      SELECT rm.mint_txid, rm.mint_vout_index, rm.mint_amount, rm.block_height, b.block_hash
      FROM res_mints rm
      JOIN outputs o ON o.txid = rm.mint_txid AND o.vout_index = rm.mint_vout_index
      JOIN blocks b ON b.block_hash = rm.block_hash
      WHERE o.address = $1
      ORDER BY rm.block_height DESC
    `,
    [addr],
  );

  const chainState = await getOrCreateChainState(pool, config);
  const compliance = await buildCompliancePayload(config, chainState);
  let nodeHeight: number | null = null;
  try {
    const info = await rpc.getblockchaininfo(config);
    nodeHeight = Number(info?.blocks ?? -1);
  } catch {
    nodeHeight = null;
  }
  const indexerTipHeight = Number(chainState.last_height);

  const oru = oruSummary.rows[0] ?? {};
  const res = resSummary.rows[0] ?? {};
  const act = activity.rows[0] ?? {};

  const hasOru = Number(oru.lifetime_count ?? 0) > 0;
  const hasRes = Number(res.lifetime_count ?? 0) > 0;
  const hasActivity = hasOru || hasRes || convergence.rows.length > 0 || resMints.rows.length > 0;
  const { address_type, classification } = classifyAddressWithHint(hasOru, hasRes, hasActivity, addr);

  const winnerTxids = convergence.rows.filter((r) => r.is_winner).map((r) => r.convergence_txid);
  const candidateCount = convergence.rows.filter((r) => !r.is_winner).length;

  const historyItems = history.rows.flatMap((row) => {
    const items: Record<string, unknown>[] = [];
    const conf =
      row.block_height != null && indexerTipHeight >= 0
        ? Math.max(0, indexerTipHeight - Number(row.block_height) + 1)
        : null;
    const base = {
      txid: row.txid,
      block_height: Number(row.block_height),
      confirmations: conf,
      timestamp: Number(row.time),
    };
    if (BigInt(row.oru_received_atoms) > 0n || BigInt(row.oru_sent_atoms) > 0n) {
      items.push({
        ...base,
        asset_type: "ORU",
        received_amount: atomsToOruDisplay(row.oru_received_atoms),
        sent_amount: atomsToOruDisplay(row.oru_sent_atoms),
      });
    }
    if (BigInt(row.res_received_atoms) > 0n || BigInt(row.res_sent_atoms) > 0n) {
      const counterparty = addressResCounterparty({
        sentAtoms: BigInt(row.res_sent_atoms),
        receivedAtoms: BigInt(row.res_received_atoms),
        senderAddressCount: Number(row.res_sender_address_count ?? 0),
        senderAddress: row.res_sender_address ?? null,
        senderMissing: Boolean(row.res_sender_missing),
        otherRecipientCount: Number(row.res_other_recipient_count ?? 0),
        otherRecipient: row.res_other_recipient ?? null,
      });
      items.push({
        ...base,
        asset_type: "RES",
        received_amount: atomsToResDisplay(row.res_received_atoms),
        sent_amount: atomsToResDisplay(row.res_sent_atoms),
        counterparty_kind: counterparty.kind,
        counterparty_address: counterparty.kind === "address" ? counterparty.address : null,
        counterparty_label:
          counterparty.kind === "address"
            ? counterparty.address
            : counterparty.kind === "multiple"
              ? BigInt(row.res_sent_atoms) > 0n
                ? "Multiple outputs"
                : "Multiple inputs"
              : "Unavailable",
      });
    }
    if (items.length === 0) {
      items.push({
        ...base,
        asset_type: "NONE",
        received_amount: "0",
        sent_amount: "0",
      });
    }
    return items;
  });

  const resMintTotal = resMints.rows.reduce((acc, r) => acc + BigInt(String(r.mint_amount ?? 0)), 0n);

  const oruLedger = reconcileIndexedAsset({
    receivedAtoms: atomsFromSql(oru.total_received_atoms),
    spentAtoms: atomsFromSql(oru.total_spent_atoms),
    unspentAtoms: atomsFromSql(oru.balance_atoms),
    unspentCount: Number(oru.utxo_count ?? 0),
    spendableCount: Number(oru.spendable_count ?? 0),
    lockedAtoms: atomsFromSql(oru.locked_atoms),
  });
  const resLedger = reconcileIndexedAsset({
    receivedAtoms: atomsFromSql(res.total_received_atoms),
    spentAtoms: atomsFromSql(res.total_spent_atoms),
    unspentAtoms: atomsFromSql(res.balance_atoms),
    unspentCount: Number(res.utxo_count ?? 0),
    spendableCount: Number(res.spendable_count ?? 0),
  });
  if (!oruLedger.consistent || !resLedger.consistent) {
    console.error(
      JSON.stringify({
        level: "ERROR",
        component: "address-accounting",
        address: addr,
        indexer_height: indexerTipHeight,
        oru: oruLedger.errors,
        res: resLedger.errors,
      }),
    );
  }

  return {
    oru_utxos: oruUtxos.rows.filter(
      (r) => !r.spent && !isConvergenceScriptPubKey(String(r.script_pubkey ?? "")),
    ),
    res_outputs: resOutputs.rows.filter((r) => !r.spent),
    identity: {
      address: addr,
      address_type,
      classification,
      first_seen_block: act.first_seen != null ? Number(act.first_seen) : null,
      latest_activity_block: act.latest_activity != null ? Number(act.latest_activity) : null,
      indexer_height: indexerTipHeight,
    },
    balances: {
      oru: {
        atoms: oruLedger.currentBalanceAtoms.toString(),
        display: atomsToOruDisplay(oruLedger.currentBalanceAtoms.toString()),
      },
      res: {
        atoms: resLedger.currentBalanceAtoms.toString(),
        display: atomsToResDisplay(resLedger.currentBalanceAtoms.toString()),
      },
    },
    utxo_summary: {
      oru: {
        total_utxo_count: oruLedger.totalUtxoCount,
        spendable_utxo_count: oruLedger.spendableUtxoCount,
        total_received: atomsToOruDisplay(oruLedger.totalReceivedAtoms.toString()),
        total_spent: atomsToOruDisplay(oruLedger.totalSpentAtoms.toString()),
        current_balance: atomsToOruDisplay(oruLedger.currentBalanceAtoms.toString()),
      },
      res: {
        total_utxo_count: resLedger.totalUtxoCount,
        spendable_utxo_count: resLedger.spendableUtxoCount,
        total_received: atomsToResDisplay(resLedger.totalReceivedAtoms.toString()),
        total_spent: atomsToResDisplay(resLedger.totalSpentAtoms.toString()),
        current_balance: atomsToResDisplay(resLedger.currentBalanceAtoms.toString()),
      },
    },
    accounting: {
      indexer_height: indexerTipHeight,
      node_height: nodeHeight,
      oru_consistent: oruLedger.consistent,
      res_consistent: resLedger.consistent,
      oru_errors: oruLedger.errors,
      res_errors: resLedger.errors,
    },
    transaction_history: {
      page,
      limit,
      total: Number(historyCount.rows[0]?.total ?? 0),
      items: historyItems,
    },
    convergence_activity: {
      participated: convergence.rows.length > 0,
      convergence_transaction_count: convergence.rows.length,
      mint_count: winnerTxids.length,
      winner_count: winnerTxids.length,
      candidate_count: candidateCount,
      mint_txids: winnerTxids,
      winner_txids: winnerTxids,
    },
    res_mint_activity: {
      res_mint_count: resMints.rows.length,
      total_res_minted_atoms: resMintTotal.toString(),
      total_res_minted_display: atomsToResDisplay(resMintTotal.toString()),
      latest_mint_block: resMints.rows[0] ? Number(resMints.rows[0].block_height) : null,
      mint_transactions: resMints.rows.map((r) => ({
        mint_txid: r.mint_txid,
        block_height: Number(r.block_height),
        mint_amount_display: atomsToResDisplay(String(r.mint_amount ?? "0")),
      })),
    },
    integrity: buildAddressIntegrity(compliance, chainState, indexerTipHeight, nodeHeight),
    readonly_notice: ADDRESS_READONLY_NOTICE,
    equivalence_claimed: false,
  };
}
