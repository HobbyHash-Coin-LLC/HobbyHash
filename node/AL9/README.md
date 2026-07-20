# HobbyHash Core v31.1.0 — AL9 / RHEL 9 class build

**HobbyHash Coin LLC**  
Use this tree on **AlmaLinux 9, RHEL 9, Rocky Linux 9, CentOS Stream 9**, and other **EL9** x86_64 systems.

If you are on **AlmaLinux 10 / RHEL 10 / Ubuntu / Debian / Fedora**, use [`../AL10/README.md`](../AL10/README.md) instead.

Upstream Bitcoin Core notes: [`README-BITCOIN-CORE.md`](README-BITCOIN-CORE.md).

---

## Why a separate AL9 folder?

Enterprise Linux 9 ships older system libraries than AlmaLinux 10. HobbyHash Coin LLC builds the **AL9** package **inside an AlmaLinux 9 container** so the binaries run cleanly on EL9 hosts.

Source code is the same as AL10. Only the build environment differs.

**Prebuilt artifact name:** `HobbyHash-Linux-Node-AL9-x86_64.tar.gz`

---

## Compatible operating systems

| Distribution | Use this guide? |
|--------------|-----------------|
| AlmaLinux 9 | Yes |
| RHEL 9 | Yes |
| Rocky Linux 9 | Yes |
| CentOS Stream 9 | Yes |
| AlmaLinux 10 / RHEL 10 | No — use **AL10** |
| Ubuntu / Debian | Prefer **AL10** native build |

Check with:

```bash
cat /etc/os-release
```

Look for `VERSION_ID="9"` or `el9`.

---

## Step-by-step (beginner)

### 1) Install Podman (build host)

You can build on an AlmaLinux 10 (or other) machine that has Podman; the container supplies the EL9 toolchain.

```bash
# Alma / RHEL example
sudo dnf -y install podman

# Ubuntu example
sudo apt-get update
sudo apt-get install -y podman
```

### 2) Enter this folder

```bash
cd node/AL9
```

### 3) Build the AL9 package

```bash
./scripts/build-linux-al9-container.sh
```

This:

1. Builds the AlmaLinux 9 builder image from `packaging/al9/Containerfile`
2. Compiles `hobbyhashd` and `hobbyhash-cli` inside that container
3. Writes `dist/HobbyHash-Linux-Node-AL9-x86_64.tar.gz`

### 4) Copy the tarball to your EL9 server and install

```bash
sudo mkdir -p /opt/hobbyhash
sudo tar -xzf HobbyHash-Linux-Node-AL9-x86_64.tar.gz -C /opt/hobbyhash
sudo ln -sf /opt/hobbyhash/HobbyHash-Linux-Node-AL9-x86_64/bin/hobbyhashd /usr/local/bin/hobbyhashd
sudo ln -sf /opt/hobbyhash/HobbyHash-Linux-Node-AL9-x86_64/bin/hobbyhash-cli /usr/local/bin/hobbyhash-cli
hobbyhashd -version
```

### 5) Configure and start

Same ports and config style as AL10:

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
```

```bash
hobbyhashd
hobbyhash-cli getblockchaininfo
```

### 6) Pools

After sync, run a compliant pool from `pool-sha/`, `pool-gpu/`, or `pool-cpu/`.  
**HOBC markers are mandatory** after height 16800 — see [`../../docs/POOL_COMPLIANCE.md`](../../docs/POOL_COMPLIANCE.md).

---

## Troubleshooting

| Problem | Fix |
|---------|-----|
| `podman: command not found` | Install Podman (step 1) |
| Build fails on missing Containerfile | Confirm you are in `node/AL9` and `packaging/al9/Containerfile` exists |
| Binary won’t run on the server (“glibc too old”) | You installed an AL10 binary on EL9 — rebuild with **this** AL9 script |
| Node rejects pool blocks | Pool missing HOBC marker or wrong algo field |

---

## Source changes

See [`../CHANGES.md`](../CHANGES.md).
