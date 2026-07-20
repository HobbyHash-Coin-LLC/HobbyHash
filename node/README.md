# HobbyHash Core V6 — Linux node source

**Package version:** HobbyHash Core **v31.1.0**  
**Publisher:** HobbyHash Coin LLC  
**Activation height (mainnet):** **16800** (three-way PoW race)

This folder holds the full node source twice — once under **AL10** and once under **AL9** — so each OS family has its own beginner guide next to the tree you will build.

The **source code is the same**. You only choose the folder that matches your Linux distribution.

---

## Which folder should I use?

### Use [`AL10/`](AL10/) if your system is:

| Distribution | Notes |
|--------------|-------|
| **AlmaLinux 10** | Primary build target for HobbyHash Coin LLC |
| **RHEL 10** | Red Hat Enterprise Linux 10 |
| **Rocky Linux 10** | RHEL-compatible |
| **Fedora** (recent) | Usually works with the AL10 / standard glibc build |
| **Debian 12+ / Ubuntu 22.04+ / 24.04** | Use AL10 tree + native CMake build on the host |
| Other **x86_64 Linux with modern glibc** | Prefer AL10 instructions |

**Prebuilt artifact name (when HobbyHash publishes binaries):**  
`HobbyHash-Linux-Node-x86_64.tar.gz`

### Use [`AL9/`](AL9/) if your system is:

| Distribution | Notes |
|--------------|-------|
| **AlmaLinux 9** | Built inside an AlmaLinux 9 container |
| **RHEL 9** | Red Hat Enterprise Linux 9 |
| **Rocky Linux 9** | RHEL-compatible |
| **CentOS Stream 9** | RHEL-compatible |
| Older enterprise hosts that need **EL9** libraries | Use the AL9 container build |

**Prebuilt artifact name:**  
`HobbyHash-Linux-Node-AL9-x86_64.tar.gz`

### Still unsure?

1. Run: `cat /etc/os-release`
2. If you see `VERSION_ID="9"` (or “el9”) → **AL9**
3. If you see `VERSION_ID="10"`, Ubuntu/Debian/Fedora → **AL10**
4. When in doubt on a brand-new desktop/server → start with **AL10**

---

## What changed in V6?

See [`CHANGES.md`](CHANGES.md) for a file-by-file list of HobbyHash Coin LLC modifications and why each exists.

High level:

- Three-algo race after height 16800 (SHA-256d, KawPow, RandomX)
- Required **HOBC coinbase marker** for accepted blocks
- Network census RPC for compliant pools
- Flat 45 HOBC subsidy after activation (all algos)

---

## Beginner path

1. Open **AL10/README.md** or **AL9/README.md**
2. Install build dependencies listed there
3. Run the one-line build script for your folder
4. Install binaries, create `hobbyhash.conf`, start `hobbyhashd`
5. Wait for sync, then point your pool at local RPC

Pool operators: also read [`../docs/POOL_COMPLIANCE.md`](../docs/POOL_COMPLIANCE.md).
