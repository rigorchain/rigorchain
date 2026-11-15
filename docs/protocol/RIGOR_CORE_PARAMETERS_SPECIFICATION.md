# RIGOR Core Parameters Specification

## Immutable Monetary Constants and Mathematical Boundaries

Version 1.0  
Status: Mainnet Consensus Reference Document

---

# Abstract

RIGOR is a Proof-of-Work blockchain protocol defined by a small set of immutable monetary constants.

Unlike systems where monetary parameters may be adjusted through governance, administrative control, or discretionary issuance policies, RIGOR defines its monetary behavior through deterministic consensus rules.

This document specifies the fundamental constants governing:

- ORU issuance;
- RES Convergence cost;
- RES theoretical maximum supply;
- Convergence arithmetic safety boundaries.

These parameters are part of the RIGOR consensus design and are not intended to be modified after Mainnet activation.

The market value of ORU and RES is determined by participants. The protocol only defines deterministic issuance and validation rules.

---

# 1. Immutable Core Constants

RIGOR monetary behavior is derived from the following protocol constants.

| Parameter | Symbol | Value | Meaning |
|---|---|---|---|
| ORU block subsidy | S | 2⁷ | 0 ORU at height 0; 128 ORU at every height ≥ 1 |
| Initial RES destruction cost | P₀ | 2¹⁰ | 1024 ORU per RES at initial scale |
| Convergence scale parameter | H | 2²⁵ | Controls long-term RES cost growth |
| Theoretical maximum RES supply | RESmax | 2²² | 4,194,304 RES |

These values are consensus parameters.

They are not governance parameters.

---

# 2. ORU Issuance Constant

## 2.1 Definition

Each non-genesis block creates:

$$
S = 2^7
$$

ORU.

Therefore:

$$
S = 128
$$

ORU are created per block.

---

## 2.2 Economic Role

ORU is the operational asset of the network.

Its roles include:

- miner compensation;
- transaction settlement;
- network security budget;
- input resource for RES Convergence.

ORU issuance is independent from RES creation.

Mining creates ORU.

Convergence creates RES.

These are separate consensus processes.

---

# 3. RES Convergence Cost Function

## 3.1 Definition

RES is not created through block subsidy.

RES creation requires irreversible destruction of ORU.

The required destruction amount is determined by:

$$
destroy\_cost(h) = P_0 \times \frac{(H+h)^2}{H^2}
$$

Where:

- $h$ = block height;
- $P_0 = 2^{10}$;
- $H = 2^{25}$.

---

## 3.2 Initial Cost

At the beginning of the chain:

$$
h = 0
$$

Therefore:

$$
destroy\_cost(0) = 2^{10}
$$

Meaning:

$$
1\ \mathrm{RES} = 1024\ \mathrm{ORU}
$$

at the initial scale.

---

## 3.3 Cost Growth

As block height increases:

$$
(H+h)^2
$$

increases quadratically.

Therefore:

The ORU cost required for each RES increases over time.

The protocol does not require an administrator to stop RES issuance.

Instead, issuance naturally approaches a finite mathematical limit.

---

# 4. Derivation of the RES Theoretical Maximum Supply

## 4.1 RES Issuance Rate

Each block at height ≥ 1 creates:

$$
2^7
$$

new ORU. Height 0 creates none.

At height $h$, the maximum theoretical RES creation supported by one block subsidy is:

$$
\frac{2^7}{destroy\_cost(h)}
$$

Substituting the cost function:

$$
= \frac{2^7}{2^{10}} \times \frac{H^2}{(H+h)^2}
$$

---

## 4.2 Infinite Supply Calculation

The total possible RES supply is the accumulation of all possible RES creation over infinite block history:

$$
RES_{max} = \sum_{h=0}^{\infty} \frac{2^7}{destroy\_cost(h)}
$$

Because the cost grows quadratically, the series converges.

The approximation becomes:

$$
RES_{max} = \frac{2^7}{2^{10}} \times 2^{25}
$$

Therefore:

$$
RES_{max} = 2^{7-10+25}
$$

Result:

$$
\boxed{RES_{max} = 2^{22}}
$$

Therefore:

$$
\boxed{RES_{max} = 4{,}194{,}304}
$$

---

# 5. Why The Last RES Cannot Be Created

RIGOR does not enforce RES scarcity through an administrative MAX_SUPPLY switch.

The protocol does not contain a rule:

```
if RES_supply == 4194304:
    stop
```

Instead, the limit emerges from the mathematical behavior of the Convergence function.

As height increases:

$$
destroy\_cost(h) \rightarrow \infty
$$

and therefore:

$$
\frac{ORU}{RES} \rightarrow 0
$$

The remaining theoretical RES generation approaches zero.

Eventually, producing one additional minimum unit of RES requires an amount of ORU that cannot satisfy the consensus conditions.

The final RES unit is therefore not prevented by policy.

It becomes unreachable by mathematics.

---

# 6. Convergence Arithmetic Safety Boundary

## 6.1 Purpose

RIGOR defines an additional boundary for Convergence transaction arithmetic.

The mint predicate shifts the destroyed amount left by 40 bits:

$$
R \cdot (2^{25} + h)^2 \le B \cdot 2^{40}
$$

Where:

- $B$ = ORU atoms destroyed in a single Convergence operation;
- $R$ = RES atoms created by that operation;
- $h$ = the height of the block that contains it.

$B$ is **not** required to be below $2^{40}$ atoms. Any $B$ with $0 \le B \le$ `MAX_MONEY` is valid input, and both sides are evaluated in unsigned 128-bit arithmetic (`CheckResMintArithmetic` in `core/src/consensus/res_issuance.h`). The bounds that make this overflow-free are:

- $B \le$ `MAX_MONEY` $< 2^{60}$, so $B \cdot 2^{40} < 2^{100}$;
- $R \le$ `RES_MAX_SUPPLY` $< 2^{49}$ and $h < 2^{31}$, so $R \cdot (2^{25} + h)^2 < 2^{113}$.

An implementation that rejects $B \ge 2^{40}$ atoms (about 10,995 ORU) would reject valid blocks.

---

## 6.2 Purpose of This Boundary

This boundary exists for consensus safety.

It prevents:

- integer overflow;
- undefined arithmetic behavior;
- inconsistent validation between implementations.

This parameter is not a monetary supply limit.

It does not determine:

$$
RES_{max}
$$

The $2^{40}$ shift, evaluated in 128-bit arithmetic, protects transaction validation.

The $2^{22}$ boundary describes the long-term theoretical RES supply.

These are separate consensus concepts.

---

# 7. Summary of RIGOR Monetary Constants

RIGOR monetary behavior can be summarized by four primary constants:

| Constant | Meaning |
|---|---|
| $2^7$ | ORU created per block |
| $2^{10}$ | Initial ORU destruction cost per RES |
| $2^{25}$ | RES cost growth scale |
| $2^{22}$ | Theoretical maximum RES supply |

Together these constants define:

- ORU security issuance;
- RES creation economics;
- long-term supply limitation;
- deterministic monetary behavior.

---

# 8. Consensus Immutability Statement

After RIGOR Mainnet activation, these monetary constants are considered part of the consensus identity of the network.

Changing these constants would constitute a consensus change.

The RIGOR protocol does not include:

- governance-controlled monetary parameter adjustment;
- emergency monetary modification;
- administrative issuance authority.

The protocol defines the rules.

Participants determine the value.

---

# Conclusion

RIGOR's monetary design is not based on discretionary supply management.

Its monetary boundaries emerge from deterministic relationships between:

- continuous ORU issuance;
- irreversible ORU destruction;
- quadratic Convergence cost growth;
- fixed integer validation boundaries.

The theoretical maximum of:

$$
2^{22}
$$

RES is therefore not a manually selected supply number.

It is the mathematical consequence of the protocol's immutable constants.
