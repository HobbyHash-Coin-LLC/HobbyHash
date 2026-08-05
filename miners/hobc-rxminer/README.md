# hobc-rxminer — HobbyHash RandomX CPU miner

Official reference CPU miner for HobbyHash V6 RandomX.

| Item | Value |
|------|-------|
| Version | **1.1** |
| Algorithm | RandomX (CPU) |
| Official stratum | `pool.hobbyhashcoin.com:5559` |
| Prebuilt packages | https://hobbyhashcoin.com/downloads/miner/randomx/ |
| Matching pool source | [`../../pool-cpu/`](../../pool-cpu/) |

This is **not** XMRig. Stock RandomX miners will not work on HobbyHash CPU pools.

---

## Consensus layout (must match the node)

- Preimage (88 bytes) = `version(4 LE) | prev(32) | merkle(32) | ntime(4 LE) | nbits(4 LE) | height(4 LE) | nonce64(8 LE)`
- Seed key = `"HOBCRX01" || epoch_le32` where `epoch = height / 2048`
- RandomX params = stock tevador RandomX, light mode (deterministic)
- Share compare = little-endian 256-bit digest vs pool target

---

## Stratum (line-delimited JSON)

```text
-> {"id":1,"method":"subscribe","params":{"agent":"hobc-rxminer/1.1"}}
-> {"id":2,"method":"authorize","params":{"address":"<addr>","worker":"<w>"}}
<- {"method":"job","params":{"job_id","blob_prefix","seed_key","target","height","clean"}}
-> {"id":N,"method":"submit","params":{"job_id","nonce64","hash"}}
```

---

## Build (Linux x86_64)

Requirements:

- `gcc`, `g++`, `make`, `cmake`
- HobbyHash node RandomX sources (`node/AL10/src/crypto/randomx` or `node/AL9/...`)
- `jansson` (static or shared)

Example against this repo’s node RandomX tree:

```bash
# 1) Build librandomx.a from the node tree
cmake -S ../../node/AL10/src/crypto/randomx -B build-randomx -DARCH=native -DBUILD_SHARED_LIBS=OFF
cmake --build build-randomx --target randomx -j"$(nproc)"

# 2) Compile miner (system jansson shown; static jansson also works)
gcc -O2 -pthread \
  -I../../node/AL10/src/crypto/randomx/src \
  hobc-rxminer.c \
  build-randomx/librandomx.a \
  -ljansson -lstdc++ -lm \
  -o hobc-rxminer

# 3) Optional consensus check helper
gcc -O2 -I../../node/AL10/src/crypto/randomx/src \
  rxcheck.c build-randomx/librandomx.a -lstdc++ -lm -o rxcheck
```

---

## Run

```bash
./hobc-rxminer -o pool.hobbyhashcoin.com:5559 -u YOUR_HOBC_ADDRESS -w worker1 -t 4
```

Optional speed tip (huge pages):

```bash
sudo sysctl -w vm.nr_hugepages=1280
```

Live pool stats: https://hobbyhashcoin.com/pool/cpu/

---

## Files

| File | Purpose |
|------|---------|
| `hobc-rxminer.c` | Miner (stratum client + RandomX workers) |
| `rxcheck.c` | Offline hash helper to compare against node `getrandomxhash` |

Copyright © HobbyHash Coin LLC.
