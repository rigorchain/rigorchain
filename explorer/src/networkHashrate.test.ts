import { beforeEach, describe, it } from "node:test";
import assert from "node:assert/strict";
import {
  HASHRATE_NOTE,
  HASHRATE_UNAVAILABLE,
  blockWorkFromCompact,
  childClearsParentMedian,
  clearNetworkHashrateCache,
  estimateNetworkHashrate,
  formatWorkRate,
  medianTimePast,
  type HashrateBlock,
} from "./networkHashrate";

/**
 * Genesis compact 0x1d008000 is target 2^223.
 * core/src/kernel/chainparams.cpp records GetBlockProof(0x1d008000) = 2^33 - 1.
 * 0x1d010000 is target 2^224, so floor(2^256 / (2^224 + 1)) = 2^32 - 1.
 */
const WORK_2_223 = (1n << 33n) - 1n;
const WORK_2_224 = (1n << 32n) - 1n;
const GENESIS_BITS = "1d008000";
const HARDER_BITS = "1d010000";

function independentTarget(compactHex: string): bigint {
  const compact = Number.parseInt(compactHex, 16);
  const nSize = compact >>> 24;
  let nWord = compact & 0x007fffff;
  if (nSize <= 3) {
    nWord >>>= 8 * (3 - nSize);
    return BigInt(nWord >>> 0);
  }
  return BigInt(nWord) << BigInt(8 * (nSize - 3));
}

function chain(
  count: number,
  opts?: {
    origin?: number;
    start?: number;
    step?: number | number[];
    bits?: string | ((height: number) => string);
  },
): HashrateBlock[] {
  const origin = opts?.origin ?? 0;
  const start = opts?.start ?? 1_700_000_000;
  const blocks: HashrateBlock[] = [];
  let time = start;
  for (let i = 0; i < count; i++) {
    const height = origin + i;
    if (i > 0) {
      const step = Array.isArray(opts?.step) ? opts.step[i - 1] ?? 600 : opts?.step ?? 600;
      time += step;
    }
    const bits = typeof opts?.bits === "function" ? opts.bits(height) : opts?.bits ?? GENESIS_BITS;
    blocks.push({ height, time, bits });
  }
  return blocks;
}

function nowAfter(blocks: readonly HashrateBlock[], skew = 30): number {
  const tip = blocks[blocks.length - 1];
  return Number(tip?.time ?? 0) + skew;
}

beforeEach(() => {
  clearNetworkHashrateCache();
});

describe("compact block work", () => {
  it("matches the published genesis proof and an independent target division", () => {
    const target = independentTarget(GENESIS_BITS);
    assert.equal(target, 1n << 223n);
    assert.equal((1n << 256n) / (target + 1n), WORK_2_223);
    assert.equal(blockWorkFromCompact(GENESIS_BITS), WORK_2_223);
    assert.equal(blockWorkFromCompact(0x1d008000), WORK_2_223);

    const harder = independentTarget(HARDER_BITS);
    assert.equal(harder, 1n << 224n);
    assert.equal((1n << 256n) / (harder + 1n), WORK_2_224);
    assert.equal(blockWorkFromCompact(HARDER_BITS), WORK_2_224);
  });

  it("rejects a negative compact, an overflow compact, and a zero target", () => {
    assert.equal(blockWorkFromCompact("1d808000"), null);
    assert.equal(blockWorkFromCompact(0x23100000), null);
    assert.equal(blockWorkFromCompact(0), null);
    assert.equal(blockWorkFromCompact("zz"), null);
  });
});

describe("median time past", () => {
  it("returns the middle sample of at most eleven header times", () => {
    assert.equal(medianTimePast([11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1]), 6);
    assert.equal(medianTimePast([1]), 1);
    assert.equal(medianTimePast([1, 2]), 2);
    assert.equal(medianTimePast([]), null);
  });
});

describe("50-block network hashrate", () => {
  it("uses one difficulty for every closing block and the boundary elapsed time", () => {
    const blocks = chain(51);
    const view = estimateNetworkHashrate(blocks, nowAfter(blocks));
    assert.equal(view.available, true);
    assert.equal(view.elapsed_seconds, 50 * 600);
    assert.equal(view.work, (50n * WORK_2_223).toString());
    assert.equal(view.hashes_per_second, ((50n * WORK_2_223) / 30000n).toString());
    assert.equal(view.display, "14.32 MH/s");
    assert.equal(view.note, HASHRATE_NOTE);
    assert.equal(view.estimate, true);
  });

  it("sums each interval's own work when difficulty changes inside the window", () => {
    const blocks = chain(51, {
      bits: (height) => (height >= 26 ? HARDER_BITS : GENESIS_BITS),
    });
    const view = estimateNetworkHashrate(blocks, nowAfter(blocks));
    const expected = 25n * WORK_2_223 + 25n * WORK_2_224;
    assert.equal(view.available, true);
    assert.equal(view.work, expected.toString());
    assert.equal(view.elapsed_seconds, 30000);
    assert.equal(view.hashes_per_second, (expected / 30000n).toString());
    assert.equal(view.display, "10.74 MH/s");
    const naiveTip = (WORK_2_224 / 600n).toString();
    assert.notEqual(view.hashes_per_second, naiveTip);
    assert.notEqual(view.display, "7.16 MH/s");
  });

  it("does not include the opening boundary block's work", () => {
    const same = chain(51);
    const otherBoundary = chain(51, {
      bits: (height) => (height === 0 ? HARDER_BITS : GENESIS_BITS),
    });
    const left = estimateNetworkHashrate(same, nowAfter(same));
    clearNetworkHashrateCache();
    const right = estimateNetworkHashrate(otherBoundary, nowAfter(otherBoundary));
    assert.equal(left.hashes_per_second, right.hashes_per_second);
  });

  it("divides by the actual boundary elapsed time when intervals are uneven", () => {
    const steps = Array.from({ length: 50 }, () => 600);
    steps[10] = 1000;
    const blocks = chain(51, { step: steps });
    const view = estimateNetworkHashrate(blocks, nowAfter(blocks));
    assert.equal(view.elapsed_seconds, 49 * 600 + 1000);
    assert.equal(view.hashes_per_second, ((50n * WORK_2_223) / 30400n).toString());
  });

  it("estimates across a backward step that still clears median time past", () => {
    const blocks = chain(51);
    const previous = Number(blocks[48]?.time);
    blocks[49] = { ...blocks[49], time: previous + 5135 };
    blocks[50] = { ...blocks[50], time: previous + 5135 - 1975 };
    assert.equal(childClearsParentMedian(blocks, 50), true);
    const whileJumpIsAhead = estimateNetworkHashrate(blocks, previous + 5135 - 700);
    assert.equal(whileJumpIsAhead.available, false);
    assert.equal(whileJumpIsAhead.reason, "future_timestamp");
    assert.equal(whileJumpIsAhead.display, HASHRATE_UNAVAILABLE);
    clearNetworkHashrateCache();
    const view = estimateNetworkHashrate(blocks, previous + 5135 + 30);
    assert.equal(view.available, true);
    assert.equal(view.reason, null);
    assert.equal(view.elapsed_seconds, 48 * 600 + 3160);
    assert.equal(view.work, (50n * WORK_2_223).toString());
    assert.equal(view.display !== HASHRATE_UNAVAILABLE, true);
  });

  it("keeps the estimate when one header is about two minutes behind its parent", () => {
    const blocks = chain(61, { origin: 2130 });
    const dipIndex = blocks.findIndex((block) => block.height === 2186);
    const parent = blocks[dipIndex - 1];
    const dipped = blocks[dipIndex];
    if (!parent || !dipped) throw new Error("missing dip headers");
    blocks[dipIndex] = { ...dipped, time: Number(parent.time) - 104 };
    assert.equal(childClearsParentMedian(blocks, 2186), true);
    const view = estimateNetworkHashrate(blocks, nowAfter(blocks));
    const start = blocks.find((block) => block.height === 2140);
    const tip = blocks.find((block) => block.height === 2190);
    assert.ok(start && tip);
    assert.equal(view.available, true);
    assert.equal(view.reason, null);
    assert.equal(view.elapsed_seconds, Number(tip.time) - Number(start.time));
    assert.equal(view.elapsed_seconds, 50 * 600);
  });

  it("rejects a backward step that does not clear median time past", () => {
    const blocks = chain(51);
    const parent = Number(blocks[49]?.time);
    blocks[50] = { ...blocks[50], time: parent - 4000 };
    assert.equal(childClearsParentMedian(blocks, 50), false);
    const view = estimateNetworkHashrate(blocks, parent + 30);
    assert.equal(view.available, false);
    assert.equal(view.reason, "median_time_past");
    assert.equal(view.display, HASHRATE_UNAVAILABLE);
  });

  it("rejects a zero-length interval and does not display zero", () => {
    const blocks = chain(51).map((block) => ({ ...block, time: 1_700_000_000 }));
    const view = estimateNetworkHashrate(blocks, 1_700_000_100);
    assert.equal(view.reason, "non_positive_elapsed");
    assert.equal(view.display, "Unavailable");
    assert.doesNotMatch(view.display, /0 H\/s/);
  });

  it("rejects a tip that is minutes ahead of the explorer clock", () => {
    const blocks = chain(51);
    const tip = Number(blocks[50]?.time);
    const view = estimateNetworkHashrate(blocks, tip - 3590);
    assert.equal(view.available, false);
    assert.equal(view.reason, "future_timestamp");
    assert.equal(view.display, "Unavailable");
  });

  it("rejects a header more than two hours ahead of the explorer clock", () => {
    const blocks = chain(51);
    const tip = Number(blocks[50]?.time);
    const view = estimateNetworkHashrate(blocks, tip - 8000);
    assert.equal(view.reason, "future_timestamp");
  });

  it("rejects one implausible forward jump that lands slightly in the future", () => {
    const blocks = chain(51);
    const parent = Number(blocks[49]?.time);
    blocks[50] = { ...blocks[50], time: parent + 8000 };
    const view = estimateNetworkHashrate(blocks, parent + 8000 - 50);
    assert.equal(view.reason, "implausible_interval");
    assert.equal(view.display, "Unavailable");
  });

  it("keeps a long historical interval once the later header is not in the future", () => {
    const blocks = chain(51);
    const parent = Number(blocks[49]?.time);
    blocks[50] = { ...blocks[50], time: parent + 8000 };
    const view = estimateNetworkHashrate(blocks, parent + 8000 + 30);
    assert.equal(view.available, true);
    assert.equal(view.elapsed_seconds, 49 * 600 + 8000);
  });

  it("reports unavailable when fewer than 50 intervals exist", () => {
    const view = estimateNetworkHashrate(chain(10), 1_800_000_000);
    assert.equal(view.reason, "insufficient_history");
    assert.equal(view.display, "Unavailable");
  });

  it("reports unavailable when a height or compact field is missing", () => {
    const gap = chain(51).filter((block) => block.height !== 25);
    assert.equal(estimateNetworkHashrate(gap, nowAfter(chain(51))).reason, "missing_block_data");

    const blank = chain(51);
    blank[10] = { ...blank[10], bits: "" };
    assert.equal(estimateNetworkHashrate(blank, nowAfter(blank)).reason, "missing_block_data");

    const broken = chain(51);
    broken[10] = { ...broken[10], bits: "1d808000" };
    assert.equal(estimateNetworkHashrate(broken, nowAfter(broken)).reason, "invalid_bits");
  });

  it("accepts a high window when the median ancestors are present", () => {
    const blocks = chain(61, { origin: 100 });
    const view = estimateNetworkHashrate(blocks, nowAfter(blocks));
    assert.equal(view.available, true);
    assert.equal(view.elapsed_seconds, 30000);
  });

  it("recalculates when the window changes and holds a stable window in cache", () => {
    const blocks = chain(51);
    const now = nowAfter(blocks);
    const first = estimateNetworkHashrate(blocks, now);
    const second = estimateNetworkHashrate(blocks, now + 5);
    assert.equal(first, second);

    const moved = blocks.map((block) => ({ ...block }));
    const tip = moved[50];
    if (!tip) throw new Error("missing tip");
    moved[50] = { ...tip, time: Number(tip.time) + 1 };
    const third = estimateNetworkHashrate(moved, now + 5);
    assert.notEqual(first, third);
    assert.equal(third.elapsed_seconds, 30001);
  });

  it("recalculates a future window after the explorer clock catches the header", () => {
    const blocks = chain(51);
    const tip = Number(blocks[50]?.time);
    const early = estimateNetworkHashrate(blocks, tip - 500);
    assert.equal(early.available, false);
    const later = estimateNetworkHashrate(blocks, tip + 10);
    assert.equal(later.available, true);
    assert.notEqual(early, later);
  });
});

describe("hashrate display units", () => {
  it("selects H/s through PH/s and rounds half up", () => {
    assert.equal(formatWorkRate(1n, 1n), "1.00 H/s");
    assert.equal(formatWorkRate(999n, 1n), "999.00 H/s");
    assert.equal(formatWorkRate(1500n, 1n), "1.50 kH/s");
    assert.equal(formatWorkRate(2_500_000n, 1n), "2.50 MH/s");
    assert.equal(formatWorkRate(136_850_000_000n, 1n), "136.85 GH/s");
    assert.equal(formatWorkRate(136850000000n * 50n, 50n), "136.85 GH/s");
    assert.equal(formatWorkRate(3_000_000_000_000n, 1n), "3.00 TH/s");
    assert.equal(formatWorkRate(4_000_000_000_000_000n, 1n), "4.00 PH/s");
    assert.equal(formatWorkRate(999995000000n, 1n), "1.00 TH/s");
  });

  it("does not render a positive rate as zero", () => {
    const tiny = formatWorkRate(1n, 100000n);
    assert.equal(tiny, "0.000010 H/s");
    assert.notEqual(tiny, "0 H/s");
    assert.notEqual(tiny, "0.00 H/s");
    assert.equal(formatWorkRate(0n, 1n), "Unavailable");
  });
});
