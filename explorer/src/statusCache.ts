/**
 * Process-wide Explorer caches.
 *
 * Production runs one Node process (one systemd unit, no cluster).
 * These snapshots live in that process and are what every HTTP client reads.
 */

import type { Pool } from "pg";
import type { Config } from "./config";
import { buildOruEconomicsView } from "./api/oruEconomics";
import {
  assembleStatusDashboard,
  loadEconomicAggregates,
  loadLiveStatus,
  type EconomicAggregates,
  type LiveStatusParts,
  type StatusDashboard,
} from "./api/status";
import type { OruEconomicsView } from "./oruEconomics";
import { SharedSnapshot } from "./sharedSnapshot";

/** Full-chain homepage and /api/status aggregates. */
export const ECONOMIC_REFRESH_MS = 45_000;
export const ECONOMIC_MIN_REFRESH_MS = 30_000;

/** Node height, peers, mempool, and the recent-block window. */
export const LIVE_REFRESH_MS = 10_000;
export const LIVE_MIN_REFRESH_MS = 5_000;

/** ORU page aggregate. Separate SQL from the homepage snapshot, same refresh budget. */
export const ORU_ECONOMICS_REFRESH_MS = 45_000;
export const ORU_ECONOMICS_MIN_REFRESH_MS = 30_000;

let liveSnap: SharedSnapshot<LiveStatusParts> | null = null;
let economicSnap: SharedSnapshot<EconomicAggregates> | null = null;
let oruSnap: SharedSnapshot<OruEconomicsView> | null = null;
let cachesStarted = false;

function iso(ms: number | null): string | null {
  return ms == null ? null : new Date(ms).toISOString();
}

export function startExplorerCaches(config: Config, pool: Pool): void {
  if (cachesStarted) return;
  cachesStarted = true;

  economicSnap = new SharedSnapshot<EconomicAggregates>({
    intervalMs: ECONOMIC_REFRESH_MS,
    minIntervalMs: ECONOMIC_MIN_REFRESH_MS,
    load: async () => {
      const value = await loadEconomicAggregates(config, pool);
      return {
        value,
        snapshotHeight: value.indexerHeight >= 0 ? value.indexerHeight : null,
      };
    },
  });

  oruSnap = new SharedSnapshot<OruEconomicsView>({
    intervalMs: ORU_ECONOMICS_REFRESH_MS,
    minIntervalMs: ORU_ECONOMICS_MIN_REFRESH_MS,
    load: async () => {
      const value = await buildOruEconomicsView(config, pool);
      const height = value.snapshot.indexer_height;
      return { value, snapshotHeight: height };
    },
  });

  liveSnap = new SharedSnapshot<LiveStatusParts>({
    intervalMs: LIVE_REFRESH_MS,
    minIntervalMs: LIVE_MIN_REFRESH_MS,
    load: async () => {
      const value = await loadLiveStatus(config, pool);
      const height = value.indexerHeight >= 0 ? value.indexerHeight : null;
      economicSnap?.noteHeight(height);
      const economic = economicSnap?.view();
      if (economic?.stale) void economicSnap?.refresh(false);
      return { value, snapshotHeight: height };
    },
  });

  economicSnap.start();
  oruSnap.start();
  liveSnap.start();
}

export function stopExplorerCaches(): void {
  liveSnap?.stop();
  economicSnap?.stop();
  oruSnap?.stop();
  liveSnap = null;
  economicSnap = null;
  oruSnap = null;
  cachesStarted = false;
}

/** Cheap height signal from the indexer tick. Does not scan economic tables. */
export function notifyIndexerHeight(height: number): void {
  if (!Number.isFinite(height) || height < 0) return;
  economicSnap?.noteHeight(height);
  oruSnap?.noteHeight(height);
  if (economicSnap?.view().stale) void economicSnap.refresh(false);
  if (oruSnap?.view().stale) void oruSnap.refresh(false);
}

export function readCachedStatus(now = Date.now()): StatusDashboard {
  const liveView = liveSnap?.view(now);
  const economicView = economicSnap?.view(now);
  return assembleStatusDashboard(liveView?.value ?? null, economicView?.value ?? null, {
    economic_status: economicView?.status ?? "loading",
    economic_generated_at: iso(economicView?.generatedAtMs ?? null),
    snapshot_height: economicView?.snapshotHeight ?? null,
    cache_age_seconds: economicView?.ageSeconds ?? null,
    stale: economicView?.value != null && economicView.stale,
    last_refresh_error: economicView?.lastError ?? liveView?.lastError ?? null,
    live_status: liveView?.status ?? "loading",
    live_generated_at: iso(liveView?.generatedAtMs ?? null),
    live_age_seconds: liveView?.ageSeconds ?? null,
  });
}

export type CachedOruEconomics =
  | {
      status: "ready";
      view: OruEconomicsView;
      cache: {
        status: "ready";
        generated_at: string | null;
        snapshot_height: number | null;
        cache_age_seconds: number | null;
        stale: boolean;
        last_refresh_error: string | null;
      };
    }
  | {
      status: "loading" | "refresh_failed";
      view: null;
      cache: {
        status: "loading" | "refresh_failed";
        generated_at: null;
        snapshot_height: null;
        cache_age_seconds: null;
        stale: false;
        last_refresh_error: string | null;
      };
    };

export function readCachedOruEconomics(now = Date.now()): CachedOruEconomics {
  const view = oruSnap?.view(now);
  if (!view || view.value == null) {
    const status = view?.status === "refresh_failed" ? "refresh_failed" : "loading";
    return {
      status,
      view: null,
      cache: {
        status,
        generated_at: null,
        snapshot_height: null,
        cache_age_seconds: null,
        stale: false,
        last_refresh_error: view?.lastError ?? null,
      },
    };
  }
  return {
    status: "ready",
    view: view.value,
    cache: {
      status: "ready",
      generated_at: view.generatedAtMs == null ? null : new Date(view.generatedAtMs).toISOString(),
      snapshot_height: view.snapshotHeight,
      cache_age_seconds: view.ageSeconds,
      stale: view.stale,
      last_refresh_error: view.lastError,
    },
  };
}

export function explorerCacheStats(): {
  economicRefreshCount: number;
  economicFailureCount: number;
  liveRefreshCount: number;
  oruRefreshCount: number;
  economicInflight: boolean;
} {
  const economic = economicSnap?.view();
  const live = liveSnap?.view();
  const oru = oruSnap?.view();
  return {
    economicRefreshCount: economic?.refreshCount ?? 0,
    economicFailureCount: economic?.failureCount ?? 0,
    liveRefreshCount: live?.refreshCount ?? 0,
    oruRefreshCount: oru?.refreshCount ?? 0,
    economicInflight: economic?.inflight ?? false,
  };
}

/** Test seam. Production startup uses startExplorerCaches. */
export function installExplorerCachesForTests(parts: {
  live?: SharedSnapshot<LiveStatusParts> | null;
  economic?: SharedSnapshot<EconomicAggregates> | null;
  oru?: SharedSnapshot<OruEconomicsView> | null;
}): void {
  stopExplorerCaches();
  liveSnap = parts.live ?? null;
  economicSnap = parts.economic ?? null;
  oruSnap = parts.oru ?? null;
  cachesStarted = true;
}
