/**
 * Explorer-only primary-output selection. Does not change consensus or CTxOut.
 * Live Convergence is OP_CONVERGE (ORU burn). Same body tx MAY mint Witness v1 RES at destroy_cost(h).
 */

import { atomsToResDisplay } from "./amount";
import { formatOruAtoms } from "./fee";
import { isConvergenceScriptPubKey, OP_CONVERGE_SCRIPT_NAME, CONVERGENCE_OUTPUT_STATE } from "./script";

export const OP_CONVERGE_SCRIPT_LABEL = OP_CONVERGE_SCRIPT_NAME;
export const CONVERGENCE_LOCK_DESTINATION_LABEL = `${OP_CONVERGE_SCRIPT_NAME} (${CONVERGENCE_OUTPUT_STATE})`;

export type OutputRow = {
  vout_index: number;
  value_atoms: string | bigint | number;
  asset_kind: string;
  address?: string | null;
  witness_version?: number | string | null;
  script_pubkey?: string | null;
};

export type PrimaryOruRole = "convergence_lock" | "payment" | "output";

export type PrimaryOruPick = {
  output: OutputRow;
  role: PrimaryOruRole;
};

export function isConvergenceOutput(out: OutputRow): boolean {
  if (out.script_pubkey && isConvergenceScriptPubKey(out.script_pubkey)) return true;
  return false;
}

export function pickPrimaryOruOutput(vout: OutputRow[]): PrimaryOruPick | null {
  const oru = vout.filter((o) => o.asset_kind === "ORU");
  if (oru.length === 0) return null;

  const lock = oru.find((o) => isConvergenceOutput(o));
  if (lock) return { output: lock, role: "convergence_lock" };

  if (oru.length === 1) return { output: oru[0], role: "output" };

  const primary = oru.reduce((a, b) => (BigInt(a.value_atoms) >= BigInt(b.value_atoms) ? a : b));
  return { output: primary, role: "payment" };
}

export type TxKind = "coinbase" | "convergence_lock" | "oru_destroy" | "res_mint" | "res_transfer" | "oru_transfer" | "other";

export function classifyTxKind(args: {
  isCoinbase: boolean;
  primaryRole: PrimaryOruRole | null;
  hasOru: boolean;
  hasRes?: boolean;
  hasResInput?: boolean;
  isResMint?: boolean;
}): TxKind {
  if (args.isResMint || (args.primaryRole === "convergence_lock" && args.hasRes && !args.hasResInput)) return "res_mint";
  if (args.isCoinbase) return "coinbase";
  if (args.hasResInput && args.hasRes) return "res_transfer";
  if (args.primaryRole === "convergence_lock") return "oru_destroy";
  if (args.hasOru) return "oru_transfer";
  return "other";
}

export function txKindLabel(kind: TxKind): string {
  if (kind === "coinbase") return "Coinbase";
  if (kind === "oru_destroy") return "ORU destroyed";
  if (kind === "convergence_lock") return CONVERGENCE_LOCK_DESTINATION_LABEL;
  if (kind === "res_mint") return "RES mint";
  if (kind === "res_transfer") return "RES transfer";
  if (kind === "oru_transfer") return "ORU transfer";
  return "Other";
}

/** Tx-page notice. Empty when the kind does not need a mint/destroy distinction. */
export function txKindNotice(kind: TxKind): string | null {
  if (kind === "oru_destroy") return "ORU destroyed. No RES minted.";
  if (kind === "res_mint") return "ORU destroyed. RES minted.";
  return null;
}

export function convergeOutputNote(kind: TxKind): string {
  if (kind === "oru_destroy") return `${OP_CONVERGE_SCRIPT_NAME} · ORU destroyed · no RES minted`;
  return `${OP_CONVERGE_SCRIPT_NAME} / ${CONVERGENCE_OUTPUT_STATE}`;
}

export function amountRoleLabel(role: PrimaryOruRole | null): string {
  if (role === "convergence_lock") return CONVERGENCE_LOCK_DESTINATION_LABEL;
  if (role === "payment") return "Payment";
  if (role === "output") return "Output";
  return "Amount";
}

/** Summary-row header: OP_CONVERGE is an unspendable burn, not a payment receiver. */
export function destinationLabel(role: PrimaryOruRole | string | null | undefined): string {
  return role === "convergence_lock" ? CONVERGENCE_LOCK_DESTINATION_LABEL : "Receiver";
}

export type AssetMovement =
  | "coinbase"
  | "oru_only"
  | "res_transfer"
  | "res_and_oru"
  | "res_mint"
  | "res_destroyed"
  | "none";

export type BlockTxPresentation = {
  tx_kind: TxKind;
  tx_kind_label: string;
  amount_atoms: string | null;
  amount_display: string | null;
  amount_role: PrimaryOruRole | "output" | null;
  asset_kind: string | null;
  receiver: string | null;
  asset_movement: AssetMovement;
};

function positiveAtoms(value: string | bigint | number): bigint {
  const atoms = BigInt(value);
  return atoms > 0n ? atoms : 0n;
}

/**
 * Block-list presentation from the transaction's complete indexed movement.
 * RES output amounts are summed once. RES inputs select the class and are
 * not added to the displayed amount. An address alone is not RES movement.
 */
export function presentBlockTx(args: {
  isCoinbase: boolean;
  isResMint: boolean;
  outputs: OutputRow[];
  hasPositiveResInput: boolean;
}): BlockTxPresentation {
  const resOuts = args.outputs.filter((output) => output.asset_kind === "RES" && positiveAtoms(output.value_atoms) > 0n);
  const oruOuts = args.outputs.filter((output) => output.asset_kind === "ORU" && positiveAtoms(output.value_atoms) > 0n);
  const resAtoms = resOuts.reduce((sum, output) => sum + positiveAtoms(output.value_atoms), 0n);
  const hasRes = resOuts.length > 0;
  const hasOru = oruOuts.length > 0;
  const primary = pickPrimaryOruOutput(args.outputs);
  const kind = classifyTxKind({
    isCoinbase: args.isCoinbase,
    primaryRole: primary?.role ?? null,
    hasOru,
    hasRes,
    hasResInput: args.hasPositiveResInput,
    isResMint: args.isResMint,
  });
  const recipients = [
    ...new Set(resOuts.map((output) => output.address).filter((address): address is string => Boolean(address))),
  ];
  const resReceiver = recipients.length === 1 ? recipients[0] : null;
  const oruAmount = primary ? BigInt(primary.output.value_atoms) : null;

  if (kind === "res_mint") {
    return {
      tx_kind: kind,
      tx_kind_label: txKindLabel(kind),
      amount_atoms: resAtoms > 0n ? resAtoms.toString() : null,
      amount_display: resAtoms > 0n ? atomsToResDisplay(resAtoms) : null,
      amount_role: "output",
      asset_kind: hasRes ? "RES" : null,
      receiver: resReceiver,
      asset_movement: "res_mint",
    };
  }
  if (kind === "coinbase") {
    return {
      tx_kind: kind,
      tx_kind_label: txKindLabel(kind),
      amount_atoms: oruAmount == null ? null : oruAmount.toString(),
      amount_display: oruAmount == null ? null : formatOruAtoms(oruAmount),
      amount_role: primary?.role ?? null,
      asset_kind: primary ? "ORU" : null,
      receiver: primary?.output.address ?? null,
      asset_movement: "coinbase",
    };
  }
  if (kind === "res_transfer") {
    const both = hasOru;
    return {
      tx_kind: kind,
      tx_kind_label: both ? "RES transfer · ORU" : "RES transfer",
      amount_atoms: resAtoms.toString(),
      amount_display: atomsToResDisplay(resAtoms),
      amount_role: "output",
      asset_kind: both ? "RES, ORU" : "RES",
      receiver: resReceiver,
      asset_movement: both ? "res_and_oru" : "res_transfer",
    };
  }
  if (args.hasPositiveResInput && !hasRes) {
    return {
      tx_kind: kind,
      tx_kind_label: hasOru ? `${txKindLabel(kind)} · RES destroyed` : "RES destroyed",
      amount_atoms: hasOru && oruAmount != null ? oruAmount.toString() : null,
      amount_display: hasOru && oruAmount != null ? formatOruAtoms(oruAmount) : null,
      amount_role: hasOru ? primary?.role ?? null : null,
      asset_kind: hasOru ? "RES, ORU" : "RES",
      receiver: hasOru ? primary?.output.address ?? null : null,
      asset_movement: "res_destroyed",
    };
  }
  if (hasOru) {
    return {
      tx_kind: kind,
      tx_kind_label: txKindLabel(kind),
      amount_atoms: oruAmount == null ? null : oruAmount.toString(),
      amount_display: oruAmount == null ? null : formatOruAtoms(oruAmount),
      amount_role: primary?.role ?? null,
      asset_kind: "ORU",
      receiver: primary?.output.address ?? null,
      asset_movement: "oru_only",
    };
  }
  return {
    tx_kind: kind,
    tx_kind_label: txKindLabel(kind),
    amount_atoms: null,
    amount_display: null,
    amount_role: null,
    asset_kind: null,
    receiver: null,
    asset_movement: "none",
  };
}
