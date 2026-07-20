/*
 * HOBC V6 coinbase telemetry marker builder (pool side).
 *
 * Emits the exact TLV wire format the node consensus rule requires (see
 * V6/src/src/consensus/hobc_marker.{h,cpp} and V6/docs/FORK_SPEC_V6.md):
 *   magic "HOBC" (4) + version 1 (1) + canonical-order TLVs:
 *     0x01 pool_id            len 1   REQUIRED
 *     0x02 algo               len 1   REQUIRED (0=sha256 1=kawpow 2=randomx)
 *     0x03 winning_share_diff len 4   REQUIRED (IEEE-754 float32, little-endian)
 *     0x04 pool_site          len<=24 optional (consensus max; last chars kept)
 *     0x06 worker_name        len 16  optional but always emitted (space-padded)
 *
 * Rig (0x05) is omitted so site/worker can use the scriptSig budget with 1+ byte
 * of headroom under the 100-byte coinbase cap. Fixed-size 0x06 keeps per-client
 * worker patches length-stable. Long text fields keep the last N characters.
 * Explorer trims trailing spaces when displaying.
 */
#ifndef HOBC_MARKER_H
#define HOBC_MARKER_H

#include <stddef.h>
#include <stdint.h>

#define HOBC_MARKER_MAX 64
#define HOBC_MARKER_SITE_MAX 24
#define HOBC_MARKER_WORKER_LEN 16

/*
 * Build the marker into out (must have room for HOBC_MARKER_MAX bytes).
 * winning_share_diff is stamped as the block's network difficulty at job time.
 * pool_site / worker_name may be NULL/empty (0x06 still emitted padded).
 * rig_model is ignored (0x05 not emitted).
 * Returns the number of bytes written.
 */
int hobc_build_marker(uint8_t *out, int pool_id, int algo,
		      float winning_share_diff, const char *pool_site,
		      const char *rig_model, const char *worker_name);

/* Truncate/pad helpers (space-padded, not NUL-terminated). Long src keeps last dstlen chars. */
void hobc_marker_fill_field(char *dst, size_t dstlen, const char *src);
void hobc_marker_worker_label(char *dst, size_t dstlen, const char *workername);
void hobc_marker_rig_label(char *dst, size_t dstlen, const char *useragent);

/* Overwrite fixed-size 0x06 worker payload inside an existing marker (same length).
 * rig_model is ignored (no 0x05 slot). */
int hobc_marker_patch_identity(uint8_t *marker, int mlen,
			       const char *rig_model, const char *worker_name);

/* Scan coinb2 for HOBC magic and patch worker TLV in place. */
int hobc_coinb2_patch_identity(uint8_t *coinb2, int coinb2len,
			       const char *rig_model, const char *worker_name);

#endif /* HOBC_MARKER_H */
