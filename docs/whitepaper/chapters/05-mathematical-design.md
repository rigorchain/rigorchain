# Note — Historical Mathematical Design Page

Narrative Chapter 5 is now
[`05-mathematical-fairness.md`](./05-mathematical-fairness.md).
What follows is a leftover design page. It is not the shipping fairness
chapter.

# Chapter 5 — Mathematical Design

Two numbers carry the public mathematics of **shipping** RIGOR:

| Constant | Domain | Job |
|----------|--------|-----|
| **`destroy_cost(h)`** | RES mint cost (display) | `1024 · (2²⁵ + h)² / 2⁵⁰` ORU per whole RES |
| **128 = 2⁷** | ORU block subsidy | Flat miner payment for every `h ≥ 1` |

Fairness is the published integer mint inequality.

```
R > 0  →  R · (2²⁵ + h)² ≤ B << 40
R = 0  →  unconstrained
```

`destroy_cost(h)` is a **protocol cost**, not a market quote. From block 210000
the destroy cost becomes 1036.85749152 ORU per RES.

---

## Live math: `destroy_cost(h)`

```
destroy_cost(h) = 1024 · (2²⁵ + h)² / 2⁵⁰          display (curve denominator 2⁵⁰)
R · (2²⁵ + h)² ≤ B << 40                   consensus (scaling factor 2⁴⁰)
```

`2⁵⁰` is the curve normalization denominator, not a consensus shift.
A display helper may floor `/ 2⁵⁰` (`>> 50`); that rounding/floor does
not enter consensus. `2⁴⁰` is the integer consensus scaling factor,
not the `destroy_cost` denominator.

Integer only. `RES_COIN = 100000000`. `RES_DESTROY_POLE = 2²⁵`.
`RES_DESTROY_SHIFT = 40`. `destroy_cost(0) = 102400000000` (1024 ORU per 1 RES).
Observational calendar `k = ⌊h / 210000⌋` is not a mint argument.

| Height | destroy_cost (atoms) | destroy_cost (ORU per 1 RES) |
|------:|-------------:|---------------------:|
| 0 | 102400000000 | 1024 |
| 210000 | 103685749152 | 1036.85749152 |
| 420000 | 104979520049 | 1049.79520049 |
| 630000 | 106281312688 | 1062.81312688 |

The destroy cost never hits zero. There is no mint horizon.

The cap is closed form `(S / P0) · H = 2⁷ / 2¹⁰ · 2²⁵ = 2²²` RES:

```
RES_MAX_SUPPLY = 419430400000000 atoms = 4194304 RES
```

Source: `CheckResMintArithmetic` in `core/src/consensus/res_issuance.h`.

**There is no unpublished cost.** Same `(R, B, h)` ⇒ same admit or reject.

---

## Why 128

ORU subsidy is flat:

```
R(0) = 0
R(h) = 128 ORU    for h ≥ 1
```

`128 = 2⁷` is the numerator of the cap identity. The retired three-halving
sequence `1000 → 500 → 250 → 125` (and the `125 = 5³` slogan) is historical.

### 125 = 5³

```
5 × 5 × 5  =  125
```

125 is a perfect cube. Among the cubes of 1 through 10
(`1, 8, 27, 64, 125, …`) it is the value the subsidy sequence actually
reaches. The cube root is the integer 5. The tail is not an arbitrary
leftover; it is a **pure third power**.

### Self-describing digits

125 is known as the only positive integer whose **proper divisors**
(`1`, `5`, `25`) all appear as contiguous substrings of its decimal
representation:

```
1 2 5
│   │
1     5
  └── 25
```

- **1** sits at the front
- **5** sits in the middle
- **25** sits at the end

The decimal word “125” displays its own divisor structure. That is a rare
self-describing coincidence. It is not a consensus rule, and it is not the
ORU subsidy.

### Other integer identities (secondary)

These are recorded for the mathematical-design page. They do not change
subsidy consensus.

| Identity | Expression |
|----------|------------|
| Perfect cube | `5³ = 125` |
| Two squares | `10² + 5² = 100 + 25 = 125` |
| Two squares | `11² + 2² = 121 + 4 = 125` |
| Divisors | `1, 5, 25, 125` — a pure prime-power lattice |
| Friedman form | `5^(1+2) = 125` |
| Nearby perfect powers | `121 = 11²`, `125 = 5³`, `128 = 2⁷` |

121, 125, and 128 form a tight cluster of perfect powers. 125 sits in the
middle of that cluster — a small, complete number between a square and a
power of two.

### ORU subsidy

The production ORU subsidy is 0 at height 0 and 128 ORU at every height ≥ 1.
There is no halving schedule and no 125 ORU floor. RES mint cost is
`destroy_cost(h)` from RFC-020, evaluated by the inequality
`R · (2²⁵ + h)² ≤ B << 40`. The integer 125 is not a consensus parameter.

---

Shipping fairness is `R · (2²⁵ + h)² ≤ B << 40`.
