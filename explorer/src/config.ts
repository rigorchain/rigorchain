import path from "node:path";
import {
  bech32HrpForNetwork,
  defaultRpcPort,
  requireExplorerNetwork,
  type ExplorerNetwork,
} from "./network";

export type Config = {
  network: ExplorerNetwork;
  bech32Hrp: string;
  rpcPort: number;
  rpcUser: string | null;
  rpcPassword: string | null;
  rigorDatadir: string;
  rigorCliBin: string;
  pollIntervalMs: number;
  apiPort: number;

  pgUrl: string;
  schemaMigrationsPath: string; // absolute path

  indexerName: string;
  importBatchMaxHeight: number; // safety cap per poll cycle
  reorgSafetyWindow: number; // must be >= 10 for closed drill
  maxSupportedReorgDepth: number; // if exceeded, require full reindex

  stopOnRootMismatch: boolean; // strict: halt block import on mismatch (retry via recovery SOP)
  mismatchRetryIntervalMs: number;

  rateLimitEnabled: boolean;
  rateLimitWindowMs: number;
  rateLimitGlobalMax: number;
  rateLimitRoutes: Record<string, number>;
};

export function loadConfigFromEnv(): Config {
  const network = requireExplorerNetwork(process.env.RIGOR_NETWORK);
  const bech32Hrp = bech32HrpForNetwork(network, process.env.BECH32_HRP);
  const rpcPort = defaultRpcPort(network, process.env.RIGOR_RPC_PORT);
  const rpcUser = process.env.RIGOR_RPC_USER?.trim() || null;
  const rpcPassword = process.env.RIGOR_RPC_PASSWORD?.trim() || null;
  const rigorDatadir = process.env.RIGOR_DATADIR ?? "/var/lib/rigor/mainnet";
  const rigorCliBin = process.env.RIGOR_CLI_BIN ?? "rigor-cli";

  const pollIntervalMs = parseInt(process.env.POLL_INTERVAL_MS ?? "30000", 10);
  const apiPort = parseInt(process.env.API_PORT ?? "8080", 10);

  const pgUrl = process.env.PG_URL ?? "postgres://postgres:postgres@127.0.0.1:5432/rigor_explorer";
  // Default to local project migrations directory.
  const schemaMigrationsPath =
    process.env.SCHEMA_MIGRATIONS_PATH ?? path.join(__dirname, "..", "migrations");

  const indexerName = process.env.INDEXER_NAME ?? "rigor-explorer-indexer-mvp";
  const importBatchMaxHeight = parseInt(process.env.IMPORT_BATCH_MAX_HEIGHT ?? "20", 10);
  const reorgSafetyWindow = parseInt(process.env.REORG_SAFETY_WINDOW ?? "10", 10);
  const maxSupportedReorgDepth = parseInt(process.env.MAX_SUPPORTED_REORG_DEPTH ?? "200", 10);
  const stopOnRootMismatch = (process.env.STOP_ON_ROOT_MISMATCH ?? "true") === "true";
  const mismatchRetryIntervalMs = parseInt(process.env.MISMATCH_RETRY_INTERVAL_MS ?? "60000", 10);

  const rateLimitEnabled = (process.env.RATE_LIMIT_ENABLED ?? "true") === "true";
  const rateLimitWindowMs = parseInt(process.env.RATE_LIMIT_WINDOW_MS ?? "60000", 10);
  const rateLimitGlobalMax = parseInt(process.env.RATE_LIMIT_GLOBAL_MAX ?? "120", 10);
  const rateLimitRoutes: Record<string, number> = {
    "/api/tx/:txid": parseInt(process.env.RATE_LIMIT_TX_MAX ?? "60", 10),
    "/api/block/:hash": parseInt(process.env.RATE_LIMIT_BLOCK_MAX ?? "60", 10),
    "/api/address/:addr": parseInt(process.env.RATE_LIMIT_ADDRESS_MAX ?? "60", 10),
    "/api/lineage/:txid": parseInt(process.env.RATE_LIMIT_LINEAGE_MAX ?? "40", 10),
    "/api/status": parseInt(process.env.RATE_LIMIT_STATUS_MAX ?? "30", 10),
    "/api/oru/economics": parseInt(process.env.RATE_LIMIT_ORU_ECONOMICS_MAX ?? "30", 10),
    "/healthz": parseInt(process.env.RATE_LIMIT_HEALTH_MAX ?? "60", 10),
    "/compliance": parseInt(process.env.RATE_LIMIT_COMPLIANCE_MAX ?? "30", 10),
  };

  return {
    network,
    bech32Hrp,
    rpcPort,
    rpcUser,
    rpcPassword,
    rigorDatadir,
    rigorCliBin,
    pollIntervalMs,
    apiPort,

    pgUrl,
    schemaMigrationsPath,

    indexerName,
    importBatchMaxHeight,
    reorgSafetyWindow,
    maxSupportedReorgDepth,

    stopOnRootMismatch,
    mismatchRetryIntervalMs,

    rateLimitEnabled,
    rateLimitWindowMs,
    rateLimitGlobalMax,
    rateLimitRoutes,
  };
}

