const CRC32_TABLE = new Uint32Array(256);

for (let i = 0; i < 256; i++) {
  let value = i;

  for (let bit = 0; bit < 8; bit++) {
    value =
      value & 1
        ? 0xedb88320 ^ (value >>> 1)
        : value >>> 1;
  }

  CRC32_TABLE[i] = value >>> 0;
}

export function crc32Init() {
  return 0xffffffff;
}

export function crc32Update(state, buffer) {
  let crc = state >>> 0;

  for (const byte of buffer) {
    crc =
      CRC32_TABLE[(crc ^ byte) & 0xff] ^
      (crc >>> 8);
  }

  return crc >>> 0;
}

export function crc32Final(state) {
  return (state ^ 0xffffffff) >>> 0;
}

export function crc32(buffer) {
  return crc32Final(
    crc32Update(crc32Init(), buffer)
  );
}

