# Note — Research and Verification (older chapter file)

Narrative Chapter 8 is now
[`08-formal-boundary.md`](./08-formal-boundary.md).

# Chapter 8 — Research and Verification

Research artifacts have grades. They must not be collapsed into one
“fully verified” claim.

| Grade | What it is | What it is not |
|-------|------------|----------------|
| RFC / SPEC | Frozen rules | A running chain |
| Formal model (Coq) | Properties of the model | A C++ refinement proof |
| Runtime archive | What a specific execution produced | Equivalence or mainnet completion |
| Observer UI | Display | Authority |

Live issuance under verification is RFC-020: continuous `destroy_cost(h)`, the mint
inequality `R · (2²⁵ + h)² ≤ B << 40`, and the closed-form cap
`419430400000000` atoms.

---

## PROOF-034

**PROOF-034 is a runtime verification archive.**

It materializes a contiguous execution through catalogued golden-vector
heights, including:

| Height | Vector | Phenomenon |
|--------|--------|------------|
| 630,000 | GV-ORU-0003 | Historical ORU-halving fixture (live ORU is flat 128) |
| 2,100,000 | GV-RES-0003 | Historical RES-cap fixture (archived) |
| 2,100,001 | GV-DRAW-0003 | Historical DRAW reject fixture (archived Category C) |

Those DRAW / 21M-era fixtures are **not** live issuance rules.

**PROOF-034 is not:**

- a **protocol equivalence proof**
- a **mainnet completion proof**
- a license to set `equivalence_claimed = true`
- a substitute for RFC / SPEC

The live Experiment page already states this. The whitepaper must not
strengthen that language.

---

## Formal verification

See `docs/verification/FORMAL-VERIFICATION-STATUS.md`. The Coq sources are not a claim that the node binary matches a model.

Production issuance is `CheckResMintArithmetic`. The cap constant is `419430400000000` atoms. A model lemma about a draw or a winner is not that rule.

---

## Existing research surfaces (keep)

| Route | Role |
|-------|------|
| `/protocol` | Frozen rules narrative |
| `/science` | State-machine narrative |
| `/lab` | Experimental framing |
| `/status` | PROOF-034 observer |
| `/experiment` | PROOF-034 explanation |

The Research section of the website **links** these pages. It does not
replace them.
