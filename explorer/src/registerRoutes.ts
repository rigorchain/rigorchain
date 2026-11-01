import type { Express, Request, Response, NextFunction } from "express";
import type { Pool } from "pg";
import path from "node:path";
import { addressMatchesExplorerHrp, parseRigorAddress } from "./addressIntelligence";
import { canonicalUrlForResolution, loadBlockByRouteParam, resolveSearchInput, WTXID_INFORMATIONAL } from "./searchResolve";
import { getAddressIntelligence } from "./addressDb";
import { getLineageForTxid } from "./lineageDb";
import {
  getOrCreateChainState,
  getTxByTxid,
  getTxidByWtxid,
  listBlocks,
  countBlocks,
  listResEvents,
  listResMints,
} from "./db";
import type { Config } from "./config";
import { buildStatusDashboard } from "./api/status";
import { buildOruEconomicsView } from "./api/oruEconomics";
import { blockListPageMeta, parseBlockListQuery } from "./blocksList";
import {
  formatDerivedMaxSupply,
  formatRateOruPerRes,
  resIssuanceSchedulePayload,
  resDestroyCostForHeight,
} from "./resIssuanceSchedule";
import { atomsToResDisplay } from "./amount";
import {
  escapeHtml,
  injectSsrResPage,
  injectSsrRoot,
  readPublicTemplate,
  renderBlockRoot,
  renderBlocksRoot,
  renderResPageSsr,
  renderStatusRoot,
  renderTxRoot,
} from "./uiSsr";

function sendPublic(res: Response, publicDir: string, file: string): void {
  res.sendFile(path.join(publicDir, file));
}

/** Browser document navigation vs programmatic fetch (wallet, curl, in-page fetch). */
export function wantsHtmlPage(req: Request): boolean {
  if (req.query.json === "1" || req.query.format === "json") return false;
  if (req.query.limit !== undefined) return false;
  if (req.query.eligible_only !== undefined) return false;

  const mode = req.get("Sec-Fetch-Mode");
  const dest = req.get("Sec-Fetch-Dest");
  if (mode === "navigate" || dest === "document") return true;

  const accept = req.headers.accept ?? "";
  const htmlIdx = accept.indexOf("text/html");
  const jsonIdx = accept.indexOf("application/json");
  if (htmlIdx !== -1 && (jsonIdx === -1 || htmlIdx < jsonIdx)) return true;

  return false;
}

export function registerApiRoutes(app: Express, pool: Pool, config: Config): void {
  const mountApi = (prefix: string) => {
    app.get(`${prefix}/blocks`, async (req, res) => {
      const query = parseBlockListQuery(req.query as Record<string, unknown>);
      const total = await countBlocks(pool);
      const page = blockListPageMeta(query, total);
      const rows = await listBlocks(pool, page.limit, page.offset);
      res.json({ blocks: rows, ...page });
    });

    app.get(`${prefix}/block/:hash`, async (req, res) => {
      const loaded = await loadBlockByRouteParam(pool, req.params.hash);
      if (!loaded) return res.status(404).json({ error: "block not found" });
      res.json(loaded.row);
    });

    app.get(`${prefix}/tx/:txid`, async (req, res) => {
      const txid = req.params.txid.trim().toLowerCase();
      const row = await getTxByTxid(pool, txid);
      if (row) {
        res.json(row);
        return;
      }
      const canonical = await getTxidByWtxid(pool, txid);
      if (canonical) {
        return res.status(400).json({ error: WTXID_INFORMATIONAL, txid: canonical });
      }
      return res.status(404).json({ error: "tx not found" });
    });

    app.get(`${prefix}/lineage/:txid`, async (req, res) => {
      const txid = req.params.txid;
      const row = await getLineageForTxid(pool, txid);
      if (!row) return res.status(404).json({ error: "transaction not found" });
      if (!row.visible) return res.status(404).json({ error: "no lineage for transaction" });
      res.json(row);
    });

    app.get(`${prefix}/address/:addr`, async (req, res) => {
      const addr = decodeURIComponent(req.params.addr);
      if (!parseRigorAddress(addr)) return res.status(400).json({ error: "invalid address format" });
      if (!addressMatchesExplorerHrp(addr, config.bech32Hrp)) {
        return res.status(400).json({ error: "address does not match this explorer network" });
      }
      const page = Math.max(1, Number(req.query.page ?? 1));
      const limit = Math.min(Math.max(1, Number(req.query.limit ?? 50)), 200);
      const row = await getAddressIntelligence(pool, config, addr, page, limit);
      if (!row) return res.status(400).json({ error: "invalid address format" });
      res.json(row);
    });

    app.get(`${prefix}/res/mints`, async (req, res) => {
      const limit = Math.min(Number(req.query.limit ?? 50), 200);
      const rows = await listResMints(pool, limit);
      res.json({ mints: rows });
    });

    app.get(`${prefix}/res/schedule`, (_req, res) => {
      res.json(resIssuanceSchedulePayload());
    });

    app.get(`${prefix}/res/events`, async (req, res) => {
      const limit = Math.min(Number(req.query.limit ?? 200), 500);
      const eligibleOnly = req.query.eligible_only === "1" || req.query.eligible_only === "true";
      const rows = await listResEvents(pool, limit, eligibleOnly);
      res.json({ events: rows });
    });

    app.get(`${prefix}/status`, async (_req, res) => {
      try {
        const dashboard = await buildStatusDashboard(config, pool);
        res.json(dashboard);
      } catch (e) {
        res.status(500).json({ error: String(e) });
      }
    });

    app.get(`${prefix}/oru/economics`, async (_req, res) => {
      try {
        const view = await buildOruEconomicsView(config, pool);
        res.json(view);
      } catch (e) {
        res.status(500).json({ error: String(e) });
      }
    });

    app.get(`${prefix}/chain/tip`, async (_req, res) => {
      const chain = await getOrCreateChainState(pool, config);
      const stats = await pool.query(
        `
          SELECT
            (SELECT cumulative_convergence FROM blocks WHERE height = $1 LIMIT 1) AS cumulative_convergence,
            (SELECT COALESCE(SUM(mint_amount),0) FROM res_mints rm JOIN blocks b ON b.block_hash = rm.block_hash WHERE b.height <= $1) AS res_mint_total_atoms,
            (SELECT burned_oru::text FROM rfc018_fee_burn_stats WHERE height = $1 LIMIT 1) AS rfc018_block_burned_oru,
            (SELECT cumulative_burned_oru::text FROM rfc018_fee_burn_stats WHERE height = $1 LIMIT 1) AS rfc018_cumulative_burned_oru
        `,
        [chain.last_height],
      );
      const row = stats.rows[0] ?? {};
      const blockBurn = row.rfc018_block_burned_oru != null ? String(row.rfc018_block_burned_oru) : "0";
      const cumBurn = row.rfc018_cumulative_burned_oru != null ? String(row.rfc018_cumulative_burned_oru) : "0";
      res.json({
        chain_state: chain,
        stats: row,
        rfc018_fee_burn: {
          block_burned_oru: blockBurn,
          cumulative_burned_oru: cumBurn,
          indexed: row.rfc018_block_burned_oru != null,
        },
      });
    });
  };

  mountApi("/api");
  // Legacy JSON paths (wallet-observer and scripts).
  mountApi("");
}

function sendSsrHtml(res: Response, html: string): void {
  res.setHeader("Cache-Control", "no-store");
  res.setHeader("Content-Type", "text/html; charset=utf-8");
  res.send(html);
}

export function registerUiRoutes(app: Express, pool: Pool, publicDir: string, config?: Config): void {
  // ---- Legacy HTML redirects (CU-03) ----
  app.get("/index.html", (_req, res) => res.redirect(301, "/"));
  app.get("/blocks.html", (_req, res) => res.redirect(301, "/blocks"));
  app.get("/search.html", (req, res) => {
    const q = typeof req.query.q === "string" ? req.query.q : "";
    if (q) return res.redirect(301, `/search?q=${encodeURIComponent(q)}`);
    res.redirect(301, "/search");
  });
  app.get("/status.html", (_req, res) => res.redirect(301, "/"));

  app.get("/address.html", (req, res) => {
    const addr = typeof req.query.addr === "string" ? req.query.addr : "";
    if (addr) return res.redirect(301, `/address/${encodeURIComponent(addr)}`);
    res.redirect(301, "/search");
  });

  app.get("/block.html", (req, res) => {
    const hash = typeof req.query.hash === "string" ? req.query.hash : "";
    if (!hash) return res.redirect(301, "/blocks");
    res.redirect(301, `/block/${encodeURIComponent(hash)}`);
  });

  app.get("/tx.html", (req, res) => {
    if (req.query.wtxid && !req.query.txid) {
      return res
        .status(400)
        .send("wtxid is informational only. Use /tx/{txid} with canonical txid.");
    }
    const txid = typeof req.query.txid === "string" ? req.query.txid : "";
    if (!txid) return res.redirect(301, "/search");
    res.redirect(301, `/tx/${encodeURIComponent(txid)}`);
  });

  const htmlOrNext =
    (file: string) => (req: Request, res: Response, next: NextFunction) => {
      if (wantsHtmlPage(req)) return sendPublic(res, publicDir, file);
      next();
    };

  // ---- Canonical UI pages (registered before JSON handlers on same paths) ----
  app.get("/blocks", async (req, res, next) => {
    if (!wantsHtmlPage(req)) return next();
    try {
      const query = parseBlockListQuery(req.query as Record<string, unknown>);
      const total = await countBlocks(pool);
      const page = blockListPageMeta(query, total);
      const rows = await listBlocks(pool, page.limit, page.offset);
      const html = injectSsrRoot(
        readPublicTemplate(publicDir, "blocks.html"),
        renderBlocksRoot(rows, page),
      );
      sendSsrHtml(res, html);
    } catch (e) {
      console.error("SSR /blocks failed:", e);
      sendPublic(res, publicDir, "blocks.html");
    }
  });

  app.get("/block/:hash", async (req, res, next) => {
    if (!wantsHtmlPage(req)) return next();
    const param = req.params.hash;
    try {
      const loaded = await loadBlockByRouteParam(pool, param);
      if (!loaded) {
        const html = injectSsrRoot(
          readPublicTemplate(publicDir, "block.html"),
          "<p>Block not found.</p>",
        );
        res.status(404);
        sendSsrHtml(res, html);
        return;
      }
      if (loaded.hash !== param.trim().toLowerCase()) {
        return res.redirect(302, `/block/${encodeURIComponent(loaded.hash)}`);
      }
      const html = injectSsrRoot(readPublicTemplate(publicDir, "block.html"), renderBlockRoot(loaded.row, loaded.hash));
      sendSsrHtml(res, html);
    } catch (e) {
      console.error("SSR /block failed:", e);
      sendPublic(res, publicDir, "block.html");
    }
  });

  app.get("/tx/:txid", async (req, res, next) => {
    if (!wantsHtmlPage(req)) return next();
    const txid = req.params.txid.trim().toLowerCase();
    try {
      const row = await getTxByTxid(pool, txid);
      if (row) {
        const html = injectSsrRoot(readPublicTemplate(publicDir, "tx.html"), renderTxRoot(row, txid));
        sendSsrHtml(res, html);
        return;
      }
      const canonical = await getTxidByWtxid(pool, txid);
      if (canonical) {
        const html = injectSsrRoot(
          readPublicTemplate(publicDir, "tx.html"),
          `<p>${escapeHtml(WTXID_INFORMATIONAL)}</p><p>Canonical txid: ${escapeHtml(canonical)}</p>`,
        );
        res.status(400);
        sendSsrHtml(res, html);
        return;
      }
      const html = injectSsrRoot(readPublicTemplate(publicDir, "tx.html"), "<p>Transaction not found.</p>");
      res.status(404);
      sendSsrHtml(res, html);
    } catch (e) {
      console.error("SSR /tx failed:", e);
      sendPublic(res, publicDir, "tx.html");
    }
  });

  app.get("/address", htmlOrNext("address.html"));
  app.get("/address/:addr", htmlOrNext("address.html"));
  app.get("/status", (_req, res) => res.redirect(301, "/"));

  app.get(["/res", "/res.html"], async (req, res, next) => {
    if (!wantsHtmlPage(req)) return next();
    try {
      const [mints, totals] = await Promise.all([
        listResMints(pool, 15),
        pool.query<{ n: number; atoms: string }>(
          `SELECT COUNT(*)::int AS n, COALESCE(SUM(mint_amount), 0)::text AS atoms FROM res_mints`,
        ),
      ]);
      let confirmHeight: number | null = null;
      if (config) {
        const chain = await getOrCreateChainState(pool, config);
        if (Number.isFinite(Number(chain.last_height))) {
          confirmHeight = Number(chain.last_height) + 1;
        }
      }
      const eco: Record<string, unknown> = {
        confirm_height: confirmHeight,
        res_destroy_cost_display:
          confirmHeight != null ? formatRateOruPerRes(resDestroyCostForHeight(confirmHeight)) : null,
        res_derived_max_supply_display: formatDerivedMaxSupply(),
        network_res_minted_display: atomsToResDisplay(totals.rows[0]?.atoms ?? "0"),
        res_mint_count: totals.rows[0]?.n ?? 0,
        last_res_height: mints[0]?.block_height ?? null,
        last_res_mint_txid: mints[0]?.mint_txid ?? null,
      };
      const { curvesHtml, rootHtml } = renderResPageSsr({ confirmHeight, eco, mints });
      sendSsrHtml(res, injectSsrResPage(readPublicTemplate(publicDir, "res.html"), curvesHtml, rootHtml));
    } catch (e) {
      console.error("SSR /res failed:", e);
      sendPublic(res, publicDir, "res.html");
    }
  });

  if (config) {
    app.get("/", async (req, res, next) => {
      if (!wantsHtmlPage(req) && (req.headers.accept ?? "").includes("application/json")) return next();
      try {
        const dashboard = await buildStatusDashboard(config, pool);
        const html = injectSsrRoot(
          readPublicTemplate(publicDir, "index.html"),
          renderStatusRoot(dashboard as unknown as Record<string, unknown>),
        );
        sendSsrHtml(res, html);
      } catch (e) {
        console.error("SSR / failed:", e);
        sendPublic(res, publicDir, "index.html");
      }
    });
  }

  function rejectForeignAddress(resolution: Awaited<ReturnType<typeof resolveSearchInput>>): string | null {
    if (!config || resolution.kind !== "address") return null;
    if (!addressMatchesExplorerHrp(resolution.address, config.bech32Hrp)) {
      return "Address does not match this explorer network.";
    }
    return null;
  }

  app.get("/search", async (req, res) => {
    const q = typeof req.query.q === "string" ? req.query.q.trim() : "";
    if (q) {
      const resolution = await resolveSearchInput(pool, q);
      const foreign = rejectForeignAddress(resolution);
      if (foreign) return res.redirect(302, `/search?error=${encodeURIComponent(foreign)}`);
      const target = canonicalUrlForResolution("", resolution);
      if (target) return res.redirect(302, target);
      const err = resolution.kind === "error" ? resolution.message : "Search failed.";
      return res.redirect(302, `/search?error=${encodeURIComponent(err)}`);
    }
    sendPublic(res, publicDir, "search.html");
  });

  app.get("/search/resolve", async (req, res) => {
    const q = typeof req.query.q === "string" ? req.query.q : "";
    const resolution = await resolveSearchInput(pool, q);
    const foreign = rejectForeignAddress(resolution);
    if (foreign) {
      res.json({ resolution: { kind: "error", message: foreign }, canonical_url: null });
      return;
    }
    const base = `${req.protocol}://${req.get("host") ?? "127.0.0.1"}`;
    const url = canonicalUrlForResolution(base, resolution);
    res.json({ resolution, canonical_url: url });
  });
}
