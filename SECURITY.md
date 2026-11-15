# Security Policy

## Reporting a vulnerability

**Do not open a public issue for anything that affects consensus, funds, or node
availability.** A public report on a live chain is itself an attack.

Use GitHub's private vulnerability reporting on this repository:
**Security → Report a vulnerability**. That channel is private to the maintainers and
creates a thread where a fix can be prepared before disclosure.

When reporting, the most useful things you can include are the affected component and
commit, what an attacker gains, and the smallest reproduction you have — a failing unit or
functional test is ideal, but a clear description of the sequence of blocks or transactions
is enough.

## Scope

Highest priority, in order:

1. **Consensus** — anything that lets two honest nodes disagree about the valid chain, or
   that lets a block violate the rules in `core/src/consensus/`. This includes RES mint
   arithmetic: paying less than the RFC-020 pole predicate for minted atoms, or
   creating RES without a corresponding `OP_CONVERGE` burn.
2. **Supply** — minting ORU beyond the per-block subsidy (128 ORU for `h ≥ 1`),
   minting RES beyond the cap `419430400000000` atoms (`2²²` RES), or creating
   spendable value from a convergence burn.
3. **Funds** — anything that spends an output without satisfying its script, or that makes
   a correctly-formed output unspendable.
4. **Node availability** — remote crashes, unbounded memory or disk growth, and consensus
   validation that can be made superlinear by an attacker.

Also in scope, at lower priority: the explorer (`explorer/`), and the
miner (`miner/`).

Out of scope: missing hardening headers on a static site, and reports
produced solely by an automated scanner without a demonstrated impact,
are not useful to us.

## What we already know

Read [docs/verification/FORMAL-VERIFICATION-STATUS.md](docs/verification/FORMAL-VERIFICATION-STATUS.md)
before reporting a “formal proof” gap. There is no mechanized refinement between the Coq
model and the C++ node. Production issuance is the RFC-020 inequality, not a selection
function. A finding that the node and a model disagree is new information. A
report that the gap exists is not, because we documented it ourselves.

The known-open items are tracked under [docs/security/](./docs/security).

## Supported versions

RIGOR is pre-mainnet. Until a mainnet release is tagged, only the default branch is
supported and fixes land there.
