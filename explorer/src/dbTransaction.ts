import type { Pool, PoolClient } from "pg";

/** Pool or a checked-out client. Both can run the statements inside one block import. */
export type DbQueryable = Pool | PoolClient;

/**
 * Run `fn` on one checked-out connection.
 *
 * `pool.query("BEGIN")` does not pin a session: each call may use a different
 * connection. Inserts then auto-commit on one session while ROLLBACK undoes
 * another, which is how block 1399 stored the witness output and lost coinbase
 * vout 0.
 */
export async function withDbTransaction<T>(
  pool: Pick<Pool, "connect">,
  fn: (client: PoolClient) => Promise<T>,
): Promise<T> {
  const client = await pool.connect();
  try {
    await client.query("BEGIN");
    try {
      const result = await fn(client);
      await client.query("COMMIT");
      return result;
    } catch (err) {
      try {
        await client.query("ROLLBACK");
      } catch {
        // The connection is already unusable. release() still returns it to the pool,
        // which discards a client that failed during rollback.
      }
      throw err;
    }
  } finally {
    client.release();
  }
}
