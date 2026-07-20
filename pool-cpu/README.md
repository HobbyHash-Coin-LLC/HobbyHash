# HobbyHash RandomX CPU pool — HobbyHash Coin LLC

Node.js stratum server for **RandomX** CPU mining on HobbyHash V6.

| Item | Value |
|------|-------|
| Official stratum port | **5559** |
| Official `pool_id` | **4** |
| Algo id in marker | **2** (RandomX) |
| Entry script | `hobc-cpu-randomx-server.js` |
| Marker library | `lib/hobc_marker.js` |
| systemd examples | `systemd/` |

---

## Mandatory compliance

HobbyHash Core **rejects** RandomX blocks without a valid **HOBC coinbase marker** after height **16800**.

Required environment:

- `HOBC_MARKER=1`
- `HOBC_POOL_ID` (official CPU pool uses `4`; third parties: pick your own)
- `HOBC_ALGO=2`
- `HOBC_POOL_SITE=your.real.domain`

**Use real pool identity information.** Fake tags or spoofed sites can result in **admin blocks** by HobbyHash Coin LLC.  
Details: [`../docs/POOL_COMPLIANCE.md`](../docs/POOL_COMPLIANCE.md).

---

## Step-by-step (beginner)

### 1) Requirements

- Synced HobbyHash Core **v31.1.0** node (RPC `18762`)
- Node.js **18+**
- A RandomX-capable CPU miner (XMRig-compatible stratum clients, HobbyHash desktop CPU miner, etc.)

### 2) Install dependencies

```bash
cd pool-cpu
npm install
```

### 3) Configure

Copy the example env:

```bash
cp systemd/hobc-cpu-pool.env.example /etc/hobbyhash/hobc-cpu-pool.env
# edit the file — set RPC, payout address, site, token
```

Or export variables in your shell (see the example file). Critical keys:

```bash
HOBC_RPC_URL=http://127.0.0.1:18762
HOBC_RPC_USER=...
HOBC_RPC_PASSWORD=...
POOL_ADDRESS=hobc1q...
HOBC_MARKER=1
HOBC_POOL_ID=4
HOBC_ALGO=2
HOBC_POOL_SITE=your.pool.domain.example
HOBC_POOL_NAME="Your CPU Pool Name"
HOBC_CENSUS_TOKEN=...   # optional
STRATUM_PORT=5559
```

### 4) Start with Node

```bash
set -a
source /etc/hobbyhash/hobc-cpu-pool.env
set +a
node hobc-cpu-randomx-server.js
```

### 5) Optional: systemd

```bash
sudo cp systemd/hobbyhash-cpu-stratum.service /etc/systemd/system/
# point EnvironmentFile= to your env path
sudo systemctl daemon-reload
sudo systemctl enable --now hobbyhash-cpu-stratum.service
sudo systemctl status hobbyhash-cpu-stratum.service
```

### 6) Point a CPU miner

```text
stratum+tcp://YOUR_SERVER:5559
User: hobc1qYourAddress.cpu1
Password: x
Algo: randomx / rx/hobc as supported by your miner
```

### 7) Verify

- Accepted shares on the pool log
- Explorer shows **RandomX** on solved blocks
- Pool label matches your registry / site+id
- Node no longer errors on RandomX tips for `getblockchaininfo` (V6 `GetTarget` fix)

---

## Marker and census

Same HOBC TLV format as SHA and GPU pools (`lib/hobc_marker.js`).  
Census reports RandomX hashrate aggregates only — **no miner IPs**.

---

## Support

HobbyHash Coin LLC — https://hobbyhashcoin.com
