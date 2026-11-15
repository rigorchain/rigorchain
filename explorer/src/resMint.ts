/**
 * Explorer mint adapter: RFC-020 issuance is a body transaction.
 * Same tx burns ORU (`OP_CONVERGE`) and creates Witness v1 RES.
 * A 0xbb template is not the live issuance path. The indexer classifies it
 * as INVALID, so this adapter does not treat it as a RES mint output.
 */

import { isConvergenceScriptPubKey } from "./script";
import { resDestroyCostForHeight } from "./resIssuanceSchedule";

export const RFC020_BODY_MINT_SOURCE = "rfc020_body_mint";
/** Legacy index rows written before the RFC-020 source rename. Not a live label. */
const LEGACY_BODY_MINT_SOURCE = "rfc019_body_mint";
/** @deprecated Pre-RFC-020 rows may still carry this source. Not live issuance. */
export const COINBASE_RES_MINT_SOURCE = "coinbase_op_res_mint";

export type MintVout = {
  vout_index: number;
  value_atoms: bigint;
  script_pubkey: string;
};

export type BodyResMint = {
  mintTxid: string;
  burnAtoms: bigint;
  outputs: MintVout[];
  amountAtoms: bigint;
};

export type BodyMintProofItems = {
  source: typeof RFC020_BODY_MINT_SOURCE;
  burn_atoms: string;
  mint_atoms: string;
  height: number;
  destroy_cost_atoms: string;
};

export type CoinbaseMintProofItems = {
  source: typeof COINBASE_RES_MINT_SOURCE;
  script_pubkey: string;
  paid_winner_txid: string;
};

function isResOutput(o: { asset_kind?: string; value_atoms: bigint }): boolean {
  return o.asset_kind === "RES" && o.value_atoms > 0n;
}

/**
 * Mirrors `TxOutputIsBodyResMint`: a non-coinbase tx with exactly one
 * positive `OP_CONVERGE` and one or more RES outputs.
 */
export function findBodyResMints(
  txs: Array<{
    txid: string;
    is_coinbase: boolean;
    vouts: Array<{
      vout_index: number;
      value_atoms: bigint;
      script_pubkey: string;
      asset_kind?: string;
    }>;
  }>,
): BodyResMint[] {
  const found: BodyResMint[] = [];
  for (const tx of txs) {
    if (tx.is_coinbase) continue;
    const burns = tx.vouts.filter((o) => isConvergenceScriptPubKey(o.script_pubkey) && o.value_atoms > 0n);
    if (burns.length !== 1) continue;
    const outputs = tx.vouts.filter(isResOutput);
    if (outputs.length === 0) continue;
    found.push({
      mintTxid: tx.txid,
      burnAtoms: burns[0].value_atoms,
      outputs,
      amountAtoms: outputs.reduce((acc, o) => acc + o.value_atoms, 0n),
    });
  }
  return found;
}

export function rfc020MintProofItems(args: {
  burnAtoms: bigint;
  mintAtoms: bigint;
  height: number;
}): BodyMintProofItems {
  return {
    source: RFC020_BODY_MINT_SOURCE,
    burn_atoms: args.burnAtoms.toString(),
    mint_atoms: args.mintAtoms.toString(),
    height: args.height,
    destroy_cost_atoms: resDestroyCostForHeight(args.height).toString(),
  };
}

export function isBodyMintProofSource(source: unknown): boolean {
  return source === RFC020_BODY_MINT_SOURCE || source === LEGACY_BODY_MINT_SOURCE;
}

export function isBodyMintProofItems(value: unknown): value is BodyMintProofItems {
  if (!value || typeof value !== "object") return false;
  return isBodyMintProofSource((value as { source?: unknown }).source);
}

export function isCoinbaseMintProofItems(value: unknown): value is CoinbaseMintProofItems {
  if (!value || typeof value !== "object") return false;
  return (value as { source?: unknown }).source === COINBASE_RES_MINT_SOURCE;
}
