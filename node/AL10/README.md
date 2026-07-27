# HobbyHash Core v31.1.0 — AL10 / standard Linux build

**HobbyHash Coin LLC**  
Use this tree on **AlmaLinux 10, RHEL 10, Rocky 10, Fedora, Debian 12+, Ubuntu 22.04/24.04**, and similar modern x86_64 Linux systems.

If you are on **AlmaLinux 9 / RHEL 9 / Rocky 9**, stop and open [`../AL9/README.md`](../AL9/README.md) instead.

Upstream Bitcoin Core notes (if you need them): [`README-BITCOIN-CORE.md`](README-BITCOIN-CORE.md).

---

## What you will build

| Binary | Role |
|--------|------|
| `hobbyhashd` | Full node daemon |
| `hobbyhash-cli` | RPC command-line tool |

Output package name: **`HobbyHash-Linux-Node-x86_64.tar.gz`**

---

## Step-by-step (beginner)

### 1) Install tools

The release script builds with **SQLite wallet** and **`-DWITH_ZMQ=OFF`**, so you do **not** need `libdb-devel` or `zeromq-devel` (those are EPEL-only on Alma/RHEL 10 and are a common install failure).

**AlmaLinux 10 / RHEL 10 / Rocky 10:**

```bash
sudo dnf -y groupinstall "Development Tools"
sudo dnf -y install cmake git python3 boost-devel libevent-devel openssl-devel sqlite-devel
```

**Ubuntu 22.04 / 24.04 / Debian 12:**

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git pkg-config python3 \
  libboost-all-dev libevent-dev libssl-dev libsqlite3-dev
```

### 2) Enter this folder

```bash
cd node/AL10
```

(Use the real path where you cloned this repository.)

### 3) Build the release package

```bash
./scripts/build-linux-al10-release.sh
```

When it finishes, look in:

```text
dist/HobbyHash-Linux-Node-x86_64.tar.gz
```

### 4) Install the binaries (example)

```bash
sudo mkdir -p /opt/hobbyhash
sudo tar -xzf dist/HobbyHash-Linux-Node-x86_64.tar.gz -C /opt/hobbyhash
sudo ln -sf /opt/hobbyhash/HobbyHash-Linux-Node-x86_64/bin/hobbyhashd /usr/local/bin/hobbyhashd
sudo ln -sf /opt/hobbyhash/HobbyHash-Linux-Node-x86_64/bin/hobbyhash-cli /usr/local/bin/hobbyhash-cli
hobbyhashd -version
```

You should see **v31.1.0** (or newer if this tree was bumped).

### 5) Create a config file

Example `~/.hobbyhash/hobbyhash.conf` (adjust paths/users):

```conf
server=1
daemon=1
listen=1
txindex=1
rpcuser=YOUR_RPC_USER
rpcpassword=YOUR_LONG_RANDOM_PASSWORD
rpcbind=127.0.0.1
rpcallowip=127.0.0.1
port=18761
rpcport=18762
# Optional: only if you operate the census collector node
# censustoken=YOUR_CENSUS_TOKEN
```

### 6) Start and sync

```bash
hobbyhashd
hobbyhash-cli getblockchaininfo
```

Wait until `blocks` catches the network tip and `initialblockdownload` is false.

### 7) Connect a pool

Point your SHA / GPU / CPU pool’s RPC URL at `http://127.0.0.1:18762` with the same user/password.  
Pools **must** stamp HOBC markers — see [`../../docs/POOL_COMPLIANCE.md`](../../docs/POOL_COMPLIANCE.md).

---

## Manual CMake build (advanced)

```bash
cmake -S . -B build-linux-x86_64 -DCMAKE_BUILD_TYPE=Release -DBUILD_DAEMON=ON -DBUILD_CLI=ON
cmake --build build-linux-x86_64 -j"$(nproc)" --target hobbyhashd hobbyhash-cli
```

---

## Ports (HobbyHash mainnet)

| Service | Port |
|---------|-----:|
| P2P | 18761 |
| RPC | 18762 |

---

## Need EL9 instead?

Go to [`../AL9/`](../AL9/).

## Source changes

See [`../CHANGES.md`](../CHANGES.md).
