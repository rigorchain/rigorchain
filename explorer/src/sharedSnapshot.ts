/**
 * One in-process snapshot shared by every HTTP request.
 * A refresh never queues behind itself, and a failed refresh leaves the last good value in place.
 */

export type SnapshotLoadResult<T> = {
  value: T;
  snapshotHeight: number | null;
};

export type SnapshotStatus = "ready" | "loading" | "refresh_failed";

export type SnapshotView<T> = {
  value: T | null;
  generatedAtMs: number | null;
  snapshotHeight: number | null;
  ageSeconds: number | null;
  stale: boolean;
  status: SnapshotStatus;
  lastError: string | null;
  refreshCount: number;
  failureCount: number;
  lastDurationMs: number | null;
  inflight: boolean;
};

export class SharedSnapshot<T> {
  private current: { value: T; generatedAtMs: number; snapshotHeight: number | null } | null = null;
  private lastError: string | null = null;
  private inflight: Promise<boolean> | null = null;
  private timer: ReturnType<typeof setInterval> | null = null;
  private started = false;
  private stopped = false;
  private lastAttemptMs = 0;
  private refreshCount = 0;
  private failureCount = 0;
  private lastDurationMs: number | null = null;
  private notedHeight: number | null = null;

  constructor(
    private readonly options: {
      intervalMs: number;
      minIntervalMs: number;
      load: () => Promise<SnapshotLoadResult<T>>;
      now?: () => number;
    },
  ) {}

  private now(): number {
    return this.options.now ? this.options.now() : Date.now();
  }

  /** Starts the single background refresh. A second call does not add another timer. */
  start(): void {
    if (this.started || this.stopped) return;
    this.started = true;
    void this.refresh(true);
    this.timer = setInterval(() => {
      void this.refresh(false);
    }, this.options.intervalMs);
    this.timer.unref?.();
  }

  /** Clears the timer. An in-flight load may finish, but it will not schedule another refresh. */
  stop(): void {
    this.stopped = true;
    this.started = true;
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
  }

  /**
   * Remember a cheap indexer height so readers can see staleness.
   * This does not run the expensive load.
   */
  noteHeight(height: number | null): void {
    if (height == null || !Number.isFinite(height) || height < 0) return;
    this.notedHeight = height;
  }

  view(nowMs = this.now()): SnapshotView<T> {
    const value = this.current?.value ?? null;
    const generatedAtMs = this.current?.generatedAtMs ?? null;
    const snapshotHeight = this.current?.snapshotHeight ?? null;
    const ageSeconds = generatedAtMs == null ? null : Math.max(0, Math.floor((nowMs - generatedAtMs) / 1000));
    const ageStale = ageSeconds != null && ageSeconds * 1000 > this.options.intervalMs;
    const heightStale =
      value != null &&
      snapshotHeight != null &&
      this.notedHeight != null &&
      this.notedHeight !== snapshotHeight;
    const failedWithValue = value != null && this.lastError != null;
    let status: SnapshotStatus = "loading";
    if (value != null) status = "ready";
    else if (this.lastError != null) status = "refresh_failed";
    return {
      value,
      generatedAtMs,
      snapshotHeight,
      ageSeconds,
      stale: value != null && (ageStale || heightStale || failedWithValue),
      status,
      lastError: this.lastError,
      refreshCount: this.refreshCount,
      failureCount: this.failureCount,
      lastDurationMs: this.lastDurationMs,
      inflight: this.inflight != null,
    };
  }

  /**
   * Run one load, or join the load already running.
   * `force` is only for the first start. Later calls respect the minimum interval
   * and do not pile up when a slow load is still running.
   */
  refresh(force = false): Promise<boolean> {
    if (this.stopped) return Promise.resolve(false);
    if (this.inflight) return this.inflight;
    const now = this.now();
    if (!force && this.lastAttemptMs !== 0 && now - this.lastAttemptMs < this.options.minIntervalMs) {
      return Promise.resolve(false);
    }
    this.lastAttemptMs = now;
    this.inflight = this.execute();
    return this.inflight;
  }

  private async execute(): Promise<boolean> {
    const started = this.now();
    try {
      const loaded = await this.options.load();
      if (!this.stopped) {
        this.current = {
          value: loaded.value,
          generatedAtMs: this.now(),
          snapshotHeight: loaded.snapshotHeight,
        };
        this.lastError = null;
        this.refreshCount += 1;
      }
      this.lastDurationMs = this.now() - started;
      return !this.stopped;
    } catch (error) {
      this.failureCount += 1;
      this.lastError = error instanceof Error ? error.message : String(error);
      this.lastDurationMs = this.now() - started;
      return false;
    } finally {
      this.inflight = null;
    }
  }
}
