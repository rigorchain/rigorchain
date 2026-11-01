/**
 * Explorer-only estimated network hashrate.
 *
 * Reads indexed header time and compact nBits. Does not accept blocks, does not
 * call CheckProofOfWork, and does not change ASERT, MTP, or any consensus rule.
 *
 * The 50-block figure is an estimate of hashes per second implied by block work,
 * not a measurement of miner hardware and not a pool-reported hashrate.
 */

import { parseBitsToUint32 } from "./consensusFingerprint";

/** Display mirror of chain.h MAX_FUTURE_BLOCK_TIME. Not enforced as consensus. */
export const MAX_FUTURE_BLOCK_TIME_SECONDS = 2 * 60 * 60;

/** Display mirror of CBlockIndex::nMedianTimeSpan. */
export const MEDIAN_TIME_SPAN = 11;

/**
 * Headers this far ahead of the explorer clock are not used.
 * Ordinary sub-minute skew still produces an estimate. The mainnet cases that
 * sat many minutes ahead of the receiving node do not.
 */
export const FUTURE_TIMESTAMP_GRACE_SECONDS = 120;

export const HASHRATE_INTERVALS = 50;

export const HASHRATE_LABEL = "Network Hashrate (50 blocks)";

export const HASHRATE_NOTE =
  "Estimated from actual block proof-of-work over the latest 50 block intervals. This is not a direct measurement of miner hardware.";

export const HASHRATE_UNAVAILABLE = "Unavailable";

const UNITS = ["H/s", "kH/s", "MH/s", "GH/s", "TH/s", "PH/s"] as const;

export type HashrateUnavailableReason =
  | "insufficient_history"
  | "missing_block_data"
  | "invalid_bits"
  | "non_monotonic_timestamp"
  | "non_positive_elapsed"
  | "future_timestamp"
  | "implausible_interval"
  | "median_time_past";

export type HashrateBlock = {
  height: unknown;
  time: unknown;
  bits: unknown;
};

export type NetworkHashrateView = {
  label: typeof HASHRATE_LABEL;
  available: boolean;
  display: string;
  hashes_per_second: string | null;
  interval_count: number;
  elapsed_seconds: number | null;
  work: string | null;
  reason: HashrateUnavailableReason | null;
  estimate: true;
  note: typeof HASHRATE_NOTE;
};

type Row = { height: number; time: number; bits: number };

type CacheEntry = {
  key: string;
  view: NetworkHashrateView;
  computedAt: number;
  revalidateAt: number;
};

let cache: CacheEntry | null = null;

export function clearNetworkHashrateCache(): void {
  cache = null;
}

function pow10(exp: number): bigint {
  let value = 1n;
  for (let i = 0; i < exp; i++) value *= 10n;
  return value;
}

/**
 * Bitcoin/RIGOR compact target. Same shift and sign/overflow predicates as
 * arith_uint256::SetCompact. Display math only.
 */
export function decodeCompactBits(compact: number): { target: bigint; negative: boolean; overflow: boolean } {
  const nSize = compact >>> 24;
  let nWord = compact & 0x007fffff;
  let target: bigint;
  if (nSize <= 3) {
    nWord >>>= 8 * (3 - nSize);
    target = BigInt(nWord >>> 0);
  } else {
    target = BigInt(nWord) << BigInt(8 * (nSize - 3));
  }
  const negative = nWord !== 0 && (compact & 0x00800000) !== 0;
  const overflow =
    nWord !== 0 && (nSize > 34 || (nWord > 0xff && nSize > 33) || (nWord > 0xffff && nSize > 32));
  return { target, negative, overflow };
}

/** floor(2^256 / (target + 1)). Null when compact is unusable. Matches GetBlockProof. */
export function blockWorkFromCompact(bits: unknown): bigint | null {
  const compact = parseBitsToUint32(bits);
  if (compact == null) return null;
  const decoded = decodeCompactBits(compact);
  if (decoded.negative || decoded.overflow || decoded.target <= 0n) return null;
  if (decoded.target >= 1n << 256n) return null;
  return (1n << 256n) / (decoded.target + 1n);
}

/** Middle element after sorting, matching CBlockIndex::GetMedianTimePast. */
export function medianTimePast(times: readonly number[]): number | null {
  if (times.length === 0) return null;
  const span = times.slice(0, MEDIAN_TIME_SPAN);
  const sorted = [...span].sort((a, b) => a - b);
  return sorted[Math.floor(sorted.length / 2)] ?? null;
}

function unavailable(reason: HashrateUnavailableReason): NetworkHashrateView {
  return {
    label: HASHRATE_LABEL,
    available: false,
    display: HASHRATE_UNAVAILABLE,
    hashes_per_second: null,
    interval_count: HASHRATE_INTERVALS,
    elapsed_seconds: null,
    work: null,
    reason,
    estimate: true,
    note: HASHRATE_NOTE,
  };
}

function formatDecimals(scaled: bigint, digits: number): string {
  const base = pow10(digits);
  const whole = scaled / base;
  const frac = scaled % base;
  return `${whole.toString()}.${frac.toString().padStart(digits, "0")}`;
}

/** Unit selection with two decimal places. A positive rate is never shown as zero. */
export function formatWorkRate(work: bigint, elapsedSeconds: bigint): string {
  if (work <= 0n || elapsedSeconds <= 0n) return HASHRATE_UNAVAILABLE;
  let unit = 0;
  while (unit < UNITS.length - 1 && work >= elapsedSeconds * pow10(3 * (unit + 1))) {
    unit++;
  }
  for (;;) {
    const denom = elapsedSeconds * pow10(3 * unit);
    const hundredths = (work * 100n + denom / 2n) / denom;
    if (hundredths >= 100000n && unit < UNITS.length - 1) {
      unit++;
      continue;
    }
    if (hundredths === 0n) {
      const micros = (work * 1000000n + denom / 2n) / denom;
      if (micros === 0n) return "<0.000001 H/s";
      return `${formatDecimals(micros, 6)} H/s`;
    }
    return `${formatDecimals(hundredths, 2)} ${UNITS[unit]}`;
  }
}

function asSafeInt(value: unknown): number | null {
  if (typeof value === "bigint") {
    if (value > BigInt(Number.MAX_SAFE_INTEGER) || value < BigInt(Number.MIN_SAFE_INTEGER)) return null;
    return Number(value);
  }
  if (typeof value === "number") {
    if (!Number.isSafeInteger(value)) return null;
    return value;
  }
  if (typeof value === "string" && /^-?\d+$/.test(value.trim())) {
    const n = Number(value.trim());
    if (!Number.isSafeInteger(n)) return null;
    return n;
  }
  return null;
}

function normalize(blocks: readonly HashrateBlock[]): { rows: Row[] } | { reason: "missing_block_data" } {
  const rows: Row[] = [];
  const seen = new Set<number>();
  for (const block of blocks) {
    const height = asSafeInt(block.height);
    const time = asSafeInt(block.time);
    if (height == null || height < 0 || time == null) return { reason: "missing_block_data" };
    if (block.bits == null || block.bits === "") return { reason: "missing_block_data" };
    const bits = parseBitsToUint32(block.bits);
    if (bits == null) return { reason: "missing_block_data" };
    if (seen.has(height)) return { reason: "missing_block_data" };
    seen.add(height);
    rows.push({ height, time, bits });
  }
  rows.sort((a, b) => a.height - b.height);
  return { rows };
}

function parentMedian(byHeight: Map<number, Row>, parentHeight: number): number | null {
  const times: number[] = [];
  for (let height = parentHeight; height >= 0 && times.length < MEDIAN_TIME_SPAN; height--) {
    const row = byHeight.get(height);
    if (!row) return null;
    times.push(row.time);
  }
  return medianTimePast(times);
}

/**
 * True when child header time is strictly above the parent's median time past.
 * Null when an ancestor required by the 11-block span is absent.
 */
export function childClearsParentMedian(blocks: readonly HashrateBlock[], childHeight: number): boolean | null {
  const normalized = normalize(blocks);
  if ("reason" in normalized) return null;
  const byHeight = new Map(normalized.rows.map((row) => [row.height, row]));
  const child = byHeight.get(childHeight);
  const parentHeight = childHeight - 1;
  if (!child || parentHeight < 0) return null;
  const mtp = parentMedian(byHeight, parentHeight);
  if (mtp == null) return null;
  return child.time > mtp;
}

function signature(rows: readonly Row[]): string {
  return rows.map((row) => `${row.height}:${row.time}:${row.bits}`).join(",");
}

function horizon(nowSeconds: number, window: readonly Row[], reason: HashrateUnavailableReason | null): number {
  if (reason !== "future_timestamp" && reason !== "implausible_interval") return Number.POSITIVE_INFINITY;
  let at = Number.POSITIVE_INFINITY;
  for (const row of window) {
    for (const candidate of [
      row.time - FUTURE_TIMESTAMP_GRACE_SECONDS,
      row.time - MAX_FUTURE_BLOCK_TIME_SECONDS,
      row.time,
    ]) {
      if (candidate > nowSeconds && candidate < at) at = candidate;
    }
  }
  return Number.isFinite(at) ? at : nowSeconds + 1;
}

function compute(blocks: readonly HashrateBlock[], nowSeconds: number): NetworkHashrateView {
  if (!Number.isFinite(nowSeconds)) return unavailable("missing_block_data");
  const normalized = normalize(blocks);
  if ("reason" in normalized) return unavailable(normalized.reason);
  const { rows } = normalized;
  if (rows.length === 0) return unavailable("insufficient_history");

  const tip = rows[rows.length - 1];
  if (!tip || tip.height < HASHRATE_INTERVALS) return unavailable("insufficient_history");

  const byHeight = new Map(rows.map((row) => [row.height, row]));
  const startHeight = tip.height - HASHRATE_INTERVALS;
  const window: Row[] = [];
  for (let height = startHeight; height <= tip.height; height++) {
    const row = byHeight.get(height);
    if (!row) return unavailable("missing_block_data");
    window.push(row);
  }

  const oldestParent = startHeight;
  const oldestAncestor = Math.max(0, oldestParent - (MEDIAN_TIME_SPAN - 1));
  for (let height = oldestAncestor; height < oldestParent; height++) {
    if (!byHeight.has(height)) return unavailable("missing_block_data");
  }

  let work = 0n;
  for (let i = 1; i < window.length; i++) {
    const row = window[i];
    if (!row) return unavailable("missing_block_data");
    const proof = blockWorkFromCompact(row.bits);
    if (proof == null) return unavailable("invalid_bits");
    work += proof;
  }
  const boundary = window[0];
  if (!boundary) return unavailable("missing_block_data");
  if (blockWorkFromCompact(boundary.bits) == null) return unavailable("invalid_bits");

  for (let i = 1; i < window.length; i++) {
    const parent = window[i - 1];
    const child = window[i];
    if (!parent || !child) return unavailable("missing_block_data");
    if (child.time < parent.time) return unavailable("non_monotonic_timestamp");
    if (child.time === parent.time) return unavailable("non_positive_elapsed");
    if (child.time < 0 || parent.time < 0) return unavailable("implausible_interval");
    const delta = child.time - parent.time;
    if (child.time > nowSeconds + MAX_FUTURE_BLOCK_TIME_SECONDS) return unavailable("future_timestamp");
    if (child.time > nowSeconds && delta > MAX_FUTURE_BLOCK_TIME_SECONDS) return unavailable("implausible_interval");
    if (child.time > nowSeconds + FUTURE_TIMESTAMP_GRACE_SECONDS) return unavailable("future_timestamp");
    const mtp = parentMedian(byHeight, parent.height);
    if (mtp == null) return unavailable("missing_block_data");
    if (child.time <= mtp) return unavailable("median_time_past");
  }

  const elapsed = BigInt(tip.time) - BigInt(boundary.time);
  if (elapsed <= 0n) return unavailable("non_positive_elapsed");
  const perSecond = work / elapsed;
  return {
    label: HASHRATE_LABEL,
    available: true,
    display: formatWorkRate(work, elapsed),
    hashes_per_second: perSecond.toString(),
    interval_count: HASHRATE_INTERVALS,
    elapsed_seconds: Number(elapsed),
    work: work.toString(),
    reason: null,
    estimate: true,
    note: HASHRATE_NOTE,
  };
}

/**
 * Estimated hashrate over the latest 50 completed intervals ending at the
 * highest supplied height. Reuses a cached result until the header window
 * changes or a future-timestamp rejection could expire.
 */
export function estimateNetworkHashrate(blocks: readonly HashrateBlock[], nowSeconds: number): NetworkHashrateView {
  const normalized = normalize(blocks);
  const key = "rows" in normalized ? signature(normalized.rows) : `invalid:${blocks.length}:${nowSeconds}`;
  if (
    cache &&
    cache.key === key &&
    nowSeconds >= cache.computedAt &&
    nowSeconds < cache.revalidateAt
  ) {
    return cache.view;
  }
  const view = compute(blocks, nowSeconds);
  const rows = "rows" in normalized ? normalized.rows : [];
  const tip = rows.length > 0 ? rows[rows.length - 1] : undefined;
  const window =
    tip && tip.height >= HASHRATE_INTERVALS
      ? rows.filter((row) => row.height >= tip.height - HASHRATE_INTERVALS && row.height <= tip.height)
      : rows;
  cache = {
    key,
    view,
    computedAt: nowSeconds,
    revalidateAt: horizon(nowSeconds, window, view.reason),
  };
  return view;
}
