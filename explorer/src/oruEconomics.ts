/**
 * Indexer-side ORU / RES cumulative presentation.
 * Does not compute consensus and does not write a protocol counter.
 *
 * Issued ORU is the RFC-020 closed form used by ComputeDerivedOruSubsidyCumulative:
 * height 0 pays 0; every later block pays 128 ORU. That is subsidy issuance,
 * not the coinbase total and not circulating supply.
 *
 * Convergence destroyed ORU is positive OP_CONVERGE output value. Plain ORU
 * destroyed is the subset of those outputs on transactions that did not mint
 * RES. RFC-018 fee burn is a separate sink and is not an OP_CONVERGE output.
 */

import { ORU_COIN, atomsToOruDisplay, atomsToResDisplay, formatFixedAtoms } from "./amount";
import {
  RES_MAX_SUPPLY,
  formatRateOruPerRes,
  resDestroyCostForHeight,
} from "./resIssuanceSchedule";

export const UNAVAILABLE = "Unavailable";
export const ORU_FLAT_SUBSIDY_ATOMS = 128n * ORU_COIN;

export const CONVERGENCE_EXPLANATION =
  "Convergence is the protocol mechanism linking ORU destruction with RES generation. The statistics above represent cumulative confirmed mainnet activity.";

export const HISTORICAL_RATIO_NOTE =
  "Historical Effective Conversion Ratio is the cumulative average of ORU destroyed by confirmed RES-minting Convergence transactions, divided by the RES those transactions generated. It is not the current display destroy_cost, and it does not guarantee a future conversion rate.";

export const CURRENT_CONVERSION_NOTE =
  "Current / Latest Mint Conversion Information is the protocol display destroy_cost at the confirming height: the ORU figure for minting 1 RES if a mint confirms at that height. It is not a lifetime average and not a market price.";

export const PLAIN_SUBSET_NOTE =
  "Plain ORU Destroyed is OP_CONVERGE value on transactions that did not mint RES. It is already included in Convergence ORU Destroyed.";

export const FEE_SEPARATE_NOTE =
  "Transaction Fees Destroyed is the RFC-018 floor share of ORU fees on RES transfers. It is not an OP_CONVERGE output and is not included in Convergence ORU Destroyed.";

export type CanonicalDestroyEvent = {
  txid: string;
  height: number;
  confirmed: boolean;
  rejected: boolean;
  /** Positive OP_CONVERGE value on this transaction. */
  convergenceAtoms: bigint;
  /** RES minted by this same transaction. Zero for a plain destroy. */
  resAtoms: bigint;
  /** RFC-018 fee burn attributed to this transaction. Not part of convergenceAtoms. */
  feeAtoms: bigint;
};

export type CanonicalFold = {
  issuedAtoms: bigint;
  convergenceAtoms: bigint;
  plainAtoms: bigint;
  attributableOruAtoms: bigint;
  attributableResAtoms: bigint;
  totalResAtoms: bigint;
  feeAtoms: bigint;
  eventCount: number;
  mintEventCount: number;
};

export type MoneyMetric = {
  atoms: string | null;
  display: string;
  available: boolean;
};

export type OruRecentEvent = {
  height: number;
  txid: string;
  blockHash: string;
  burnAtoms: bigint;
  resAtoms: bigint;
};

export type OruEconomicsInput = {
  indexerHeight: number | null;
  indexerBlockHash: string | null;
  nodeHeight: number | null;
  nodeBestBlockHash: string | null;
  confirmHeight: number | null;
  nodeIssuedAtoms: bigint | null;
  nodeConvergenceAtoms: bigint | null;
  /** Node cumulative_convergence stored on the indexed tip block, in atoms. */
  headerConvergenceAtoms: bigint | null;
  indexedConvergenceAtoms: bigint;
  plainAtoms: bigint;
  attributableOruAtoms: bigint;
  attributableResAtoms: bigint;
  totalResAtoms: bigint;
  /** Null when the fee prefix is missing at the indexed tip. Zero is a real zero. */
  feeAtoms: bigint | null;
  eventCount: number;
  lastMintHeight: number | null;
  recent: OruRecentEvent[];
};

export type OruEconomicsView = {
  title: string;
  subtitle: string;
  explanation: string;
  generated_from: "indexed canonical chain";
  snapshot: {
    indexer_height: number | null;
    indexer_block_hash: string | null;
    node_height: number | null;
    node_best_block_hash: string | null;
    lag_blocks: number | null;
    confirm_height: number | null;
    heights_match: boolean;
    tip_hash_match: boolean | null;
    header_convergence_matches_index: boolean | null;
    freshness: string;
    lag_note: string | null;
    accounting_note: string | null;
  };
  metrics: {
    total_issued: MoneyMetric;
    total_destroyed: MoneyMetric;
    convergence_destroyed: MoneyMetric;
    plain_destroyed: MoneyMetric;
    fees_destroyed: MoneyMetric;
    convergence_destruction_ratio: { display: string; available: boolean };
    total_destruction_ratio: { display: string; available: boolean };
    total_res_minted: MoneyMetric;
    res_maximum_supply: MoneyMetric;
    remaining_res: MoneyMetric;
    res_issuance_progress: { display: string; available: boolean };
    convergence_events: { count: number | null; display: string };
    attributable_oru: MoneyMetric;
    attributable_res: MoneyMetric;
    unattributed_res: MoneyMetric;
    historical_ratio: { display: string; available: boolean };
    res_per_1000_oru: { display: string; available: boolean };
    inclusive_convergence_ratio: { display: string; available: boolean; shown: boolean };
    current_conversion: { display: string; available: boolean; confirm_height: number | null };
    last_mint_height: number | null;
    partition_consistent: boolean;
    homepage_issued_matches: boolean | null;
    homepage_convergence_matches: boolean | null;
    homepage_res_scope: string;
  };
  sections: {
    destruction: PresentationSection;
    res: PresentationSection;
    relationship: PresentationSection;
  };
  recent: Array<{
    height: number;
    txid: string;
    block_hash: string;
    oru_destroyed_display: string;
    res_minted_display: string;
  }>;
  notes: {
    historical_ratio: string;
    current_conversion: string;
    plain_destroy: string;
    fees: string;
  };
};

export type PresentationRow = {
  label: string;
  value: string;
  note?: string;
};

export type PresentationSection = {
  id: string;
  title: string;
  rows: PresentationRow[];
  groups?: Array<{ title: string; rows: PresentationRow[] }>;
};

function nonNeg(v: bigint): bigint | null {
  if (v < 0n) return null;
  return v;
}

export function derivedOruSubsidyCumulativeAtoms(height: number): bigint | null {
  if (!Number.isInteger(height) || height < 0) return null;
  if (height === 0) return 0n;
  return BigInt(height) * ORU_FLAT_SUBSIDY_ATOMS;
}

/** Round-half-up of numerator/denominator to `decimals` fractional digits, as fixed-point atoms. */
export function roundedRatioAtoms(numerator: bigint, denominator: bigint, decimals = 8): bigint | null {
  if (denominator <= 0n || numerator < 0n) return null;
  const scale = 10n ** BigInt(decimals);
  return (numerator * scale + denominator / 2n) / denominator;
}

export function formatOruPerRes(burnAtoms: bigint, resAtoms: bigint): string | null {
  const atoms = roundedRatioAtoms(burnAtoms, resAtoms, 8);
  if (atoms == null) return null;
  return `${formatFixedAtoms(atoms, 8)} ORU per 1 RES`;
}

export function formatResPerThousandOru(resAtoms: bigint, burnAtoms: bigint): string | null {
  if (burnAtoms <= 0n || resAtoms < 0n) return null;
  const atoms = roundedRatioAtoms(resAtoms * 1000n, burnAtoms, 8);
  if (atoms == null) return null;
  return `${formatFixedAtoms(atoms, 8)} RES per 1,000 ORU`;
}

export function formatPercent(part: bigint, whole: bigint): string | null {
  if (whole <= 0n || part < 0n) return null;
  const atoms = roundedRatioAtoms(part * 100n, whole, 8);
  if (atoms == null) return null;
  return `${formatFixedAtoms(atoms, 8)}%`;
}

export function moneyMetric(atoms: bigint | null, kind: "ORU" | "RES"): MoneyMetric {
  if (atoms == null || atoms < 0n) {
    return { atoms: null, display: UNAVAILABLE, available: false };
  }
  const display = kind === "ORU" ? atomsToOruDisplay(atoms) : atomsToResDisplay(atoms);
  return { atoms: atoms.toString(), display, available: true };
}

export function remainingResAtoms(minted: bigint, cap: bigint = RES_MAX_SUPPLY): bigint {
  if (minted <= 0n) return cap;
  if (minted >= cap) return 0n;
  return cap - minted;
}

/**
 * Confirmed canonical events only. Rejected and unconfirmed rows are ignored.
 * A repeated txid is counted once. Heights above `tipHeight` are not yet canonical.
 */
export function foldCanonicalEvents(events: CanonicalDestroyEvent[], tipHeight: number): CanonicalFold {
  const seen = new Set<string>();
  let convergenceAtoms = 0n;
  let plainAtoms = 0n;
  let attributableOruAtoms = 0n;
  let attributableResAtoms = 0n;
  let totalResAtoms = 0n;
  let feeAtoms = 0n;
  let eventCount = 0;
  let mintEventCount = 0;
  for (const event of events) {
    if (!event.confirmed || event.rejected) continue;
    if (!Number.isInteger(event.height) || event.height < 0 || event.height > tipHeight) continue;
    if (seen.has(event.txid)) continue;
    seen.add(event.txid);
    const burn = event.convergenceAtoms > 0n ? event.convergenceAtoms : 0n;
    const res = event.resAtoms > 0n ? event.resAtoms : 0n;
    const fee = event.feeAtoms > 0n ? event.feeAtoms : 0n;
    if (burn > 0n) {
      eventCount += 1;
      convergenceAtoms += burn;
      if (res > 0n) {
        attributableOruAtoms += burn;
        mintEventCount += 1;
      } else {
        plainAtoms += burn;
      }
    }
    if (res > 0n) {
      totalResAtoms += res;
      if (burn > 0n) attributableResAtoms += res;
    }
    feeAtoms += fee;
  }
  const issued = derivedOruSubsidyCumulativeAtoms(tipHeight) ?? 0n;
  return {
    issuedAtoms: issued,
    convergenceAtoms,
    plainAtoms,
    attributableOruAtoms,
    attributableResAtoms,
    totalResAtoms,
    feeAtoms,
    eventCount,
    mintEventCount,
  };
}

/** Drop blocks at and above the fork reconnect height, then fold the prefix. */
export function foldAfterReorg(events: CanonicalDestroyEvent[], disconnectFromHeight: number): CanonicalFold {
  const kept = events.filter((event) => event.height < disconnectFromHeight);
  const tip = kept.reduce((max, event) => Math.max(max, event.height), -1);
  return foldCanonicalEvents(kept, tip < 0 ? -1 : tip);
}

function shortHash(hash: string | null): string {
  if (!hash) return UNAVAILABLE;
  if (hash.length <= 20) return hash;
  return `${hash.slice(0, 12)}…${hash.slice(-6)}`;
}

function sameSnapshotMatch(heightsMatch: boolean, nodeAtoms: bigint | null, indexedAtoms: bigint | null): boolean | null {
  if (!heightsMatch || nodeAtoms == null || indexedAtoms == null) return null;
  return nodeAtoms === indexedAtoms;
}

function freshnessLine(input: OruEconomicsInput, lag: number | null): string {
  const indexed = input.indexerHeight == null ? UNAVAILABLE : String(input.indexerHeight);
  const node = input.nodeHeight == null ? UNAVAILABLE : String(input.nodeHeight);
  const lagText = lag == null ? UNAVAILABLE : `${lag} block${lag === 1 ? "" : "s"}`;
  const confirm = input.confirmHeight == null ? UNAVAILABLE : String(input.confirmHeight);
  return `Indexed height ${indexed} · Indexed tip ${shortHash(input.indexerBlockHash)} · Node height ${node} · Indexer lag ${lagText} · Confirming height ${confirm}`;
}

export function buildOruEconomics(input: OruEconomicsInput): OruEconomicsView {
  const indexerHeight = input.indexerHeight;
  const issuedAtoms = indexerHeight == null ? null : derivedOruSubsidyCumulativeAtoms(indexerHeight);
  const convergenceAtoms = nonNeg(input.indexedConvergenceAtoms);
  const plainAtoms = nonNeg(input.plainAtoms);
  const attributableOru = nonNeg(input.attributableOruAtoms);
  const attributableRes = nonNeg(input.attributableResAtoms);
  const totalRes = nonNeg(input.totalResAtoms);
  const feeAtoms = input.feeAtoms == null ? null : nonNeg(input.feeAtoms);

  const partitionConsistent =
    convergenceAtoms != null &&
    plainAtoms != null &&
    attributableOru != null &&
    plainAtoms + attributableOru === convergenceAtoms;

  const unattributedRes =
    totalRes != null && attributableRes != null && totalRes >= attributableRes ? totalRes - attributableRes : null;

  const totalDestroyed =
    partitionConsistent && convergenceAtoms != null && feeAtoms != null ? convergenceAtoms + feeAtoms : null;

  const historicalAvailable =
    partitionConsistent && attributableOru != null && attributableRes != null && attributableRes > 0n;
  const historicalDisplay = historicalAvailable
    ? formatOruPerRes(attributableOru, attributableRes)
    : null;
  const perThousand = historicalAvailable ? formatResPerThousandOru(attributableRes, attributableOru) : null;

  const inclusiveDiffers =
    historicalAvailable &&
    convergenceAtoms != null &&
    (plainAtoms !== 0n || (unattributedRes != null && unattributedRes !== 0n));
  const inclusiveDisplay =
    inclusiveDiffers && convergenceAtoms != null && attributableRes != null
      ? formatOruPerRes(convergenceAtoms, attributableRes)
      : null;

  const currentAtoms =
    input.confirmHeight != null && input.confirmHeight >= 0 ? resDestroyCostForHeight(input.confirmHeight) : 0n;
  const currentDisplay = input.confirmHeight != null && currentAtoms > 0n ? formatRateOruPerRes(currentAtoms) : null;

  const heightsMatch =
    input.indexerHeight != null && input.nodeHeight != null && input.indexerHeight === input.nodeHeight;
  const tipHashMatch =
    input.indexerBlockHash && input.nodeBestBlockHash
      ? input.indexerBlockHash.toLowerCase() === input.nodeBestBlockHash.toLowerCase()
      : null;
  const lag =
    input.nodeHeight != null && input.indexerHeight != null
      ? Math.max(0, input.nodeHeight - input.indexerHeight)
      : null;

  const issuedMetric = moneyMetric(issuedAtoms, "ORU");
  const convergenceMetric = moneyMetric(partitionConsistent ? convergenceAtoms : convergenceAtoms, "ORU");
  const plainMetric = moneyMetric(plainAtoms, "ORU");
  const feeMetric = moneyMetric(feeAtoms, "ORU");
  const totalDestroyedMetric = moneyMetric(totalDestroyed, "ORU");
  const resMetric = moneyMetric(totalRes, "RES");
  const capMetric = moneyMetric(RES_MAX_SUPPLY, "RES");
  const remainingMetric = moneyMetric(totalRes == null ? null : remainingResAtoms(totalRes), "RES");
  const attributableOruMetric = moneyMetric(partitionConsistent ? attributableOru : null, "ORU");
  const attributableResMetric = moneyMetric(attributableRes, "RES");
  const unattributedMetric = moneyMetric(unattributedRes, "RES");

  const convergenceRatio =
    issuedAtoms != null && issuedAtoms > 0n && convergenceAtoms != null
      ? formatPercent(convergenceAtoms, issuedAtoms)
      : null;
  const totalRatio =
    issuedAtoms != null && issuedAtoms > 0n && totalDestroyed != null ? formatPercent(totalDestroyed, issuedAtoms) : null;
  const progress =
    totalRes != null ? formatPercent(totalRes, RES_MAX_SUPPLY) : null;

  const homepageIssuedMatches = sameSnapshotMatch(heightsMatch, input.nodeIssuedAtoms, issuedAtoms);
  const homepageConvergenceMatches = sameSnapshotMatch(heightsMatch, input.nodeConvergenceAtoms, convergenceAtoms);

  const headerMatch =
    input.headerConvergenceAtoms == null || convergenceAtoms == null
      ? null
      : input.headerConvergenceAtoms === convergenceAtoms;
  const lagNote =
    lag != null && lag > 0
      ? `Indexer is ${lag} block${lag === 1 ? "" : "s"} behind the node. Cumulative figures on this page use the indexed height. Homepage Issued ORU and Convergence destroyed ORU follow the node tip.`
      : tipHashMatch === false
        ? "Indexed tip hash differs from the node best block. Cumulative figures on this page use the indexed chain."
        : null;
  const accountingNote =
    headerMatch === false
      ? "The indexed OP_CONVERGE sum does not equal blocks.cumulative_convergence at the indexed tip. Category totals use indexed outputs and are not independently verified against the node prefix."
      : null;

  const plainNote =
    plainAtoms === 0n
      ? `${PLAIN_SUBSET_NOTE} The indexed plain total is zero, so every indexed Convergence ORU output is on a RES-minting transaction.`
      : PLAIN_SUBSET_NOTE;

  const destructionRows: PresentationRow[] = [
    { label: "Total ORU Issued", value: issuedMetric.display, note: "Cumulative block subsidy. Not circulating supply." },
    {
      label: "Total ORU Destroyed",
      value: totalDestroyedMetric.display,
      note: "Convergence ORU Destroyed plus Transaction Fees Destroyed. Each category is counted once.",
    },
    { label: "Convergence ORU Destroyed", value: convergenceMetric.display, note: "All confirmed positive OP_CONVERGE outputs." },
    { label: "Plain ORU Destroyed", value: plainMetric.display, note: plainNote },
    { label: "Transaction Fees Destroyed", value: feeMetric.display, note: FEE_SEPARATE_NOTE },
    { label: "Convergence Destruction Ratio", value: convergenceRatio ?? UNAVAILABLE, note: "Convergence ORU Destroyed / Total ORU Issued." },
    { label: "Total ORU Destruction Ratio", value: totalRatio ?? UNAVAILABLE, note: "Total ORU Destroyed / Total ORU Issued." },
  ];

  const resRows: PresentationRow[] = [
    { label: "Total RES Minted", value: resMetric.display, note: "Sum of confirmed res_mints. Not the issuance ceiling." },
    { label: "RES Maximum Supply", value: capMetric.display, note: "Consensus RES_MAX_SUPPLY: 2^22 RES." },
    { label: "Remaining RES Issuance Capacity", value: remainingMetric.display },
    { label: "RES Issuance Progress", value: progress ?? UNAVAILABLE, note: "Total RES Minted / RES Maximum Supply." },
    {
      label: "Total Convergence Events",
      value: String(input.eventCount),
      note: "Distinct confirmed transactions with a positive OP_CONVERGE output.",
    },
  ];

  const historicalRows: PresentationRow[] = [
    { label: "Cumulative Convergence ORU Destroyed", value: convergenceMetric.display },
    {
      label: "Cumulative Convergence-attributable RES Generated",
      value: attributableResMetric.display,
      note:
        unattributedRes != null && unattributedRes > 0n
          ? "RES minted by a transaction that also destroyed ORU through OP_CONVERGE. RES with no such burn is excluded."
          : "RES minted by a transaction that also destroyed ORU through OP_CONVERGE.",
    },
  ];
  if (plainAtoms !== 0n || (unattributedRes != null && unattributedRes > 0n)) {
    historicalRows.push({
      label: "Cumulative Convergence-attributable ORU Destroyed",
      value: attributableOruMetric.display,
      note: "OP_CONVERGE value on the transactions that generated the RES above.",
    });
  }
  historicalRows.push(
    {
      label: "Historical Effective Conversion Ratio",
      value: historicalDisplay ?? UNAVAILABLE,
      note: HISTORICAL_RATIO_NOTE,
    },
    {
      label: "RES Generated per 1,000 ORU Destroyed",
      value: perThousand ?? UNAVAILABLE,
      note: "Uses the same mint-linked ORU and RES as the historical ratio.",
    },
  );
  if (inclusiveDisplay) {
    historicalRows.push({
      label: "All Convergence ORU per attributable RES",
      value: inclusiveDisplay,
      note: "Includes plain OP_CONVERGE that minted no RES. This is not the mint-linked ratio.",
    });
  }

  const currentRows: PresentationRow[] = [
    { label: "Current / Latest Mint Conversion Information", value: currentDisplay ?? UNAVAILABLE, note: CURRENT_CONVERSION_NOTE },
    { label: "Confirming height", value: input.confirmHeight == null ? UNAVAILABLE : String(input.confirmHeight) },
    { label: "Last mint height", value: input.lastMintHeight == null ? UNAVAILABLE : String(input.lastMintHeight) },
  ];

  return {
    title: "ORU Convergence",
    subtitle: "Cumulative ORU Destruction and RES Generation — RIGOR Mainnet",
    explanation: CONVERGENCE_EXPLANATION,
    generated_from: "indexed canonical chain",
    snapshot: {
      indexer_height: input.indexerHeight,
      indexer_block_hash: input.indexerBlockHash,
      node_height: input.nodeHeight,
      node_best_block_hash: input.nodeBestBlockHash,
      lag_blocks: lag,
      confirm_height: input.confirmHeight,
      heights_match: heightsMatch,
      tip_hash_match: tipHashMatch,
      header_convergence_matches_index: headerMatch,
      freshness: freshnessLine(input, lag),
      lag_note: lagNote,
      accounting_note: accountingNote,
    },
    metrics: {
      total_issued: issuedMetric,
      total_destroyed: totalDestroyedMetric,
      convergence_destroyed: convergenceMetric,
      plain_destroyed: plainMetric,
      fees_destroyed: feeMetric,
      convergence_destruction_ratio: { display: convergenceRatio ?? UNAVAILABLE, available: convergenceRatio != null },
      total_destruction_ratio: { display: totalRatio ?? UNAVAILABLE, available: totalRatio != null },
      total_res_minted: resMetric,
      res_maximum_supply: capMetric,
      remaining_res: remainingMetric,
      res_issuance_progress: { display: progress ?? UNAVAILABLE, available: progress != null },
      convergence_events: { count: input.eventCount, display: String(input.eventCount) },
      attributable_oru: attributableOruMetric,
      attributable_res: attributableResMetric,
      unattributed_res: unattributedMetric,
      historical_ratio: { display: historicalDisplay ?? UNAVAILABLE, available: historicalDisplay != null },
      res_per_1000_oru: { display: perThousand ?? UNAVAILABLE, available: perThousand != null },
      inclusive_convergence_ratio: {
        display: inclusiveDisplay ?? historicalDisplay ?? UNAVAILABLE,
        available: (inclusiveDisplay ?? historicalDisplay) != null,
        shown: inclusiveDisplay != null,
      },
      current_conversion: {
        display: currentDisplay ?? UNAVAILABLE,
        available: currentDisplay != null,
        confirm_height: input.confirmHeight,
      },
      last_mint_height: input.lastMintHeight,
      partition_consistent: partitionConsistent,
      homepage_issued_matches: homepageIssuedMatches,
      homepage_convergence_matches: homepageConvergenceMatches,
      homepage_res_scope: "Indexer sum of res_mints. The homepage RES minted field uses the same sum.",
    },
    sections: {
      destruction: { id: "oru-destruction", title: "ORU Destruction", rows: destructionRows },
      res: { id: "res-generation", title: "RES Generation", rows: resRows },
      relationship: {
        id: "oru-res-relationship",
        title: "ORU–RES Economic Relationship",
        rows: [],
        groups: [
          { title: "Historical cumulative average", rows: historicalRows },
          { title: "Current / Latest Mint Conversion Information", rows: currentRows },
        ],
      },
    },
    recent: input.recent.map((row) => ({
      height: row.height,
      txid: row.txid,
      block_hash: row.blockHash,
      oru_destroyed_display: atomsToOruDisplay(row.burnAtoms),
      res_minted_display: atomsToResDisplay(row.resAtoms),
    })),
    notes: {
      historical_ratio: HISTORICAL_RATIO_NOTE,
      current_conversion: CURRENT_CONVERSION_NOTE,
      plain_destroy: PLAIN_SUBSET_NOTE,
      fees: FEE_SEPARATE_NOTE,
    },
  };
}
