import { describe, it } from "node:test";
import assert from "node:assert/strict";
import type { Pool, PoolClient } from "pg";
import { withDbTransaction } from "./dbTransaction";

describe("withDbTransaction", () => {
  it("begins, runs, and commits on the same checked-out client", async () => {
    const queries: string[] = [];
    let released = false;
    const client = {
      query: async (sql: string) => {
        queries.push(sql);
        return { rows: [] };
      },
      release: () => {
        released = true;
      },
    };
    const pool = {
      connect: async () => client,
    };

    const value = await withDbTransaction(pool as unknown as Pick<Pool, "connect">, async (bound) => {
      assert.equal(bound, client);
      await bound.query("INSERT INTO outputs VALUES (1)");
      return 7;
    });

    assert.equal(value, 7);
    assert.deepEqual(queries, ["BEGIN", "INSERT INTO outputs VALUES (1)", "COMMIT"]);
    assert.equal(released, true);
  });

  it("rolls the same client back when the body throws, then releases it", async () => {
    const queries: string[] = [];
    let released = false;
    const client = {
      query: async (sql: string) => {
        queries.push(sql);
        return { rows: [] };
      },
      release: () => {
        released = true;
      },
    };

    await assert.rejects(
      () =>
        withDbTransaction({ connect: async () => client as unknown as PoolClient }, async () => {
          throw new Error("outputs_txid_fkey");
        }),
      /outputs_txid_fkey/,
    );

    assert.deepEqual(queries, ["BEGIN", "ROLLBACK"]);
    assert.equal(released, true);
  });
});
