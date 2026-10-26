# Community tools

Third-party tools that work with the RIGOR protocol. These are independent
projects; they are not operated or endorsed by the RIGOR project.

Listing a tool here means it is provided for community reference only.
A listing does not imply a security audit, verification of accuracy, or
endorsement by the RIGOR project. Each tool stands on its own, and users
should review it themselves before running anything.

## RIGOR Explorer 98

A community-built, read-only web explorer for the RIGOR mainnet:

- **Live:** <https://rigor-explorer.pages.dev>
- **Source:** <https://github.com/Alok-mad98/rigor-explorer>
- **API used:** the public chain API (`/api/status`, `/api/blocks`, `/api/block/{height|hash}`, `/api/tx/{txid}`, `/api/address/{addr}`), read-only, no credentials

What it does:

- overview: tip height, difficulty, estimated hashrate, peers, mempool, monetary state (ORU circulating, immature coinbase, convergence burns, fee burns, RES supply)
- block list and full block detail (header, reward, consensus panels)
- transaction detail with raw hex
- address lookup: balances, lifetime received/spent, UTXO list, transaction history, convergence activity, RES mints
- search that resolves heights, 64-hex hashes and `rg1q…` addresses
- a Three.js chain visualizer of the latest blocks

Stack: React 18 + Vite + Three.js frontend, a Rust/WASM core for query
classification and exact `u128` atom math, and a Rust Cloudflare Worker as a
read-only API proxy. MIT licensed.

If you have built a tool that interoperates with RIGOR, feel free to open a
pull request adding it to this file.
