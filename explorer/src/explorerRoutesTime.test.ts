import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { runInNewContext } from "node:vm";

type TimeAgo = (unixSeconds: number, nowSeconds?: number) => string;
type Routes = {
  timeAgo: TimeAgo;
  timeAbsolute: (unixSeconds: number) => string;
  blockTimeDetailHtml: (unixSeconds: number, nowSeconds?: number) => string;
  timeCellHtml: (unixSeconds: number, nowSeconds?: number) => string;
};

function loadRoutes(): Routes {
  const code = readFileSync(join(__dirname, "../public/explorer-routes.js"), "utf8");
  const window: { ExplorerRoutes?: Routes } = {};
  runInNewContext(code, { window, Date, Number, Math }, { filename: "explorer-routes.js" });
  const routes = window.ExplorerRoutes;
  if (!routes?.timeAgo || !routes.timeAbsolute || !routes.blockTimeDetailHtml || !routes.timeCellHtml) {
    throw new Error("ExplorerRoutes time helpers were not installed");
  }
  return routes;
}

const { timeAgo, timeAbsolute, blockTimeDetailHtml, timeCellHtml } = loadRoutes();
const NOW = 1_793_103_349;

describe("explorer-routes timeAgo", () => {
  it("labels a past timestamp with the existing ago wording", () => {
    assert.equal(timeAgo(NOW - 7 * 60, NOW), "7 minutes ago");
    assert.equal(timeAgo(NOW - 4, NOW), "just now");
    assert.equal(timeAgo(NOW - 5, NOW), "5 seconds ago");
    assert.equal(timeAgo(NOW - 3600, NOW), "1 hour ago");
    assert.equal(timeAgo(NOW - 40 * 86400, NOW), "1 month ago");
  });

  it("labels a timestamp equal to the reference clock as just now", () => {
    assert.equal(timeAgo(NOW, NOW), "just now");
  });

  it("does not collapse a slightly future timestamp into just now", () => {
    assert.equal(timeAgo(NOW + 1, NOW), "in 1 second");
    assert.equal(timeAgo(NOW + 4, NOW), "in 4 seconds");
    assert.notEqual(timeAgo(NOW + 1, NOW), "just now");
  });

  it("labels a substantially future timestamp in minutes and hours", () => {
    assert.equal(timeAgo(NOW + 16 * 60, NOW), "in 16 minutes");
    assert.equal(timeAgo(NOW + 3600, NOW), "in 1 hour");
    assert.equal(timeAgo(NOW + 2 * 3600, NOW), "in 2 hours");
  });

  it("follows a timestamp from the future into the past as the clock advances", () => {
    const header = NOW + 90;
    assert.equal(timeAgo(header, NOW), "in 1 minute");
    assert.equal(timeAgo(header, header), "just now");
    assert.equal(timeAgo(header, header + 7 * 60), "7 minutes ago");
  });

  it("keeps the original UTC header time and treats relative time as a supplement", () => {
    assert.equal(timeAbsolute(1793104113), "2026-10-27 12:28:33 UTC");
    assert.equal(timeAbsolute(1793102138), "2026-10-27 11:55:38 UTC");
    const detail = blockTimeDetailHtml(NOW + 16 * 60, NOW);
    assert.match(detail, new RegExp("^" + timeAbsolute(NOW + 16 * 60).replace(/[.*+?^${}()|[\]\\]/g, "\\$&")));
    assert.match(detail, /in 16 minutes/);
    assert.doesNotMatch(detail, /timestamp recorded in the block/);
    assert.doesNotMatch(detail, /just now/);
    const cell = timeCellHtml(NOW + 1, NOW);
    assert.match(cell, /title="[^"]+ UTC"/);
    assert.match(cell, />in 1 second</);
    assert.doesNotMatch(cell, /just now/);
  });
});
