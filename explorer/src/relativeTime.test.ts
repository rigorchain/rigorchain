import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { formatBlockHeaderTimeUtc, formatRelativeTime } from "./relativeTime";

const NOW = 1_793_103_349;

describe("formatRelativeTime", () => {
  it("labels a past timestamp with the existing ago wording", () => {
    assert.equal(formatRelativeTime(NOW - 7 * 60, NOW), "7 minutes ago");
    assert.equal(formatRelativeTime(NOW - 4, NOW), "just now");
    assert.equal(formatRelativeTime(NOW - 5, NOW), "5 seconds ago");
    assert.equal(formatRelativeTime(NOW - 3600, NOW), "1 hour ago");
  });

  it("labels a timestamp equal to the reference clock as just now", () => {
    assert.equal(formatRelativeTime(NOW, NOW), "just now");
  });

  it("does not collapse a slightly future timestamp into just now", () => {
    assert.equal(formatRelativeTime(NOW + 1, NOW), "in 1 second");
    assert.equal(formatRelativeTime(NOW + 4, NOW), "in 4 seconds");
    assert.notEqual(formatRelativeTime(NOW + 1, NOW), "just now");
  });

  it("labels a substantially future timestamp in minutes and hours", () => {
    assert.equal(formatRelativeTime(NOW + 16 * 60, NOW), "in 16 minutes");
    assert.equal(formatRelativeTime(NOW + 3600, NOW), "in 1 hour");
    assert.equal(formatRelativeTime(NOW + 2 * 3600, NOW), "in 2 hours");
  });

  it("follows a timestamp from the future into the past as the clock advances", () => {
    const header = NOW + 90;
    assert.equal(formatRelativeTime(header, NOW), "in 1 minute");
    assert.equal(formatRelativeTime(header, header), "just now");
    assert.equal(formatRelativeTime(header, header + 7 * 60), "7 minutes ago");
  });

  it("formats the original header time in UTC without shifting it", () => {
    assert.equal(formatBlockHeaderTimeUtc(1793098978), "2026-10-27 11:02:58 UTC");
    assert.equal(formatBlockHeaderTimeUtc(1793104113), "2026-10-27 12:28:33 UTC");
    assert.equal(formatBlockHeaderTimeUtc(1793102138), "2026-10-27 11:55:38 UTC");
    assert.equal(formatBlockHeaderTimeUtc(1793102162), "2026-10-27 11:56:02 UTC");
    assert.equal(formatBlockHeaderTimeUtc(0), "");
    assert.equal(formatRelativeTime("not-a-time", NOW), "—");
  });
});
