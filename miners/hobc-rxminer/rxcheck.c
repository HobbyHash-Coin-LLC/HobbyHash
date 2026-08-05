/*
 * rxcheck — build the HOBC RandomX 80-byte blob prefix exactly as the node
 * (randomx_check.cpp RandomXHeaderBlob) and pool will, hash prefix||nonce64,
 * and print the digest. Used to prove miner/pool/node byte-compatibility
 * against the node's getrandomxhash RPC.
 *
 * Args: version prevhex_display merklehex_display ntime nbits_hex height nonce64
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "randomx.h"

static int hexdec(const char* hex, uint8_t* out, size_t maxlen) {
    size_t n = strlen(hex);
    if (n % 2 || n / 2 > maxlen) return -1;
    for (size_t i = 0; i < n / 2; i++) { unsigned v; if (sscanf(hex + 2*i, "%2x", &v) != 1) return -1; out[i] = (uint8_t)v; }
    return (int)(n/2);
}
static void put_le32(uint8_t* p, uint32_t v){ p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }

int main(int argc, char** argv){
    if (argc != 8){ fprintf(stderr,"usage: rxcheck version prevhex merklehex ntime nbitshex height nonce64\n"); return 2; }
    uint32_t version = (uint32_t)strtoul(argv[1], NULL, 10);
    uint8_t prev_disp[32], merk_disp[32];
    if (hexdec(argv[2], prev_disp, 32)!=32){ fprintf(stderr,"bad prev\n"); return 2; }
    if (hexdec(argv[3], merk_disp, 32)!=32){ fprintf(stderr,"bad merkle\n"); return 2; }
    uint32_t ntime = (uint32_t)strtoul(argv[4], NULL, 10);
    uint32_t nbits = (uint32_t)strtoul(argv[5], NULL, 16);
    uint32_t height = (uint32_t)strtoul(argv[6], NULL, 10);
    uint64_t nonce = strtoull(argv[7], NULL, 10);

    /* internal byte order = reverse of RPC/display hex (matches ParseHashV + uint256) */
    uint8_t prev_int[32], merk_int[32];
    for (int i=0;i<32;i++){ prev_int[i]=prev_disp[31-i]; merk_int[i]=merk_disp[31-i]; }

    uint8_t prefix[80];
    int o=0;
    put_le32(prefix+o, version); o+=4;
    memcpy(prefix+o, prev_int, 32); o+=32;
    memcpy(prefix+o, merk_int, 32); o+=32;
    put_le32(prefix+o, ntime); o+=4;
    put_le32(prefix+o, nbits); o+=4;
    put_le32(prefix+o, height); o+=4; /* == 80 */

    uint8_t blob[88];
    memcpy(blob, prefix, 80);
    for (int b=0;b<8;b++) blob[80+b]=(uint8_t)(nonce>>(8*b));

    /* seed = "HOBCRX01" || epoch_le32, epoch = height/2048 */
    uint8_t seed[12] = {'H','O','B','C','R','X','0','1',0,0,0,0};
    put_le32(seed+8, height/2048);

    randomx_flags f = randomx_get_flags();
    randomx_cache* c = randomx_alloc_cache(f);
    randomx_init_cache(c, seed, sizeof(seed));
    randomx_vm* vm = randomx_create_vm(f, c, NULL);
    uint8_t dg[32];
    randomx_calculate_hash(vm, blob, 88, dg);

    char h[65]; static const char* d="0123456789abcdef";
    for (int i=0;i<32;i++){ h[2*i]=d[dg[i]>>4]; h[2*i+1]=d[dg[i]&0xf]; }
    h[64]='\0';
    /* print the digest in the SAME orientation as node getrandomxhash (uint256.GetHex = reverse of internal bytes) */
    char hrev[65];
    for (int i=0;i<32;i++){ hrev[2*i]=d[dg[31-i]>>4]; hrev[2*i+1]=d[dg[31-i]&0xf]; }
    hrev[64]='\0';
    printf("prefix_hex="); for(int i=0;i<80;i++) printf("%02x", prefix[i]); printf("\n");
    printf("digest_internal_le=%s\n", h);
    printf("digest_gethex=%s\n", hrev);
    randomx_destroy_vm(vm); randomx_release_cache(c);
    return 0;
}
