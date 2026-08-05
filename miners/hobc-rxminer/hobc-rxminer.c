/*
 * hobc-rxminer — HOBC RandomX CPU miner (reference).
 *
 * Byte-exact with the HobbyHash node's RandomX consensus (randomx_check.cpp):
 *   preimage (88 bytes) = version(4 LE) | prev(32) | merkle(32) | ntime(4 LE) |
 *                         nbits(4 LE) | height(4 LE) | nonce64(8 LE)
 *   seed key            = "HOBCRX01" || epoch_le32, epoch = height / 2048
 *   RandomX params      = stock (tevador RandomX), light mode (deterministic).
 *
 * The pool hands out the exact 80-byte blob prefix (everything except the 8-byte
 * nonce) plus the seed key and a little-endian 256-bit share target, so the miner
 * never has to reason about header byte order. The miner appends its own 8-byte
 * little-endian nonce, RandomX-hashes the 88 bytes, and submits when the digest
 * (interpreted little-endian, exactly as the node's arith_uint256 compare) is <=
 * the target.
 *
 * Simple line-delimited JSON stratum (miner <-> hobc CPU pool):
 *   -> {"id":1,"method":"subscribe","params":{"agent":"hobc-rxminer/1.1"}}
 *   -> {"id":2,"method":"authorize","params":{"address":"<addr>","worker":"<w>"}}
 *   <- {"method":"job","params":{"job_id","blob_prefix","seed_key","target",
 *                                "height","clean"}}
 *   -> {"id":N,"method":"submit","params":{"job_id","nonce64","hash"}}
 */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- portable sockets / sleep (POSIX + Windows/Winsock) ---- */
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <windows.h>
  typedef SOCKET sock_t;
  #define SOCK_INVALID INVALID_SOCKET
  #define closesock closesocket
  static inline int sock_read(sock_t s, void* b, size_t n) { return recv(s, (char*)b, (int)n, 0); }
  static inline int sock_write(sock_t s, const void* b, size_t n) { return send(s, (const char*)b, (int)n, 0); }
  static inline void msleep(int ms) { Sleep(ms); }
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <netinet/in.h>
  #include <netinet/tcp.h>
  #include <sys/socket.h>
  #include <unistd.h>
  typedef int sock_t;
  #define SOCK_INVALID (-1)
  #define closesock close
  static inline int sock_read(sock_t s, void* b, size_t n) { return (int)read(s, b, n); }
  static inline int sock_write(sock_t s, const void* b, size_t n) { return (int)write(s, b, n); }
  static inline void msleep(int ms) { usleep((useconds_t)ms * 1000); }
#endif

#include <jansson.h>
#include "randomx.h"

#define BLOB_PREFIX_LEN 80
#define BLOB_LEN 88

/* -------- shared job state -------- */
typedef struct {
    pthread_mutex_t lock;
    char job_id[64];
    uint8_t prefix[BLOB_PREFIX_LEN];
    uint8_t target[32];   /* little-endian 256-bit */
    uint8_t seed[64];
    size_t seed_len;
    int height;
    atomic_ullong generation; /* bumped on every new job */
    bool have_job;
} job_state_t;

static job_state_t g_job;

/* shared RandomX cache, guarded by g_cache_lock; VMs are per-thread */
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static randomx_cache* g_cache = NULL;
static uint8_t g_cache_seed[64];
static size_t g_cache_seed_len = 0;
static atomic_ullong g_cache_gen = 0; /* bumped whenever g_cache is re-keyed */
static randomx_flags g_flags;

static sock_t g_sock = SOCK_INVALID;
static pthread_mutex_t g_send_lock = PTHREAD_MUTEX_INITIALIZER;
static atomic_int g_msg_id = 10;
static char g_address[128];
static char g_worker[64];
static atomic_ullong g_hashes = 0;
static atomic_ullong g_accepted = 0;
static atomic_ullong g_blocks = 0;

/* -------- helpers -------- */
static int hex2bin(const char* hex, uint8_t* out, size_t maxlen)
{
    size_t n = strlen(hex);
    if (n % 2 != 0 || n / 2 > maxlen) return -1;
    for (size_t i = 0; i < n / 2; i++) {
        unsigned v;
        if (sscanf(hex + 2 * i, "%2x", &v) != 1) return -1;
        out[i] = (uint8_t)v;
    }
    return (int)(n / 2);
}

static void bin2hex(const uint8_t* in, size_t len, char* out)
{
    static const char* d = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        out[2 * i] = d[in[i] >> 4];
        out[2 * i + 1] = d[in[i] & 0xf];
    }
    out[2 * len] = '\0';
}

/* Compare 32-byte little-endian values: return -1 if a<b, 0 if eq, 1 if a>b. */
static int le256_cmp(const uint8_t* a, const uint8_t* b)
{
    for (int i = 31; i >= 0; i--) {
        if (a[i] < b[i]) return -1;
        if (a[i] > b[i]) return 1;
    }
    return 0;
}

static int send_line(const char* s)
{
    pthread_mutex_lock(&g_send_lock);
    size_t len = strlen(s);
    int w = sock_write(g_sock, s, len);
    int ok = (w == (int)len);
    if (ok) ok = (sock_write(g_sock, "\n", 1) == 1);
    pthread_mutex_unlock(&g_send_lock);
    return ok ? 0 : -1;
}

static void send_json(json_t* obj)
{
    char* s = json_dumps(obj, JSON_COMPACT);
    if (s) {
        send_line(s);
        free(s);
    }
    json_decref(obj);
}

/* Ensure the shared cache is keyed with `seed`. Caller must NOT hold g_cache_lock. */
static void ensure_cache(const uint8_t* seed, size_t seed_len)
{
    pthread_mutex_lock(&g_cache_lock);
    if (g_cache && g_cache_seed_len == seed_len && memcmp(g_cache_seed, seed, seed_len) == 0) {
        pthread_mutex_unlock(&g_cache_lock);
        return;
    }
    if (!g_cache) {
        g_cache = randomx_alloc_cache(g_flags);
        if (!g_cache) {
            fprintf(stderr, "FATAL: randomx_alloc_cache failed\n");
            exit(1);
        }
    }
    randomx_init_cache(g_cache, seed, seed_len);
    memcpy(g_cache_seed, seed, seed_len);
    g_cache_seed_len = seed_len;
    atomic_fetch_add(&g_cache_gen, 1);
    pthread_mutex_unlock(&g_cache_lock);
    fprintf(stderr, "[rxminer] RandomX cache (re)keyed (seed_len=%zu)\n", seed_len);
}

/* -------- miner threads -------- */
typedef struct { int idx; int nthreads; } worker_arg_t;

static void* worker(void* varg)
{
    worker_arg_t* wa = (worker_arg_t*)varg;
    randomx_vm* vm = NULL;
    unsigned long long my_cache_gen = 0;

    uint8_t blob[BLOB_LEN];
    uint8_t digest[RANDOMX_HASH_SIZE];
    uint8_t local_target[32];
    char local_job[64];
    int local_height = 0;
    unsigned long long local_gen = (unsigned long long)-1; /* force first job refresh */
    uint64_t nonce = ((uint64_t)wa->idx) << 56; /* per-thread nonce space */

    for (;;) {
        /* Refresh job snapshot if generation changed. */
        unsigned long long g = atomic_load(&g_job.generation);
        if (g != local_gen) {
            pthread_mutex_lock(&g_job.lock);
            if (!g_job.have_job) { pthread_mutex_unlock(&g_job.lock); msleep(50); continue; }
            memcpy(blob, g_job.prefix, BLOB_PREFIX_LEN);
            memcpy(local_target, g_job.target, 32);
            memcpy(local_job, g_job.job_id, sizeof(local_job));
            local_height = g_job.height;
            uint8_t seed[64]; size_t seed_len = g_job.seed_len;
            memcpy(seed, g_job.seed, seed_len);
            local_gen = g;
            pthread_mutex_unlock(&g_job.lock);

            ensure_cache(seed, seed_len);
            nonce = (((uint64_t)wa->idx) << 56) | (nonce & 0x00ffffffffffffffULL);
        }

        /* (Re)build VM if the shared cache was re-keyed. Wait until a cache exists. */
        unsigned long long cg = atomic_load(&g_cache_gen);
        if (cg == 0) { msleep(50); continue; }
        if (cg != my_cache_gen || !vm) {
            if (vm) { randomx_destroy_vm(vm); vm = NULL; }
            pthread_mutex_lock(&g_cache_lock);
            vm = randomx_create_vm(g_flags, g_cache, NULL);
            if (!vm && (g_flags & RANDOMX_FLAG_JIT)) {
                /* Some Windows hosts refuse RWX JIT pages; fall back to interpreter. */
                randomx_flags soft = (randomx_flags)(g_flags & ~RANDOMX_FLAG_JIT);
                fprintf(stderr, "[rxminer] randomx_create_vm(JIT) failed; retrying flags=0x%x\n",
                        (unsigned)soft);
                vm = randomx_create_vm(soft, g_cache, NULL);
                if (vm) g_flags = soft;
            }
            pthread_mutex_unlock(&g_cache_lock);
            if (!vm) { fprintf(stderr, "FATAL: randomx_create_vm failed flags=0x%x\n", (unsigned)g_flags); exit(1); }
            fprintf(stderr, "[rxminer] thread %d VM ready flags=0x%x\n", wa->idx, (unsigned)g_flags);
            my_cache_gen = cg;
        }

        /* Hash a batch of nonces before re-checking for new work. */
        for (int i = 0; i < 256; i++) {
            /* nonce64 little-endian at offset 80 */
            for (int b = 0; b < 8; b++) blob[BLOB_PREFIX_LEN + b] = (uint8_t)(nonce >> (8 * b));
            randomx_calculate_hash(vm, blob, BLOB_LEN, digest);
            atomic_fetch_add(&g_hashes, 1);

            if (le256_cmp(digest, local_target) <= 0) {
                /* Report the hash in the node's getrandomxhash orientation (GetHex =
                 * reverse of the internal little-endian digest) so the pool's string
                 * compare and big-endian target math line up with consensus. */
                uint8_t digrev[32];
                for (int k = 0; k < 32; k++) digrev[k] = digest[31 - k];
                char hashhex[65];
                bin2hex(digrev, 32, hashhex);
                char noncestr[32];
                snprintf(noncestr, sizeof(noncestr), "%llu", (unsigned long long)nonce);
                json_t* p = json_object();
                json_object_set_new(p, "job_id", json_string(local_job));
                json_object_set_new(p, "nonce64", json_string(noncestr));
                json_object_set_new(p, "hash", json_string(hashhex));
                json_t* m = json_object();
                json_object_set_new(m, "id", json_integer(atomic_fetch_add(&g_msg_id, 1)));
                json_object_set_new(m, "method", json_string("submit"));
                json_object_set_new(m, "params", p);
                send_json(m);
            }
            nonce++;

            if (atomic_load(&g_job.generation) != local_gen) break; /* new work */
        }
    }
    return NULL;
}

/* -------- network / job parsing -------- */
static void apply_job(json_t* params)
{
    const char* job_id = json_string_value(json_object_get(params, "job_id"));
    const char* prefix_hex = json_string_value(json_object_get(params, "blob_prefix"));
    const char* seed_hex = json_string_value(json_object_get(params, "seed_key"));
    const char* target_hex = json_string_value(json_object_get(params, "target"));
    json_int_t height = json_integer_value(json_object_get(params, "height"));
    if (!job_id || !prefix_hex || !seed_hex || !target_hex) {
        fprintf(stderr, "[rxminer] malformed job, ignoring\n");
        return;
    }
    uint8_t prefix[BLOB_PREFIX_LEN], seed[64], target[32];
    if (hex2bin(prefix_hex, prefix, sizeof(prefix)) != BLOB_PREFIX_LEN) {
        fprintf(stderr, "[rxminer] bad blob_prefix len\n"); return;
    }
    int sl = hex2bin(seed_hex, seed, sizeof(seed));
    if (sl <= 0) { fprintf(stderr, "[rxminer] bad seed_key\n"); return; }
    if (hex2bin(target_hex, target, sizeof(target)) != 32) {
        fprintf(stderr, "[rxminer] bad target len (need 32-byte LE)\n"); return;
    }

    pthread_mutex_lock(&g_job.lock);
    memcpy(g_job.prefix, prefix, BLOB_PREFIX_LEN);
    memcpy(g_job.seed, seed, sl);
    g_job.seed_len = (size_t)sl;
    memcpy(g_job.target, target, 32);
    snprintf(g_job.job_id, sizeof(g_job.job_id), "%s", job_id);
    g_job.height = (int)height;
    g_job.have_job = true;
    atomic_fetch_add(&g_job.generation, 1);
    pthread_mutex_unlock(&g_job.lock);
    fprintf(stderr, "[rxminer] new job %s height=%d\n", job_id, (int)height);
}

static void handle_line(const char* line)
{
    json_error_t err;
    json_t* root = json_loads(line, 0, &err);
    if (!root) return;
    const char* method = json_string_value(json_object_get(root, "method"));
    if (method && strcmp(method, "job") == 0) {
        apply_job(json_object_get(root, "params"));
    } else {
        json_t* res = json_object_get(root, "result");
        if (res && json_is_object(res)) {
            json_t* ok = json_object_get(res, "ok");
            json_t* blk = json_object_get(res, "block");
            if (ok && json_is_true(ok)) {
                atomic_fetch_add(&g_accepted, 1);
                if (blk && json_is_true(blk)) {
                    atomic_fetch_add(&g_blocks, 1);
                    fprintf(stderr, "[rxminer] *** BLOCK FOUND accepted by pool ***\n");
                } else {
                    fprintf(stderr, "[rxminer] share accepted\n");
                }
            } else if (res) {
                const char* e = json_string_value(json_object_get(res, "error"));
                fprintf(stderr, "[rxminer] share rejected: %s\n", e ? e : "?");
            }
        }
    }
    json_decref(root);
}

static void* stats_thread(void* arg)
{
    (void)arg;
    unsigned long long last = 0;
    for (;;) {
        msleep(10000);
        unsigned long long h = atomic_load(&g_hashes);
        double hps = (double)(h - last) / 10.0;
        last = h;
        fprintf(stderr, "[rxminer] %.1f H/s | accepted=%llu blocks=%llu\n",
                hps, (unsigned long long)atomic_load(&g_accepted),
                (unsigned long long)atomic_load(&g_blocks));
    }
    return NULL;
}

int main(int argc, char** argv)
{
    const char* host = "127.0.0.1";
    int port = 5559;
    int threads = 1;
    snprintf(g_address, sizeof(g_address), "%s", "");
    snprintf(g_worker, sizeof(g_worker), "%s", "rxminer");

    /* When stderr is a pipe (wallet spawn), MSVC/mingw fully-buffer it and a
     * crash before fflush shows as a silent "CPU miner exited" with no lines.
     * Force line buffering so every debug line reaches the wallet log. */
    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);
    fprintf(stderr, "[rxminer] starting hobc-rxminer/1.1 pid-ish argv0=%s\n",
            (argc > 0 && argv[0]) ? argv[0] : "?");
    fflush(stderr);

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) {
            char* url = argv[++i];
            char* c = strrchr(url, ':');
            if (c) { *c = '\0'; host = url; port = atoi(c + 1); }
            else host = url;
        } else if (!strcmp(argv[i], "-u") && i + 1 < argc) {
            snprintf(g_address, sizeof(g_address), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "-w") && i + 1 < argc) {
            snprintf(g_worker, sizeof(g_worker), "%s", argv[++i]);
        } else if (!strcmp(argv[i], "-t") && i + 1 < argc) {
            threads = atoi(argv[++i]); if (threads < 1) threads = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Usage: %s -o host:port -u <hobc_address> [-w worker] [-t threads]\n", argv[0]);
            return 0;
        }
    }
    fprintf(stderr, "[rxminer] args host=%s port=%d address=%s worker=%s threads=%d\n",
            host, port, g_address[0] ? g_address : "(empty)", g_worker, threads);
    if (!g_address[0]) { fprintf(stderr, "ERROR: -u <hobc_address> required\n"); return 1; }

    g_flags = randomx_get_flags();
    fprintf(stderr, "[rxminer] randomx_get_flags=0x%x (JIT=%d LARGE_PAGES=%d HARD_AES=%d FULL_MEM=%d SECURE=%d ARGON2_SSSE3=%d ARGON2_AVX2=%d)\n",
            (unsigned)g_flags,
            !!(g_flags & RANDOMX_FLAG_JIT),
            !!(g_flags & RANDOMX_FLAG_LARGE_PAGES),
            !!(g_flags & RANDOMX_FLAG_HARD_AES),
            !!(g_flags & RANDOMX_FLAG_FULL_MEM),
            !!(g_flags & RANDOMX_FLAG_SECURE),
            !!(g_flags & RANDOMX_FLAG_ARGON2_SSSE3),
            !!(g_flags & RANDOMX_FLAG_ARGON2_AVX2));
    /* Light-mode only: portable; full dataset / large pages not required for consensus. */
    g_flags = (randomx_flags)(g_flags & ~(RANDOMX_FLAG_FULL_MEM | RANDOMX_FLAG_LARGE_PAGES));
    fprintf(stderr, "[rxminer] using flags=0x%x (forced light mode, no large pages)\n", (unsigned)g_flags);

    pthread_mutex_init(&g_job.lock, NULL);
    g_job.have_job = false;
    atomic_store(&g_job.generation, 0);

#ifdef _WIN32
    { WSADATA wsa; if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { fprintf(stderr, "ERROR: WSAStartup failed\n"); return 1; } }
    fprintf(stderr, "[rxminer] WSAStartup ok\n");
#endif

    /* connect */
    struct addrinfo hints, *ai;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    char portstr[16]; snprintf(portstr, sizeof(portstr), "%d", port);
    fprintf(stderr, "[rxminer] resolving %s:%s …\n", host, portstr);
    if (getaddrinfo(host, portstr, &hints, &ai) != 0) {
        fprintf(stderr, "ERROR: cannot resolve %s\n", host); return 1;
    }
    g_sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (g_sock == SOCK_INVALID || connect(g_sock, ai->ai_addr, (int)ai->ai_addrlen) != 0) {
        fprintf(stderr, "ERROR: cannot connect to %s:%d\n", host, port); return 1;
    }
    freeaddrinfo(ai);
    int one = 1; setsockopt(g_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one));
    fprintf(stderr, "[rxminer] connected to %s:%d as %s/%s, %d thread(s)\n", host, port, g_address, g_worker, threads);

    /* subscribe + authorize */
    { json_t* m = json_object(); json_object_set_new(m, "id", json_integer(1));
      json_object_set_new(m, "method", json_string("subscribe"));
      json_t* p = json_object(); json_object_set_new(p, "agent", json_string("hobc-rxminer/1.1"));
      json_object_set_new(m, "params", p); send_json(m); }
    { json_t* m = json_object(); json_object_set_new(m, "id", json_integer(2));
      json_object_set_new(m, "method", json_string("authorize"));
      json_t* p = json_object(); json_object_set_new(p, "address", json_string(g_address));
      json_object_set_new(p, "worker", json_string(g_worker));
      json_object_set_new(m, "params", p); send_json(m); }

    /* start workers + stats */
    pthread_t* tids = calloc(threads, sizeof(pthread_t));
    worker_arg_t* wargs = calloc(threads, sizeof(worker_arg_t));
    for (int i = 0; i < threads; i++) {
        wargs[i].idx = i; wargs[i].nthreads = threads;
        pthread_create(&tids[i], NULL, worker, &wargs[i]);
    }
    pthread_t st; pthread_create(&st, NULL, stats_thread, NULL);

    /* read loop */
    char buf[8192]; size_t used = 0;
    for (;;) {
        int r = sock_read(g_sock, buf + used, sizeof(buf) - used - 1);
        if (r <= 0) { fprintf(stderr, "[rxminer] disconnected\n"); break; }
        used += (size_t)r; buf[used] = '\0';
        char* start = buf; char* nl;
        while ((nl = memchr(start, '\n', used - (start - buf))) != NULL) {
            *nl = '\0';
            if (nl > start) handle_line(start);
            start = nl + 1;
        }
        size_t rem = used - (start - buf);
        memmove(buf, start, rem); used = rem;
    }
    return 0;
}
