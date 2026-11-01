/**
 * Explorer fee display only. Does not change the consensus fee model.
 * The displayed fee is the ORU leg: ORU inputs minus ORU outputs.
 * RES amounts are not part of that leg. RFC-018 destruction is a separate
 * floor share of this fee and is not subtracted here.
 * Coinbase displays 0. A missing prevout stays N/A rather than a guessed fee.
 */

export type FeeKind = "coinbase" | "oru" | "na";

export type FeeView = {
  fee_atoms: string | null;
  fee_display: string;
  fee_kind: FeeKind;
};

const ORU_DECIMALS = 8n;
const ORU_BASE = 100000000n;

export function formatOruAtoms(atoms: bigint): string {
  const neg = atoms < 0n;
  const a = neg ? -atoms : atoms;
  const int = a / ORU_BASE;
  const frac = a % ORU_BASE;
  return `${neg ? "-" : ""}${int}.${frac.toString().padStart(Number(ORU_DECIMALS), "0")}`;
}

export function computeTxFee(args: {
  isCoinbase: boolean;
  /** Sum of spent ORU input values. null if any prevout is missing. */
  oruInputAtoms: bigint | null;
  oruOutputAtoms: bigint;
  /** Retained for callers. RES movement does not replace or suppress the ORU leg. */
  involvesRes: boolean;
}): FeeView {
  void args.involvesRes;
  if (args.isCoinbase) {
    return { fee_atoms: "0", fee_display: "0", fee_kind: "coinbase" };
  }
  if (args.oruInputAtoms === null) {
    return { fee_atoms: null, fee_display: "N/A", fee_kind: "na" };
  }
  const fee = args.oruInputAtoms - args.oruOutputAtoms;
  return {
    fee_atoms: fee.toString(),
    fee_display: formatOruAtoms(fee),
    fee_kind: "oru",
  };
}
