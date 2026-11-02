import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { SharedSnapshot } from "./sharedSnapshot";

function delay(ms: number): Promise<void> {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

describe("shared snapshot", () => {
  it("serves one load to concurrent callers and keeps that value", async () => {
    let calls = 0;
    let release: () => void = () => {};
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    const snap = new SharedSnapshot<number>({
      intervalMs: 60_000,
      minIntervalMs: 30_000,
      load: async () => {
        calls += 1;
        await gate;
        return { value: calls, snapshotHeight: 9 };
      },
    });
    const pending = Promise.all(Array.from({ length: 100 }, () => snap.refresh(true)));
    await delay(20);
    assert.equal(calls, 1);
    assert.equal(snap.view().inflight, true);
    assert.equal(snap.view().value, null);
    release();
    const results = await pending;
    assert.equal(results.every(Boolean), true);
    assert.equal(calls, 1);
    assert.equal(snap.view().value, 1);
    assert.equal(snap.view().snapshotHeight, 9);
    assert.equal(snap.view().status, "ready");
    assert.equal(snap.view().refreshCount, 1);
  });

  it("lets readers keep the previous snapshot while one refresh is running", async () => {
    let phase = 1;
    let release: () => void = () => {};
    const gate = new Promise<void>((resolve) => {
      release = resolve;
    });
    const snap = new SharedSnapshot<string>({
      intervalMs: 1_000,
      minIntervalMs: 0,
      load: async () => {
        if (phase === 1) return { value: "first", snapshotHeight: 1 };
        await gate;
        return { value: "second", snapshotHeight: 2 };
      },
    });
    await snap.refresh(true);
    phase = 2;
    const refreshing = snap.refresh(true);
    assert.equal(snap.view().value, "first");
    assert.equal(snap.view().inflight, true);
    release();
    await refreshing;
    assert.equal(snap.view().value, "second");
    assert.equal(snap.view().snapshotHeight, 2);
  });

  it("coalesces an expired refresh into one load", async () => {
    let now = 0;
    let calls = 0;
    const snap = new SharedSnapshot<number>({
      intervalMs: 45_000,
      minIntervalMs: 30_000,
      now: () => now,
      load: async () => {
        calls += 1;
        return { value: calls, snapshotHeight: calls };
      },
    });
    await snap.refresh(true);
    now = 45_000;
    await Promise.all([snap.refresh(false), snap.refresh(false), snap.refresh(false)]);
    assert.equal(calls, 2);
    assert.equal(snap.view().value, 2);
  });

  it("keeps the last good snapshot when a refresh fails and does not retry immediately", async () => {
    let now = 10_000;
    let calls = 0;
    const snap = new SharedSnapshot<string>({
      intervalMs: 45_000,
      minIntervalMs: 30_000,
      now: () => now,
      load: async () => {
        calls += 1;
        if (calls > 1) throw new Error("db down");
        return { value: "kept", snapshotHeight: 4 };
      },
    });
    assert.equal(await snap.refresh(true), true);
    assert.equal(await snap.refresh(false), false);
    assert.equal(calls, 1);
    now += 30_000;
    assert.equal(await snap.refresh(false), false);
    assert.equal(calls, 2);
    const view = snap.view();
    assert.equal(view.value, "kept");
    assert.equal(view.status, "ready");
    assert.equal(view.stale, true);
    assert.equal(view.snapshotHeight, 4);
    assert.match(view.lastError ?? "", /db down/);
    assert.equal(view.failureCount, 1);
  });

  it("does not invent a zero value on a cold or failed start", async () => {
    const snap = new SharedSnapshot<number>({
      intervalMs: 45_000,
      minIntervalMs: 30_000,
      load: async () => {
        throw new Error("not ready");
      },
    });
    assert.equal(snap.view().value, null);
    assert.equal(snap.view().status, "loading");
    assert.equal(await snap.refresh(true), false);
    assert.equal(snap.view().value, null);
    assert.equal(snap.view().status, "refresh_failed");
    assert.match(snap.view().lastError ?? "", /not ready/);
  });

  it("reports age and marks a newer indexed height stale without loading", async () => {
    let now = 5_000;
    const snap = new SharedSnapshot<{ ok: boolean }>({
      intervalMs: 45_000,
      minIntervalMs: 30_000,
      now: () => now,
      load: async () => ({ value: { ok: true }, snapshotHeight: 12 }),
    });
    await snap.refresh(true);
    now = 17_000;
    assert.equal(snap.view().ageSeconds, 12);
    assert.equal(snap.view().stale, false);
    assert.equal(snap.view().snapshotHeight, 12);
    snap.noteHeight(13);
    assert.equal(snap.view().stale, true);
    assert.equal(snap.view().refreshCount, 1);
  });

  it("starts one timer and stops it", async () => {
    let calls = 0;
    const snap = new SharedSnapshot<number>({
      intervalMs: 40,
      minIntervalMs: 30,
      load: async () => {
        calls += 1;
        return { value: calls, snapshotHeight: 1 };
      },
    });
    snap.start();
    snap.start();
    await delay(15);
    assert.equal(calls, 1);
    snap.stop();
    await delay(90);
    assert.equal(calls, 1);
    snap.stop();
  });
});
