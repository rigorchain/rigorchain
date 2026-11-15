# Convergence consensus lock

**See also** [`PROTOCOL-NAMING-TRUTH-SOURCE.md`](./PROTOCOL-NAMING-TRUTH-SOURCE.md).
`src/` is the source of truth.

RFC-020: a convergence output destroys ORU. The same body transaction may mint
Witness v1 RES when `CheckResMintArithmetic` holds. There is no draw, no
candidate window, no winner, and no coinbase RES mint.

---

## 1. Ticket (ORU burn)

A convergence transaction is an ordinary ORU spend with **exactly one**
positive-value burn:

```
scriptPubKey = <20-byte HASH160> <0xbd CONVERGE marker>
```

- Encoding is exactly 22 bytes. The output is not a spendable predicate
  (`AddCoins` skips it).
- Amount is `nValue`, not encoded in the script.
- `MIN_CONVERGENCE_AMOUNT` is **1 atom**. This is not a mint price.
- Byte `0xbc` stays burned (`HasValidOps` still rejects it).

---

## 2. RES mint (same body transaction)

When the transaction also creates Witness v1 RES outputs summing to `R`:

1. Inputs must be ORU.
2. Exactly one positive CONVERGE output with value `B`.
3. `R > 0 → R · (2²⁵+h)² ≤ B << 40`.
4. There is no mint horizon.

A plain burn (`R = 0`) is valid. New RES never comes from the coinbase.

`RES_COIN = 100000000`. Cap `419430400000000` atoms (`2²²` RES).
Genesis display destroy cost `destroy_cost(0) = 102400000000` (1024 ORU per 1 RES).

---

## 3. Coinbase

`scriptSig` is the BIP34 height push only. See
[`rigor_coinbase_layout.h`](../src/consensus/rigor_coinbase_layout.h) and
[mining-pool-gbt.md](./mining-pool-gbt.md).

---

## 4. Maturity

- **`COINBASE_MATURITY` is 100 blocks** for coinbase ORU.
- Fresh Witness v1 RES mint outputs use the same 100-block maturity
  (`RES_MINT_MATURITY`).

---

## 5. What left consensus

Not required, matched, or paired with a mint:

- draw window / σ / `RES_PROBABILITY_TARGET`
- coinbase Convergence Root / paid-winner
- coinbase `0xbb` mint
- CVGE / RIGR / PCORB coinbase `OP_RETURN`
