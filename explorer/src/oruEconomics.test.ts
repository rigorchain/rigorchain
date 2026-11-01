import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { ORU_COIN, RES_COIN } from "./amount";
import { RES_MAX_SUPPLY, resDestroyCostForHeight } from "./resIssuanceSchedule";
import { oruEconomicsAggregateSql, oruEconomicsRecentSql } from "./oruEconomicsQuery";
import {
  buildOruEconomics,
  derivedOruSubsidyCumulativeAtoms,
  foldAfterReorg,
  foldCanonicalEvents,
  formatOruPerRes,
  formatPercent,
  formatResPerThousandOru,
  remainingResAtoms,
  UNAVAILABLE,
  type CanonicalDestroyEvent,
  type OruEconomicsInput,
} from "./oruEconomics";

const SNAPSHOT_HASH = "c0a2f4ef1de70c8d38c1e2b891d994d3a18877993b7022b4e9d8bc5b70b1c984";
const ISSUED_1956 = 25036800000000n;
const CONVERGENCE_1956 = 1713252258735n;
const RES_1956 = 1672968358n;

function input(overrides: Partial<OruEconomicsInput> = {}): OruEconomicsInput {
  return {
    indexerHeight: 1956,
    indexerBlockHash: SNAPSHOT_HASH,
    nodeHeight: 1956,
    nodeBestBlockHash: SNAPSHOT_HASH,
    confirmHeight: 1957,
    nodeIssuedAtoms: ISSUED_1956,
    nodeConvergenceAtoms: CONVERGENCE_1956,
    headerConvergenceAtoms: CONVERGENCE_1956,
    indexedConvergenceAtoms: CONVERGENCE_1956,
    plainAtoms: 0n,
    attributableOruAtoms: CONVERGENCE_1956,
    attributableResAtoms: RES_1956,
    totalResAtoms: RES_1956,
    feeAtoms: 0n,
    eventCount: 79,
    lastMintHeight: 1947,
    recent: [],
    ...overrides,
  };
}

function event(partial: Partial<CanonicalDestroyEvent> & Pick<CanonicalDestroyEvent, "txid">): CanonicalDestroyEvent {
  return {
    height: 1,
    confirmed: true,
    rejected: false,
    convergenceAtoms: 0n,
    resAtoms: 0n,
    feeAtoms: 0n,
    ...partial,
  };
}

describe("ORU issuance and destruction reconstruction", () => {
  it("reconstructs issued ORU as the flat subsidy closed form", () => {
    assert.equal(derivedOruSubsidyCumulativeAtoms(0), 0n);
    assert.equal(derivedOruSubsidyCumulativeAtoms(1), 128n * ORU_COIN);
    assert.equal(derivedOruSubsidyCumulativeAtoms(1956), ISSUED_1956);
    assert.equal(derivedOruSubsidyCumulativeAtoms(-1), null);
  });

  it("separates convergence, plain destroys, and fee burns without double counting", () => {
    const fold = foldCanonicalEvents(
      [
        event({ txid: "mint", height: 10, convergenceAtoms: 1000n, resAtoms: 1n }),
        event({ txid: "plain", height: 11, convergenceAtoms: 50n, resAtoms: 0n }),
        event({ txid: "fee-only", height: 12, convergenceAtoms: 0n, resAtoms: 0n, feeAtoms: 7n }),
        event({ txid: "mint-with-fee", height: 13, convergenceAtoms: 200n, resAtoms: 1n, feeAtoms: 3n }),
      ],
      13,
    );
    assert.equal(fold.convergenceAtoms, 1250n);
    assert.equal(fold.plainAtoms, 50n);
    assert.equal(fold.attributableOruAtoms, 1200n);
    assert.equal(fold.feeAtoms, 10n);
    assert.equal(fold.convergenceAtoms + fold.feeAtoms, 1260n);
    assert.equal(fold.plainAtoms + fold.attributableOruAtoms, fold.convergenceAtoms);
    const view = buildOruEconomics(
      input({
        indexerHeight: 13,
        indexedConvergenceAtoms: fold.convergenceAtoms,
        plainAtoms: fold.plainAtoms,
        attributableOruAtoms: fold.attributableOruAtoms,
        attributableResAtoms: fold.attributableResAtoms,
        totalResAtoms: fold.totalResAtoms,
        feeAtoms: fold.feeAtoms,
        headerConvergenceAtoms: fold.convergenceAtoms,
        nodeConvergenceAtoms: fold.convergenceAtoms,
        nodeIssuedAtoms: derivedOruSubsidyCumulativeAtoms(13),
      }),
    );
    assert.equal(view.metrics.total_destroyed.atoms, "1260");
    assert.equal(view.metrics.partition_consistent, true);
    assert.equal(view.metrics.convergence_destruction_ratio.display, formatPercent(1250n, 13n * 128n * ORU_COIN));
    assert.equal(view.metrics.total_destruction_ratio.display, formatPercent(1260n, 13n * 128n * ORU_COIN));
  });

  it("does not count a duplicate txid twice", () => {
    const fold = foldCanonicalEvents(
      [
        event({ txid: "same", height: 2, convergenceAtoms: 10n, resAtoms: 1n, feeAtoms: 4n }),
        event({ txid: "same", height: 2, convergenceAtoms: 10n, resAtoms: 1n, feeAtoms: 4n }),
      ],
      2,
    );
    assert.equal(fold.eventCount, 1);
    assert.equal(fold.convergenceAtoms, 10n);
    assert.equal(fold.totalResAtoms, 1n);
    assert.equal(fold.feeAtoms, 4n);
  });

  it("returns Unavailable instead of a ratio when issued ORU is zero", () => {
    const view = buildOruEconomics(
      input({
        indexerHeight: 0,
        indexedConvergenceAtoms: 0n,
        plainAtoms: 0n,
        attributableOruAtoms: 0n,
        attributableResAtoms: 0n,
        totalResAtoms: 0n,
        feeAtoms: 0n,
        headerConvergenceAtoms: 0n,
        nodeIssuedAtoms: 0n,
        nodeConvergenceAtoms: 0n,
        confirmHeight: 1,
      }),
    );
    assert.equal(view.metrics.total_issued.display, "0.00000000 ORU");
    assert.equal(view.metrics.convergence_destruction_ratio.display, UNAVAILABLE);
    assert.equal(view.metrics.total_destruction_ratio.display, UNAVAILABLE);
    assert.equal(view.metrics.historical_ratio.display, UNAVAILABLE);
    assert.equal(view.metrics.res_per_1000_oru.display, UNAVAILABLE);
    assert.equal(view.metrics.historical_ratio.display.includes("NaN"), false);
    assert.equal(view.metrics.historical_ratio.display.includes("Infinity"), false);
  });

  it("leaves total destroyed unavailable when fee history is missing", () => {
    const view = buildOruEconomics(input({ feeAtoms: null }));
    assert.equal(view.metrics.fees_destroyed.display, UNAVAILABLE);
    assert.equal(view.metrics.total_destroyed.display, UNAVAILABLE);
    assert.equal(view.metrics.total_destruction_ratio.display, UNAVAILABLE);
    assert.equal(view.metrics.convergence_destroyed.available, true);
    assert.equal(view.metrics.convergence_destruction_ratio.available, true);
  });
});

describe("RES generation accounting", () => {
  it("matches the consensus RES_MAX_SUPPLY of 2^22 RES", () => {
    const header = readFileSync(
      join(__dirname, "../../core/src/consensus/rigor_consensus_lock.h"),
      "utf8",
    );
    assert.match(header, /RES_MAX_SUPPLY = 419430400000000LL/);
    assert.match(header, /cap must be 2\^22 RES atoms/);
    assert.equal(RES_MAX_SUPPLY, (1n << 22n) * RES_COIN);
    assert.equal(RES_MAX_SUPPLY, 419430400000000n);
  });

  it("reconstructs minted RES, remaining capacity, and progress", () => {
    const view = buildOruEconomics(input());
    assert.equal(view.metrics.total_res_minted.atoms, RES_1956.toString());
    assert.equal(view.metrics.total_res_minted.display, "16.72968358 RES");
    assert.equal(view.metrics.res_maximum_supply.display, "4194304.00000000 RES");
    assert.equal(remainingResAtoms(RES_1956), RES_MAX_SUPPLY - RES_1956);
    assert.equal(view.metrics.remaining_res.atoms, (RES_MAX_SUPPLY - RES_1956).toString());
    assert.equal(view.metrics.res_issuance_progress.display, "0.00039887%");
    assert.equal(view.metrics.convergence_events.count, 79);
  });

  it("excludes rejected, unconfirmed, and not-yet-canonical events", () => {
    const fold = foldCanonicalEvents(
      [
        event({ txid: "ok", height: 4, convergenceAtoms: 5n, resAtoms: 1n }),
        event({ txid: "rejected", height: 4, rejected: true, convergenceAtoms: 9n, resAtoms: 1n }),
        event({ txid: "mempool", height: 4, confirmed: false, convergenceAtoms: 8n, resAtoms: 1n }),
        event({ txid: "future", height: 9, convergenceAtoms: 7n, resAtoms: 1n }),
      ],
      4,
    );
    assert.equal(fold.eventCount, 1);
    assert.equal(fold.totalResAtoms, 1n);
    assert.equal(fold.convergenceAtoms, 5n);
  });

  it("does not report a negative remaining capacity above the cap", () => {
    assert.equal(remainingResAtoms(RES_MAX_SUPPLY + 1n), 0n);
    const view = buildOruEconomics(input({ totalResAtoms: RES_MAX_SUPPLY + 5n, attributableResAtoms: RES_MAX_SUPPLY }));
    assert.equal(view.metrics.remaining_res.atoms, "0");
    assert.equal(view.metrics.res_issuance_progress.display.startsWith("-"), false);
  });
});

describe("ORU–RES economic relationship", () => {
  it("keeps the historical average distinct from destroy_cost at the confirming height", () => {
    const view = buildOruEconomics(input());
    assert.equal(resDestroyCostForHeight(1957), 102411944928n);
    assert.equal(view.metrics.current_conversion.display, "1024.11944928 ORU destroyed to mint 1 RES");
    assert.equal(view.metrics.historical_ratio.display, "1024.07929627 ORU per 1 RES");
    assert.equal(view.metrics.res_per_1000_oru.display, "0.97648688 RES per 1,000 ORU");
    assert.notEqual(view.metrics.historical_ratio.display, view.metrics.current_conversion.display);
    assert.equal(view.metrics.homepage_issued_matches, true);
    assert.equal(view.metrics.homepage_convergence_matches, true);
    assert.equal(view.snapshot.header_convergence_matches_index, true);
    assert.equal(view.metrics.total_issued.display, "250368.00000000 ORU");
    assert.equal(view.metrics.convergence_destroyed.display, "17132.52258735 ORU");
    assert.equal(view.metrics.plain_destroyed.display, "0.00000000 ORU");
    assert.equal(view.metrics.total_destroyed.display, "17132.52258735 ORU");
  });

  it("uses mint-linked burns when plain destroys are present", () => {
    const view = buildOruEconomics(
      input({
        indexedConvergenceAtoms: 1500n,
        plainAtoms: 500n,
        attributableOruAtoms: 1000n,
        attributableResAtoms: 1n * RES_COIN,
        totalResAtoms: 1n * RES_COIN,
        headerConvergenceAtoms: 1500n,
        feeAtoms: 0n,
      }),
    );
    assert.equal(view.metrics.historical_ratio.display, formatOruPerRes(1000n, RES_COIN));
    assert.equal(view.metrics.inclusive_convergence_ratio.shown, true);
    assert.notEqual(view.metrics.historical_ratio.display, view.metrics.inclusive_convergence_ratio.display);
    const labels = view.sections.relationship.groups?.flatMap((group) => group.rows.map((row) => row.label)) ?? [];
    assert.ok(labels.includes("Historical Effective Conversion Ratio"));
    assert.ok(labels.includes("Current / Latest Mint Conversion Information"));
    assert.ok(labels.includes("Cumulative Convergence-attributable ORU Destroyed"));
  });

  it("marks the historical ratio unavailable when attribution is missing", () => {
    const view = buildOruEconomics(
      input({
        indexedConvergenceAtoms: 100n,
        plainAtoms: 0n,
        attributableOruAtoms: 40n,
        attributableResAtoms: 0n,
        totalResAtoms: 5n,
        headerConvergenceAtoms: 100n,
      }),
    );
    assert.equal(view.metrics.partition_consistent, false);
    assert.equal(view.metrics.historical_ratio.display, UNAVAILABLE);
    assert.equal(view.metrics.total_destroyed.display, UNAVAILABLE);
  });

  it("does not compare homepage figures across different heights", () => {
    const view = buildOruEconomics(input({ nodeHeight: 2000, nodeBestBlockHash: "aa".repeat(32) }));
    assert.equal(view.snapshot.heights_match, false);
    assert.equal(view.metrics.homepage_issued_matches, null);
    assert.equal(view.metrics.homepage_convergence_matches, null);
    assert.match(view.snapshot.lag_note ?? "", /behind the node/);
  });
});

describe("indexer canonical reconstruction", () => {
  it("folds genesis through the tip and rolls a reorg back to the parent prefix", () => {
    const events = [
      event({ txid: "a", height: 1, convergenceAtoms: 10n, resAtoms: 1n }),
      event({ txid: "b", height: 2, convergenceAtoms: 20n, resAtoms: 0n, feeAtoms: 2n }),
      event({ txid: "c", height: 3, convergenceAtoms: 30n, resAtoms: 4n, feeAtoms: 1n }),
    ];
    const full = foldCanonicalEvents(events, 3);
    assert.equal(full.issuedAtoms, 3n * 128n * ORU_COIN);
    assert.equal(full.convergenceAtoms, 60n);
    assert.equal(full.eventCount, 3);
    const rolled = foldAfterReorg(events, 3);
    assert.equal(rolled.convergenceAtoms, 30n);
    assert.equal(rolled.feeAtoms, 2n);
    assert.equal(rolled.totalResAtoms, 1n);
    assert.equal(rolled.eventCount, 2);
    const again = foldCanonicalEvents(events.filter((row) => row.height < 3), 2);
    assert.deepEqual(rolled, again);
  });

  it("queries the full canonical chain rather than a rolling window", () => {
    const sql = oruEconomicsAggregateSql();
    assert.match(sql, /JOIN blocks b ON b.block_hash = t.block_hash/);
    assert.match(sql, /rfc018_fee_burn_stats/);
    assert.match(sql, /s.height = \(SELECT height FROM tip\)/);
    assert.doesNotMatch(sql, /last 100| - 100|LIMIT 100/i);
    assert.match(sql, /COUNT\(DISTINCT o.txid\)/);
    assert.match(oruEconomicsRecentSql(), /JOIN blocks b ON b.block_hash = t.block_hash/);
  });

  it("flags an indexed convergence sum that disagrees with the block header prefix", () => {
    const view = buildOruEconomics(input({ headerConvergenceAtoms: CONVERGENCE_1956 + 1n }));
    assert.equal(view.snapshot.header_convergence_matches_index, false);
    assert.match(view.snapshot.accounting_note ?? "", /not independently verified/);
  });
});

describe("decimal precision", () => {
  it("rounds the historical ratio half up at 8 decimal places", () => {
    assert.equal(formatOruPerRes(CONVERGENCE_1956, RES_1956), "1024.07929627 ORU per 1 RES");
    assert.equal(formatResPerThousandOru(RES_1956, CONVERGENCE_1956), "0.97648688 RES per 1,000 ORU");
    assert.equal(formatOruPerRes(1n, 0n), null);
    assert.equal(formatResPerThousandOru(1n, 0n), null);
  });
});
