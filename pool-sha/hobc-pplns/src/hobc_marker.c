/*
 * HOBC V6 coinbase telemetry marker builder (pool side). See hobc_marker.h.
 */
#include "hobc_marker.h"

#include <ctype.h>
#include <string.h>

void hobc_marker_fill_field(char *dst, size_t dstlen, const char *src)
{
	size_t i, len;
	const char *use;

	if (!dst || dstlen == 0)
		return;
	memset(dst, ' ', dstlen);
	if (!src || !src[0])
		return;
	/* src must be a NUL-terminated C string. Keep the last dstlen chars when longer. */
	len = strlen(src);
	use = src;
	if (len > dstlen) {
		use = src + (len - dstlen);
		len = dstlen;
	}
	for (i = 0; i < len; i++) {
		unsigned char c = (unsigned char)use[i];
		dst[i] = (c >= 0x20 && c <= 0x7e) ? (char)c : ' ';
	}
}

void hobc_marker_worker_label(char *dst, size_t dstlen, const char *workername)
{
	const char *src = workername;
	const char *dot;

	if (!dst || dstlen == 0)
		return;
	if (!workername || !workername[0]) {
		hobc_marker_fill_field(dst, dstlen, "");
		return;
	}
	/* Prefer the last dotted suffix (rig / worker id), not the payout address. */
	dot = strrchr(workername, '.');
	if (dot && dot[1])
		src = dot + 1;
	/* fill_field keeps the last dstlen chars when the suffix is still too long. */
	hobc_marker_fill_field(dst, dstlen, src);
}

void hobc_marker_rig_label(char *dst, size_t dstlen, const char *useragent)
{
	char tmp[64];
	size_t i, n = 0;

	if (!dst || dstlen == 0)
		return;
	if (!useragent || !useragent[0]) {
		hobc_marker_fill_field(dst, dstlen, "");
		return;
	}
	for (i = 0; useragent[i] && n < sizeof(tmp) - 1; i++) {
		unsigned char c = (unsigned char)useragent[i];
		if (c < 0x20 || c > 0x7e)
			continue;
		if (c == '/' || c == ' ')
			break;
		tmp[n++] = (char)c;
	}
	tmp[n] = '\0';
	hobc_marker_fill_field(dst, dstlen, tmp);
}

int hobc_build_marker(uint8_t *out, int pool_id, int algo,
		      float winning_share_diff, const char *pool_site,
		      const char *rig_model, const char *worker_name)
{
	char site[HOBC_MARKER_SITE_MAX];
	char worker[HOBC_MARKER_WORKER_LEN];
	int n = 0;
	size_t sl;

	(void)rig_model; /* 0x05 omitted — budget goes to site/worker + 1-byte headroom */

	if (!out)
		return 0;

	hobc_marker_fill_field(site, sizeof(site), pool_site);
	sl = HOBC_MARKER_SITE_MAX;
	while (sl > 0 && site[sl - 1] == ' ')
		sl--;

	/* magic + version */
	out[n++] = 'H';
	out[n++] = 'O';
	out[n++] = 'B';
	out[n++] = 'C';
	out[n++] = 1;

	/* 0x01 pool_id (len 1) */
	out[n++] = 0x01;
	out[n++] = 1;
	out[n++] = (uint8_t)(pool_id & 0xff);

	/* 0x02 algo (len 1) */
	out[n++] = 0x02;
	out[n++] = 1;
	out[n++] = (uint8_t)(algo & 0xff);

	/* 0x03 winning_share_diff (len 4, float32 little-endian) */
	out[n++] = 0x03;
	out[n++] = 4;
	memcpy(out + n, &winning_share_diff, 4);
	n += 4;

	/* 0x04 pool_site (optional; omit when empty) */
	if (sl > 0) {
		out[n++] = 0x04;
		out[n++] = (uint8_t)sl;
		memcpy(out + n, site, sl);
		n += (int)sl;
	}

	/* 0x06 worker_name — always fixed length (space-padded) for in-place patch */
	if (worker_name && worker_name[0])
		hobc_marker_worker_label(worker, sizeof(worker), worker_name);
	else
		hobc_marker_fill_field(worker, sizeof(worker), "");
	out[n++] = 0x06;
	out[n++] = (uint8_t)HOBC_MARKER_WORKER_LEN;
	memcpy(out + n, worker, HOBC_MARKER_WORKER_LEN);
	n += HOBC_MARKER_WORKER_LEN;

	return n;
}

int hobc_marker_patch_identity(uint8_t *marker, int mlen,
			       const char *rig_model, const char *worker_name)
{
	char worker[HOBC_MARKER_WORKER_LEN];
	int i = 5;
	int patched = 0;

	(void)rig_model;

	if (!marker || mlen < 5)
		return 0;
	if (marker[0] != 'H' || marker[1] != 'O' || marker[2] != 'B' ||
	    marker[3] != 'C' || marker[4] != 1)
		return 0;

	/* Callers pass already-labeled, space-padded fixed worker field (no NUL). */
	memset(worker, ' ', sizeof(worker));
	if (worker_name)
		memcpy(worker, worker_name, HOBC_MARKER_WORKER_LEN);

	while (i + 2 <= mlen) {
		uint8_t t = marker[i];
		uint8_t l = marker[i + 1];

		if (i + 2 + l > mlen)
			break;
		if (t == 0x06 && l == HOBC_MARKER_WORKER_LEN) {
			memcpy(marker + i + 2, worker, HOBC_MARKER_WORKER_LEN);
			patched++;
		}
		i += 2 + l;
	}
	return patched;
}

int hobc_coinb2_patch_identity(uint8_t *coinb2, int coinb2len,
			       const char *rig_model, const char *worker_name)
{
	int i;

	if (!coinb2 || coinb2len < 5)
		return 0;
	for (i = 0; i <= coinb2len - 5; i++) {
		if (coinb2[i] == 'H' && coinb2[i + 1] == 'O' &&
		    coinb2[i + 2] == 'B' && coinb2[i + 3] == 'C' &&
		    coinb2[i + 4] == 1)
			return hobc_marker_patch_identity(coinb2 + i, coinb2len - i,
							  rig_model, worker_name);
	}
	return 0;
}
