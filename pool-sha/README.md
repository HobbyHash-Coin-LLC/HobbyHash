# HobbyHash SHA-256d pools (ckpool) — HobbyHash Coin LLC

This folder is the **SHA** pool source for HobbyHash V6:

| Subfolder | Typical use | Default stratum port | Official `pool_id` |
|-----------|-------------|---------------------:|-------------------:|
| [`hobc-main/`](hobc-main/) | Main solo | 5555 | 1 |
| [`hobc-nano/`](hobc-nano/) | Nano solo (lower start difficulty) | 5556 | 2 |
| [`hobc-pplns/`](hobc-pplns/) | PPLNS | 5557 | 3 |

Each tree is a full ckpool-based source with HobbyHash Coin LLC patches for:

- **HOBC coinbase markers** (`src/hobc_marker.c`)
- Per-worker / rig identity in the marker
- **Network census** heartbeats (`submitcensus`)
- **LuxOS / AsicBoost share validation** (dual-hash BIP34 bit `0x4` — see [`CHANGES.md`](CHANGES.md))

**Change log:** [`CHANGES.md`](CHANGES.md) (last updated **2026-07-26 19:25 UTC**).

---

## Read this before you mine

After mainnet height **16800**, HobbyHash Core **will not accept** SHA blocks that lack a valid HOBC marker.

You must configure:

- `hobc_marker`: `1`
- `hobc_pool_id`: your numeric id
- `hobc_algo`: `0` (SHA-256d)
- `hobc_pool_site`: your **real** site hostname/label

**Fake or spoofed identity information can get your pool blocked by HobbyHash Coin LLC administrators.**  
Full policy: [`../docs/POOL_COMPLIANCE.md`](../docs/POOL_COMPLIANCE.md).  
**Validate + generate config first:** https://hobbyhashcoin.com/docs/pool-config/ (`pool_site` ≤ 24 chars, worker suffix ≤ 16 chars).

---

## Step-by-step (beginner) — build main solo

### 1) Install build tools

**Alma / RHEL:**

```bash
sudo dnf -y groupinstall "Development Tools"
sudo dnf -y install autoconf automake libtool pkgconfig jansson-devel openssl-devel
```

**Ubuntu / Debian:**

```bash
sudo apt-get update
sudo apt-get install -y build-essential autoconf automake libtool pkg-config \
  libjansson-dev libssl-dev
```

### 2) Build

```bash
cd pool-sha/hobc-main
./autogen.sh
./configure
make -j"$(nproc)"
```

You should get a `ckpool` binary under `src/` (or the configured prefix).

Repeat the same commands inside `hobc-nano` or `hobc-pplns` if you need those flavors.

### 3) Minimal `ckpool.conf` (example — edit everything)

```json
{
  "btcdurl": "http://YOUR_RPC_USER:YOUR_RPC_PASSWORD@127.0.0.1:18762",
  "btcaddress": "hobc1q_your_fallback_address",
  "serverurl": ["0.0.0.0:5555"],
  "hobc_marker": 1,
  "hobc_pool_id": 1,
  "hobc_algo": 0,
  "hobc_pool_site": "your.pool.domain.example"
}
```

Notes:

- Point `btcdurl` at a synced HobbyHash **v31.1.0** node.
- Solo payouts use the worker name address (standard HobbyHash solo pattern).
- **Census:** by default the pool auto-POSTs heartbeats to the HobbyHash hub (`hobc_census_hub` defaults true). No census token or public RPC is required. Optional `hobc_census_token` is only for also submitting to your **local** node. Set `"hobc_census_hub": false` to disable the hub. Install `curl` on the pool host.

### 4) Run

```bash
./src/ckpool -c ckpool.conf
```

### 5) Point a miner

Stratum example:

```text
stratum+tcp://YOUR_SERVER_IP:5555
User: hobc1qYourAddress.worker1
Password: x
```

### 6) Verify compliance

1. Confirm shares are accepted.
2. When a block is found, check the explorer block page:
   - Algorithm = SHA-256
   - Mined by pool shows your display name / site+id
   - Worker / rig rows appear when those TLVs were stamped
3. If the node rejects the block with a marker error, fix config before continuing.

---

## Marker source files (HobbyHash Coin LLC)

| File | Role |
|------|------|
| `src/hobc_marker.h` / `hobc_marker.c` | Builds the HOBC TLV bytes |
| `src/stratifier.c` | Inserts marker into coinbase; sends census |
| `src/ckpool.c` / `ckpool.h` | Parses `hobc_*` config keys |

---

## Nano and PPLNS

Build steps are identical. Change:

- Listen port (`5556` / `5557`)
- `hobc_pool_id` (`2` / `3` for official HobbyHash IDs — third parties: use your own)
- Any PPLNS-specific payout settings in that tree’s docs/config

---

## Support

HobbyHash Coin LLC — https://hobbyhashcoin.com
