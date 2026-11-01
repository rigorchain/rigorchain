import { describe, it } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { join } from "node:path";
import { runInNewContext } from "node:vm";

const HASHRATE_NOTE =
  "Estimated from actual block proof-of-work over the latest 50 block intervals. This is not a direct measurement of miner hardware.";

function readPublic(name: string): string {
  return readFileSync(join(__dirname, "../public", name), "utf8");
}

function classList() {
  const names = new Set<string>();
  return {
    add(...items: string[]) {
      for (const item of items) names.add(item);
    },
    remove(...items: string[]) {
      for (const item of items) names.delete(item);
    },
    contains(item: string) {
      return names.has(item);
    },
  };
}

type Root = {
  attrs: Record<string, string>;
  textWrites: string[];
  innerHTML: string;
  textContent: string;
  getAttribute(name: string): string | null;
  setAttribute(name: string, value: string): void;
  removeAttribute(name: string): void;
  querySelector(sel: string): object | null;
};

function makeRoot(html = ""): Root {
  const root: Root = {
    attrs: {},
    textWrites: [],
    innerHTML: html,
    get textContent() {
      return this.innerHTML;
    },
    set textContent(value: string) {
      this.textWrites.push(value);
      this.innerHTML = value;
    },
    getAttribute(name: string) {
      return Object.prototype.hasOwnProperty.call(this.attrs, name) ? this.attrs[name] : null;
    },
    setAttribute(name: string, value: string) {
      this.attrs[name] = value;
    },
    removeAttribute(name: string) {
      delete this.attrs[name];
    },
    querySelector(sel: string) {
      return sel === ".status-grid" && this.innerHTML.includes("status-grid") ? { ok: true } : null;
    },
  };
  return root;
}

type Timer = { fn: () => void; ms: number };

function installBrowser(opts?: { visibility?: string; rootHtml?: string; ssr?: boolean }) {
  const root = makeRoot(opts?.rootHtml ?? "");
  if (opts?.ssr) root.attrs["data-ssr"] = "1";
  const elements = new Map<string, Record<string, unknown>>();
  elements.set("status-root", root as unknown as Record<string, unknown>);
  const listeners = new Map<string, Array<(event?: { persisted?: boolean }) => void>>();
  let visibility = opts?.visibility ?? "visible";
  let reloads = 0;
  const fetches: string[] = [];
  const timers = new Map<number, Timer>();
  let nextTimer = 1;
  let statusBody: Record<string, unknown> = {
    block_health: {
      best_block_hash: "aa".repeat(32),
      network_hashrate: {
        available: true,
        display: "136.85 GH/s",
        note: HASHRATE_NOTE,
      },
    },
  };
  let releaseFetch: (() => void) | null = null;
  let holdFetch = false;

  function addListener(type: string, fn: (event?: { persisted?: boolean }) => void) {
    const list = listeners.get(type) ?? [];
    list.push(fn);
    listeners.set(type, list);
  }
  function removeListener(type: string, fn: (event?: { persisted?: boolean }) => void) {
    listeners.set(
      type,
      (listeners.get(type) ?? []).filter((item) => item !== fn),
    );
  }

  const document = {
    visibilityState: "visible",
    readyState: "complete",
    body: {
      classList: classList(),
      firstChild: null,
      insertBefore(node: { id?: string }) {
        if (node.id) elements.set(node.id, node as Record<string, unknown>);
        return node;
      },
      appendChild(node: { id?: string }) {
        if (node.id) elements.set(node.id, node as Record<string, unknown>);
        return node;
      },
    },
    getElementById(id: string) {
      return elements.get(id) ?? null;
    },
    createElement(tag: string) {
      return { tag, id: "", innerHTML: "", classList: classList(), setAttribute() {}, parentNode: { removeChild() {} } };
    },
    addEventListener: addListener,
    removeEventListener: removeListener,
    querySelector() {
      return null;
    },
  };
  Object.defineProperty(document, "visibilityState", {
    get() {
      return visibility;
    },
  });

  const sandbox: Record<string, unknown> = {
    document,
    AbortSignal,
    Date,
    Number,
    Math,
    String,
    Object,
    Promise,
    setInterval(fn: () => void, ms: number) {
      const id = nextTimer++;
      timers.set(id, { fn, ms });
      return id;
    },
    clearInterval(id: number) {
      timers.delete(id);
    },
    setTimeout(fn: () => void) {
      fn();
      return 0;
    },
    clearTimeout() {},
    fetch(url: string) {
      fetches.push(String(url));
      const response = {
        ok: true,
        status: 200,
        json: async () => (String(url).includes("compliance") ? {} : statusBody),
      };
      if (holdFetch && !String(url).includes("compliance")) {
        return new Promise((resolve) => {
          releaseFetch = () => resolve(response);
        });
      }
      return Promise.resolve(response);
    },
    location: {
      reload() {
        reloads += 1;
      },
    },
    addEventListener: addListener,
    removeEventListener: removeListener,
  };
  sandbox.window = sandbox;
  sandbox.ExplorerRoutes = {
    fetchJson(url: string) {
      return (sandbox.fetch as (url: string) => Promise<unknown>)(url);
    },
  };

  return {
    sandbox,
    root,
    timers,
    fetches,
    reloads: () => reloads,
    listenerCount: (type: string) => (listeners.get(type) ?? []).length,
    setVisibility(next: string) {
      visibility = next;
      for (const fn of listeners.get("visibilitychange") ?? []) fn();
    },
    emit(type: string, event?: { persisted?: boolean }) {
      for (const fn of listeners.get(type) ?? []) fn(event);
    },
    setHold(hold: boolean) {
      holdFetch = hold;
    },
    release() {
      const done = releaseFetch;
      releaseFetch = null;
      if (done) done();
    },
    loadStatus(code: string) {
      runInNewContext(code, sandbox, { filename: "status.js" });
      return sandbox.StatusDashboard as {
        load: (opts?: { preserve?: boolean }) => Promise<void>;
        renderDashboard: (data: Record<string, unknown>) => string;
        startAutoRefresh: () => void;
        stopAutoRefresh: () => void;
        HOME_REFRESH_MS: number;
      };
    },
  };
}

describe("homepage status refresh", () => {
  const statusSource = readPublic("status.js");
  const indexSource = readPublic("index.html");

  it("refreshes the existing dashboard every 60 seconds without a page reload", async () => {
    const browser = installBrowser();
    const dashboard = browser.loadStatus(statusSource);
    assert.equal(dashboard.HOME_REFRESH_MS, 60000);
    dashboard.startAutoRefresh();
    dashboard.startAutoRefresh();
    assert.equal(browser.timers.size, 1);
    assert.equal(browser.listenerCount("visibilitychange"), 1);
    const timer = [...browser.timers.values()][0];
    assert.equal(timer?.ms, 60000);

    await dashboard.load();
    assert.match(browser.root.innerHTML, /Chain identity/);
    assert.match(browser.root.innerHTML, /Block production/);
    assert.match(browser.root.innerHTML, /Indexer integrity/);
    assert.match(browser.root.innerHTML, /Issuance/);
    assert.ok(browser.root.innerHTML.indexOf("Best block") < browser.root.innerHTML.indexOf("Network Hashrate (50 blocks)"));
    assert.match(browser.root.innerHTML, /136\.85 GH\/s/);
    assert.match(browser.root.innerHTML, /not a direct measurement of miner hardware/);
    browser.root.textWrites.length = 0;
    const before = browser.fetches.length;
    timer?.fn();
    await Promise.resolve();
    await Promise.resolve();
    assert.ok(browser.fetches.length > before);
    assert.equal(browser.fetches.filter((url) => url === "/api/status").length >= 2, true);
    assert.deepEqual(browser.root.textWrites, []);
    assert.equal(browser.reloads(), 0);
    assert.equal(browser.timers.size, 1);
  });

  it("stops polling while hidden and resumes a single timer when visible", () => {
    const browser = installBrowser();
    const dashboard = browser.loadStatus(statusSource);
    dashboard.startAutoRefresh();
    browser.setVisibility("hidden");
    assert.equal(browser.timers.size, 0);
    browser.setVisibility("visible");
    assert.equal(browser.timers.size, 1);
    browser.setVisibility("visible");
    assert.equal(browser.timers.size, 1);
  });

  it("does not arm a timer for a document that is already hidden", () => {
    const browser = installBrowser({ visibility: "hidden" });
    const dashboard = browser.loadStatus(statusSource);
    dashboard.startAutoRefresh();
    assert.equal(browser.timers.size, 0);
    browser.setVisibility("visible");
    assert.equal(browser.timers.size, 1);
  });

  it("clears timers and listeners when the page is destroyed", () => {
    const browser = installBrowser();
    const dashboard = browser.loadStatus(statusSource);
    dashboard.startAutoRefresh();
    browser.emit("pagehide");
    assert.equal(browser.timers.size, 0);
    browser.emit("pageshow", { persisted: true });
    assert.equal(browser.timers.size, 1);
    dashboard.stopAutoRefresh();
    assert.equal(browser.timers.size, 0);
    assert.equal(browser.listenerCount("visibilitychange"), 0);
    browser.setVisibility("hidden");
    browser.setVisibility("visible");
    assert.equal(browser.timers.size, 0);
  });

  it("keeps a manual load working after automatic polling has stopped", async () => {
    const browser = installBrowser();
    const dashboard = browser.loadStatus(statusSource);
    dashboard.startAutoRefresh();
    dashboard.stopAutoRefresh();
    const before = browser.fetches.length;
    await dashboard.load();
    assert.ok(browser.fetches.length > before);
    assert.match(browser.root.innerHTML, /136\.85 GH\/s/);
    assert.equal(browser.reloads(), 0);
  });

  it("does not start a second status request while one is in flight", async () => {
    const browser = installBrowser();
    browser.setHold(true);
    const dashboard = browser.loadStatus(statusSource);
    const first = dashboard.load();
    const second = dashboard.load();
    assert.equal(browser.fetches.length, 1);
    browser.release();
    await first;
    await second;
    assert.equal(browser.fetches.length, 1);
  });

  it("shows Unavailable instead of zero when the estimate is missing", () => {
    const browser = installBrowser();
    const dashboard = browser.loadStatus(statusSource);
    const html = dashboard.renderDashboard({ block_health: { network_hashrate: { available: false, display: "0 H/s" } } });
    assert.match(html, /Network Hashrate \(50 blocks\)/);
    assert.match(html, />Unavailable</);
    assert.doesNotMatch(html, />0 H\/s</);
    assert.match(html, /Chain identity/);
    assert.match(html, /Block production/);
    assert.match(html, /Indexer integrity/);
    assert.match(html, /Issuance/);
  });

  it("keeps the homepage shell free of automatic full-page reloads", () => {
    assert.match(indexSource, /StatusDashboard\.startAutoRefresh\(\)/);
    assert.doesNotMatch(indexSource, /setInterval/);
    assert.doesNotMatch(indexSource, /location\.reload/);
    assert.doesNotMatch(indexSource, /http-equiv="refresh"/);
    assert.doesNotMatch(statusSource, /location\.reload/);
    assert.match(statusSource, /HOME_REFRESH_MS = 60000/);
  });
});

describe("compliance polling on the homepage", () => {
  it("pauses the existing 30 second banner poll while the document is hidden", () => {
    const browser = installBrowser();
    runInNewContext(readPublic("compliance.js"), browser.sandbox, { filename: "compliance.js" });
    const poll = browser.sandbox.CompliancePoll as { start: () => void; stop: () => void; POLL_MS: number };
    assert.equal(poll.POLL_MS, 30000);
    assert.equal(browser.timers.size, 1);
    assert.equal([...browser.timers.values()][0]?.ms, 30000);
    poll.start();
    assert.equal(browser.timers.size, 1);
    browser.setVisibility("hidden");
    assert.equal(browser.timers.size, 0);
    browser.setVisibility("visible");
    assert.equal(browser.timers.size, 1);
    browser.emit("pagehide");
    assert.equal(browser.timers.size, 0);
    poll.stop();
    browser.setVisibility("visible");
    assert.equal(browser.timers.size, 0);
    assert.equal(browser.reloads(), 0);
  });
});
