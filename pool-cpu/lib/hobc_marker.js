'use strict';
/*
 * HOBC V6 coinbase telemetry marker builder (KPSS GPU pool, KawPow).
 *
 * Wire format matches the node consensus parser and SHA ckpool builder:
 *   magic "HOBC" (4) + version 1 (1) + canonical-order TLVs:
 *     0x01 pool_id            len 1   REQUIRED
 *     0x02 algo               len 1   REQUIRED (0=sha256 1=kawpow 2=randomx)
 *     0x03 winning_share_diff len 4   REQUIRED (IEEE-754 float32, little-endian)
 *     0x04 pool_site          len<=24 optional (consensus max; last chars kept)
 *     0x06 worker_name        len 16  optional but always emitted (space-padded)
 *
 * Rig (0x05) is omitted so site/worker fit under the 100-byte scriptSig cap with
 * headroom. Long text fields keep the last N characters.
 */

const SITE_MAX = 24;
const WORKER_LEN = 16;

// difficulty-1 target as used for compact-bits difficulty (0x1d00ffff)
const DIFF1 = 0xffff * Math.pow(2, 8 * (0x1d - 3));

function bitsToDifficulty(bitsHex) {
  const bits = parseInt(bitsHex, 16) >>> 0;
  const exponent = bits >>> 24;
  const mantissa = bits & 0x007fffff;
  const target = mantissa * Math.pow(2, 8 * (exponent - 3));
  if (!isFinite(target) || target <= 0) return 0;
  return DIFF1 / target;
}

/** Space-pad to len; keep the last len chars when src is longer. */
function fillField(len, src) {
  const out = Buffer.alloc(len, 0x20);
  if (!src) return out;
  let s = '';
  const raw = String(src);
  for (let i = 0; i < raw.length; i++) {
    const c = raw.charCodeAt(i);
    if (c >= 0x20 && c <= 0x7e) s += raw[i];
  }
  if (s.length > len) s = s.slice(s.length - len);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}

function workerLabel(workerName) {
  if (!workerName) return '';
  const s = String(workerName);
  const dot = s.lastIndexOf('.');
  return (dot >= 0 && dot + 1 < s.length) ? s.slice(dot + 1) : s;
}

function rigLabel(userAgent) {
  if (!userAgent) return '';
  const s = String(userAgent);
  let out = '';
  for (let i = 0; i < s.length; i++) {
    const c = s.charCodeAt(i);
    if (c < 0x20 || c > 0x7e) continue;
    if (s[i] === '/' || s[i] === ' ') break;
    out += s[i];
    if (out.length >= 64) break;
  }
  return out;
}

function buildMarker(poolId, algo, winningShareDiff, poolSite, rigModel, workerName) {
  const parts = [];
  parts.push(Buffer.from('HOBC', 'ascii'));
  parts.push(Buffer.from([1]));

  parts.push(Buffer.from([0x01, 1, poolId & 0xff]));
  parts.push(Buffer.from([0x02, 1, algo & 0xff]));

  const diffBuf = Buffer.alloc(6);
  diffBuf[0] = 0x03; diffBuf[1] = 4;
  diffBuf.writeFloatLE(Math.fround(winningShareDiff || 0), 2);
  parts.push(diffBuf);

  if (poolSite && String(poolSite).length) {
    const site = fillField(SITE_MAX, poolSite);
    let sl = SITE_MAX;
    while (sl > 0 && site[sl - 1] === 0x20) sl--;
    if (sl > 0) {
      parts.push(Buffer.concat([Buffer.from([0x04, sl]), site.slice(0, sl)]));
    }
  }

  // 0x05 rig omitted — budget reserved for site/worker + scriptSig headroom
  void rigModel;

  const worker = fillField(WORKER_LEN, workerName ? workerLabel(workerName) : '');
  parts.push(Buffer.concat([Buffer.from([0x06, WORKER_LEN]), worker]));

  return Buffer.concat(parts);
}

function patchMarkerIdentity(marker, rigModel, workerName) {
  if (!Buffer.isBuffer(marker) || marker.length < 5) return 0;
  if (marker.toString('ascii', 0, 4) !== 'HOBC' || marker[4] !== 1) return 0;
  void rigModel;
  const worker = fillField(WORKER_LEN, workerName);
  let i = 5;
  let patched = 0;
  while (i + 2 <= marker.length) {
    const t = marker[i];
    const l = marker[i + 1];
    if (i + 2 + l > marker.length) break;
    if (t === 0x06 && l === WORKER_LEN) {
      worker.copy(marker, i + 2);
      patched++;
    }
    i += 2 + l;
  }
  return patched;
}

function patchTxIdentity(txHex, rigModel, workerName) {
  const buf = Buffer.from(String(txHex), 'hex');
  const magic = Buffer.from('HOBC', 'ascii');
  const idx = buf.indexOf(magic);
  if (idx < 0 || idx + 5 > buf.length || buf[idx + 4] !== 1) return null;
  const marker = buf.slice(idx);
  if (patchMarkerIdentity(marker, rigModel, workerName) <= 0) return null;
  return buf.toString('hex');
}

module.exports = {
  buildMarker,
  bitsToDifficulty,
  workerLabel,
  rigLabel,
  patchMarkerIdentity,
  patchTxIdentity,
  WORKER_LEN,
  SITE_MAX,
};
