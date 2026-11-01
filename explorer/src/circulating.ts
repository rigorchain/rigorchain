/**
 * Explorer circulating-ORU accounting (presentation).
 * Does not change consensus. Node UTXO set already omits OP_CONVERGE;
 * this layer must not add those locked outputs back into circulating charts.
 *
 * RFC-018 fee burns never create an output, so they cannot appear here.
 *
 * Historical RFC-014 / retired Witness v1 CVG2 rows may still exist in old
 * indexer DBs until reindex; they are also excluded from circulating.
 */

import { isConvergenceScriptPubKey, isRetiredCvg2WitnessV1 } from "./script";

/** Display-only copy of consensus coinbase maturity. Not a consensus rule. */
export const COINBASE_MATURITY_DISPLAY = 100;

export type IndexedOruRow = {
  spent: boolean;
  script_pubkey: string;
  amount_atoms: bigint | string | number;
};

export function isCirculatingSpendableOruRow(row: IndexedOruRow): boolean {
  if (row.spent) return false;
  if (isConvergenceScriptPubKey(row.script_pubkey)) return false;
  if (isRetiredCvg2WitnessV1(row.script_pubkey)) return false;
  return true;
}

/** Sum of indexed unspent ORU that may be treated as circulating / spendable UTXO. */
export function circulatingOruAtoms(rows: IndexedOruRow[]): bigint {
  let sum = 0n;
  for (const row of rows) {
    if (!isCirculatingSpendableOruRow(row)) continue;
    sum += BigInt(String(row.amount_atoms));
  }
  return sum;
}

export function unlockedOruSql(alias?: string): string {
  const spent = alias ? `${alias}.spent` : "spent";
  const spk = alias ? `${alias}.script_pubkey` : "script_pubkey";
  return (
    `${spent} = FALSE` +
    ` AND NOT (${opConvergeScriptSql(spk)})` +
    ` AND LOWER(${spk}) NOT LIKE '512043564732%'`
  );
}

/**
 * SQL predicate: unspent, not live OP_CONVERGE, and not historical RFC-014 CVG2 v1.
 * OP_CONVERGE wire: 0x14 || 20 bytes || 0xbd (44 hex chars).
 */
export function opConvergeScriptSql(column = "script_pubkey"): string {
  return `LOWER(REPLACE(TRIM(${column}), ' ', '')) ~ '^14[0-9a-f]{40}bd$'`;
}

/**
 * True when this transaction minted RES.
 * Same predicate as a body mint: a res_mints row or a positive RES output.
 */
export function oruTxMintedResSql(txidExpr: string): string {
  return (
    `EXISTS (SELECT 1 FROM res_mints rm WHERE rm.mint_txid = ${txidExpr})` +
    ` OR EXISTS (SELECT 1 FROM outputs r WHERE r.txid = ${txidExpr} AND r.asset_kind = 'RES' AND r.value_atoms > 0)`
  );
}

/**
 * OP_CONVERGE ORU that did not mint RES.
 * Subset of Convergence destroyed — do not subtract from issued or circulating,
 * and do not add it again on top of Convergence destroyed.
 */
export function plainDestroyOruSql(outputAlias = "o"): string {
  const txid = `${outputAlias}.txid`;
  return (
    `${outputAlias}.asset_kind = 'ORU'` +
    ` AND ${opConvergeScriptSql(`${outputAlias}.script_pubkey`)}` +
    ` AND ${outputAlias}.value_atoms > 0` +
    ` AND NOT (${oruTxMintedResSql(txid)})`
  );
}

/** Unspent ORU excluding OP_CONVERGE and retired CVG2. Includes immature coinbase. */
export const CIRCULATING_ORU_SQL_PREDICATE = unlockedOruSql();

export const UNSPENT_UNLOCKED_ORU_SQL_PREDICATE = CIRCULATING_ORU_SQL_PREDICATE;

/**
 * Mature transferable ORU: unlocked unspent, and coinbase has ≥100 confirmations.
 * Requires aliases `u` (oru_utxos) and `t` (transactions).
 */
export function matureSpendableOruSql(
  tipHeightSql = "(SELECT COALESCE(MAX(height), 0) FROM blocks)",
): string {
  return (
    `${unlockedOruSql("u")}` +
    ` AND (COALESCE(t.is_coinbase, FALSE) = FALSE` +
    ` OR ((${tipHeightSql}) - u.height + 1) >= ${COINBASE_MATURITY_DISPLAY})`
  );
}
