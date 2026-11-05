import express, { type NextFunction, type Request, type Response } from "express";
import path from "node:path";
import { loadConfigFromEnv } from "./config";
import { buildCompliancePayload } from "./compliance";
import { createPool, ensureSchema, getOrCreateChainState, updateChainState } from "./db";
import { importerTick } from "./importer";
import { getImporterRecoveryState } from "./importerRecovery";
import { logMismatchEvent } from "./mismatchLog";
import { registerApiRoutes, registerUiRoutes, wantsHtmlPage } from "./registerRoutes";
import { notifyIndexerHeight, startExplorerCaches, stopExplorerCaches } from "./statusCache";
import { createRateLimitMiddleware } from "./rateLimit";
import { assertFailClosedStartup } from "./startupGuard";

async function main(): Promise<void> {
  const config = loadConfigFromEnv();
  const pool = createPool(config);
  await ensureSchema(pool, config);
  await assertFailClosedStartup(config, pool);

  const app = express();
  app.disable("x-powered-by");
  app.set("trust proxy", process.env.TRUST_PROXY === "true");

  const publicDir = path.join(__dirname, "..", "public");
  app.use(createRateLimitMiddleware(config));

  app.get("/healthz", async (_req, res) => {
    try {
      const chain = await getOrCreateChainState(pool, config);
      res.json({
        ok: true,
        chain,
        indexer_recovery: getImporterRecoveryState(),
        rate_limit_enabled: config.rateLimitEnabled,
      });
    } catch (e) {
      res.status(500).json({ ok: false, error: String(e) });
    }
  });

  app.get("/compliance", async (_req, res) => {
    try {
      const chain = await getOrCreateChainState(pool, config);
      const payload = await buildCompliancePayload(config, chain);
      res.json(payload);
    } catch (e) {
      res.status(500).json({ error: String(e) });
    }
  });

  // Canonical UI routes (HTML) before JSON handlers on shared paths.
  registerUiRoutes(app, pool, publicDir, config);
  registerApiRoutes(app, pool, config);

  app.use(express.static(publicDir));
  app.get("/", (_req, res) => res.sendFile(path.join(publicDir, "index.html")));

  app.use((req, res) => {
    if (wantsHtmlPage(req)) {
      res.status(404).sendFile(path.join(publicDir, "404.html"));
      return;
    }
    res.status(404).json({ error: "not found" });
  });

  app.use((err: unknown, req: Request, res: Response, next: NextFunction) => {
    if (res.headersSent) {
      next(err);
      return;
    }
    console.error("Unhandled explorer error:", err);
    if (wantsHtmlPage(req)) {
      res.status(500).sendFile(path.join(publicDir, "500.html"));
      return;
    }
    res.status(500).json({ error: "internal error" });
  });

  let importing = false;

  const runTick = async () => {
    if (importing) return;
    importing = true;
    try {
      const chainBefore = await getOrCreateChainState(pool, config);
      const result = await importerTick(config, pool, new Date().toISOString());
      if (result.stalled) {
        const recovery = getImporterRecoveryState();
        logMismatchEvent({
          kind: "recovery_stalled",
          height: recovery.stalled_height ?? undefined,
          message: "Import stalled; will retry per mismatch SOP",
          attempt: recovery.attempt_count,
        });
        console.error(
          JSON.stringify({
            level: "WARN",
            component: "indexer",
            message: "Import stalled; will retry per mismatch SOP",
            recovery,
          }),
        );
      }
      if (result.reorgHappened) {
        // Reorg handling may truncate and re-import; keep API serving reads from stable DB.
      }
      const chainAfter = await getOrCreateChainState(pool, config);
      if (chainBefore.last_height !== chainAfter.last_height) {
        const height = Number(chainAfter.last_height);
        if (Number.isFinite(height)) notifyIndexerHeight(height);
      }
      if (chainBefore.critical_root_mismatch !== chainAfter.critical_root_mismatch) {
        await updateChainState(pool, config, { critical_root_mismatch: chainAfter.critical_root_mismatch });
      }
    } catch (e) {
      console.error("Importer tick failed:", e);
    } finally {
      importing = false;
    }
  };

  // Bind IPv4 explicitly. Node's default (::) breaks WSL2 Windows localhost forwarding
  // (ERR_CONNECTION_REFUSED on http://127.0.0.1:PORT from the host browser).
  const apiHost = process.env.API_HOST?.trim() || "0.0.0.0";
  startExplorerCaches(config, pool);
  const server = app.listen(config.apiPort, apiHost, () => {
    console.log(`Explorer indexer MVP listening on http://127.0.0.1:${config.apiPort} (${apiHost})`);
  });

  await runTick();
  const tickTimer = setInterval(runTick, config.pollIntervalMs);
  const shutdown = (signal: string) => {
    console.log(`Explorer shutting down (${signal})`);
    clearInterval(tickTimer);
    stopExplorerCaches();
    server.close(() => process.exit(0));
  };
  process.once("SIGTERM", () => shutdown("SIGTERM"));
  process.once("SIGINT", () => shutdown("SIGINT"));
}

main().catch((e) => {
  console.error("Fatal startup error:", e);
  process.exit(1);
});
