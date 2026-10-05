// Intrinsic image size from the file header (plan P0-11): PNG, GIF, WebP and
// JPEG carry their pixel size in the first bytes, so the worker reads a
// prefix of the response instead of decoding the whole bitmap. Returns
// { w, h }, { more: true } when the prefix ended before the answer, or null
// when the format is unknown (SVG, AVIF, …: the caller decodes instead).
// JPEG honours the EXIF orientation the way browsers do (5–8 swap axes).

const be16 = (b, i) => (b[i] << 8) | b[i + 1];
const be32 = (b, i) => ((b[i] << 24) >>> 0) + (b[i + 1] << 16) + (b[i + 2] << 8) + b[i + 3];
const le16 = (b, i) => b[i] | (b[i + 1] << 8);
const le24 = (b, i) => b[i] | (b[i + 1] << 8) | (b[i + 2] << 16);
const ascii = (b, i, s) => [...s].every((c, k) => b[i + k] === c.charCodeAt(0));

export function sniffImageSize(b) {
  if (b.length < 4) return { more: true };
  if (b[0] === 0x89 && ascii(b, 1, 'PNG')) {
    if (b.length < 24) return { more: true };
    return ascii(b, 12, 'IHDR') ? { w: be32(b, 16), h: be32(b, 20) } : null;
  }
  if (ascii(b, 0, 'GIF8')) {
    if (b.length < 10) return { more: true };
    return { w: le16(b, 6), h: le16(b, 8) };
  }
  if (ascii(b, 0, 'RIFF')) {
    if (b.length < 30) return { more: true };
    if (!ascii(b, 8, 'WEBP')) return null;
    if (ascii(b, 12, 'VP8 ')) return { w: le16(b, 26) & 0x3fff, h: le16(b, 28) & 0x3fff };
    if (ascii(b, 12, 'VP8L')) {
      const bits = b[21] | (b[22] << 8) | (b[23] << 16) | (b[24] << 24);
      return { w: (bits & 0x3fff) + 1, h: ((bits >>> 14) & 0x3fff) + 1 };
    }
    if (ascii(b, 12, 'VP8X')) return { w: le24(b, 24) + 1, h: le24(b, 27) + 1 };
    return null;
  }
  if (b[0] === 0xff && b[1] === 0xd8) return sniffJpeg(b);
  return null;
}

function sniffJpeg(b) {
  let orientation = 1;
  let i = 2;
  for (;;) {
    if (i + 4 > b.length) return { more: true };
    if (b[i] !== 0xff) return null;
    const m = b[i + 1];
    if (m === 0xff) { i++; continue; }  // fill byte
    if (m === 0xd8 || (m >= 0xd0 && m <= 0xd7) || m === 0x01) { i += 2; continue; }
    const len = be16(b, i + 2);
    if (len < 2) return null;
    if (m === 0xe1 && i + 4 + len - 2 <= b.length && ascii(b, i + 4, 'Exif\0\0'))
      orientation = exifOrientation(b, i + 10, i + 2 + len) ?? orientation;
    const sof = m >= 0xc0 && m <= 0xcf && m !== 0xc4 && m !== 0xc8 && m !== 0xcc;
    if (sof) {
      if (i + 9 > b.length) return { more: true };
      const h = be16(b, i + 5), w = be16(b, i + 7);
      return orientation >= 5 && orientation <= 8 ? { w: h, h: w } : { w, h };
    }
    if (m === 0xda || m === 0xd9) return null;  // scan data before any SOF
    i += 2 + len;
  }
}

// TIFF IFD0 tag 0x0112 inside the Exif APP1 segment [t, end)
function exifOrientation(b, t, end) {
  if (t + 8 > end) return null;
  const le = b[t] === 0x49;  // "II" little endian, "MM" big endian
  const u16 = (i) => (le ? le16(b, i) : be16(b, i));
  const u32 = (i) => (le ? (b[i] | (b[i + 1] << 8) | (b[i + 2] << 16)) + b[i + 3] * 0x1000000 : be32(b, i));
  const ifd = t + u32(t + 4);
  if (ifd + 2 > end) return null;
  const n = u16(ifd);
  for (let k = 0; k < n; k++) {
    const e = ifd + 2 + k * 12;
    if (e + 12 > end) return null;
    if (u16(e) === 0x0112) return u16(e + 8);
  }
  return null;
}
