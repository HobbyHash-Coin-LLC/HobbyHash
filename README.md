# HobbyHash Coin LLC — V6 Open Source

**HobbyHash Core v31.1.0** — multi-algorithm race (SHA-256d, KawPow, RandomX) on HobbyHash Coin mainnet.

This repository contains:

| Folder | What it is |
|--------|------------|
| [`node/`](node/) | Full node source for **AL10** and **AL9** Linux builds |
| [`pool-sha/`](pool-sha/) | SHA-256d solo / PPLNS pool source (ckpool-based) |
| [`pool-gpu/`](pool-gpu/) | KawPow GPU solo pool source (KPSS-based) |
| [`pool-cpu/`](pool-cpu/) | RandomX CPU solo pool source |
| [`docs/`](docs/) | Technical fork specification |

Copyright © HobbyHash Coin LLC. See `COPYING` / license files inside each tree.

---

## Which node package do I need?

Open [`node/README.md`](node/README.md). Short answer:

- **AlmaLinux 10, RHEL 10, Fedora 10, or most modern glibc Linux** → use **`node/AL10/`**
- **AlmaLinux 9, RHEL 9, Rocky 9, CentOS Stream 9** → use **`node/AL9/`**

Both folders contain the **same V6 source**. The difference is the **recommended build / install path** and which prebuilt tarball name matches your OS.

---

## Critical rule for every pool operator

Starting at mainnet height **16800**, HobbyHash Core **rejects** blocks that do not carry a valid **HOBC coinbase marker**.

That marker must include real pool identity fields (`pool_id`, algorithm, share difficulty, and preferably `pool_site`, worker, and rig).  
**Fake, blank, or spoofed identity data can get your pool blocked by HobbyHash Coin LLC operators** (network census, explorer labeling, and admin pool controls).

Read:

- [`docs/POOL_COMPLIANCE.md`](docs/POOL_COMPLIANCE.md) — marker rules, real-info policy, examples, do’s / don’ts
- [`docs/FORK_SPEC_V6.md`](docs/FORK_SPEC_V6.md) — full technical specification
- **Live validator / config builder:** https://hobbyhashcoin.com/docs/pool-config/

---

## Quick start (new operators)

1. Build or install a **V6 node** from [`node/`](node/) for your OS family.
2. Sync the chain and unlock wallet / configure RPC as needed.
3. Pick your algo pool source:
   - SHA ASICs → [`pool-sha/`](pool-sha/)
   - NVIDIA/AMD KawPow GPUs → [`pool-gpu/`](pool-gpu/)
   - CPU RandomX → [`pool-cpu/`](pool-cpu/)
4. Validate `pool_site` (≤ 24 chars), `pool_id`, and `algo` on https://hobbyhashcoin.com/docs/pool-config/ and copy the generated config.
5. Configure `hobc_marker`, `hobc_pool_id`, `hobc_pool_site`, and census token **with real information**.
6. Point miners at your stratum port and confirm accepted shares + valid HOBC markers on solved blocks.

Each folder has a beginner (“for dummies”) guide with copy-paste commands.

---

## Support

HobbyHash Coin LLC — https://hobbyhashcoin.com  
Explorer / network race stats are published on the official HobbyHash website.
