import { crc32 } from "./crc32.js";

const FILE_HEADER_SIZE = 32;
const BAND_HEADER_SIZE = 24;
const END_RECORD_SIZE = 16;

const PIXEL_FORMAT_MONO_1BPP = 1;

const COMPRESSION_NONE = 0;
const COMPRESSION_PACKBITS = 1;

/*
 * 标准 PackBits 变体：
 *
 * 0..127:
 *   后面复制 control + 1 个原始字节
 *
 * 129..255:
 *   后面一个字节重复 257 - control 次
 *
 * 128:
 *   保留，不使用
 */
export function encodePackBits(input) {
  const output = [];
  let position = 0;

  function countRun(start) {
    let length = 1;

    while (
      start + length < input.length &&
      length < 128 &&
      input[start + length] === input[start]
    ) {
      length++;
    }

    return length;
  }

  while (position < input.length) {
    const runLength = countRun(position);

    if (runLength >= 3) {
      output.push(257 - runLength);
      output.push(input[position]);
      position += runLength;
      continue;
    }

    const literalStart = position;
    position++;

    while (
      position < input.length &&
      position - literalStart < 128
    ) {
      const nextRun = countRun(position);

      if (nextRun >= 3) {
        break;
      }

      position++;
    }

    const literalLength = position - literalStart;

    output.push(literalLength - 1);

    for (
      let i = literalStart;
      i < literalStart + literalLength;
      i++
    ) {
      output.push(input[i]);
    }
  }

  return Buffer.from(output);
}

function createFileHeader({
  width,
  height,
  bandHeight,
  bandCount,
  rawLength,
  compressionId,
  sourceCrc
}) {
  const header = Buffer.alloc(FILE_HEADER_SIZE, 0);

  header.write("TPB1", 0, 4, "ascii");

  header.writeUInt8(1, 4); // 协议版本

  /*
   * flags:
   * bit 0: 黑色像素为 1
   * bit 1: 字节内 MSB 对应左侧像素
   * bit 2: 图像从上到下
   */
  header.writeUInt8(0b00000111, 5);

  header.writeUInt8(
    PIXEL_FORMAT_MONO_1BPP,
    6
  );

  header.writeUInt8(compressionId, 7);
  header.writeUInt16LE(FILE_HEADER_SIZE, 8);
  header.writeUInt16LE(width, 10);
  header.writeUInt32LE(height, 12);
  header.writeUInt16LE(bandHeight, 16);
  header.writeUInt16LE(bandCount, 18);
  header.writeUInt32LE(rawLength, 20);
  header.writeUInt32LE(sourceCrc, 24);

  const headerCrc = crc32(header.subarray(0, 28));
  header.writeUInt32LE(headerCrc, 28);

  return header;
}

function createBandHeader({
  index,
  rows,
  y,
  rawLength,
  payloadLength,
  rawCrc
}) {
  const header = Buffer.alloc(BAND_HEADER_SIZE, 0);

  header.write("BAND", 0, 4, "ascii");
  header.writeUInt16LE(index, 4);
  header.writeUInt16LE(rows, 6);
  header.writeUInt32LE(y, 8);
  header.writeUInt32LE(rawLength, 12);
  header.writeUInt32LE(payloadLength, 16);
  header.writeUInt32LE(rawCrc, 20);

  return header;
}

function createEndRecord({
  bandCount,
  totalRawLength,
  bitmapCrc
}) {
  const record = Buffer.alloc(END_RECORD_SIZE, 0);

  record.write("END!", 0, 4, "ascii");
  record.writeUInt16LE(bandCount, 4);
  record.writeUInt16LE(0, 6); // status = success
  record.writeUInt32LE(totalRawLength, 8);
  record.writeUInt32LE(bitmapCrc, 12);

  return record;
}

export function buildTpbDocument({
  bitmap,
  width,
  height,
  bandHeight,
  compression,
  source
}) {
  if (width % 8 !== 0) {
    throw new Error(
      "当前协议要求图像宽度是 8 的倍数"
    );
  }

  const rowBytes = width / 8;
  const expectedLength = rowBytes * height;

  if (bitmap.length !== expectedLength) {
    throw new Error(
      `点阵长度错误：${bitmap.length}，` +
      `期望 ${expectedLength}`
    );
  }

  const bandCount = Math.ceil(height / bandHeight);

  if (bandCount > 0xffff) {
    throw new Error("点阵分块数量超过协议限制");
  }

  const compressionId =
    compression === "packbits"
      ? COMPRESSION_PACKBITS
      : COMPRESSION_NONE;

  const sourceBuffer = Buffer.from(source, "utf8");

  const chunks = [
    createFileHeader({
      width,
      height,
      bandHeight,
      bandCount,
      rawLength: bitmap.length,
      compressionId,
      sourceCrc: crc32(sourceBuffer)
    })
  ];

  for (
    let index = 0;
    index < bandCount;
    index++
  ) {
    const y = index * bandHeight;
    const rows = Math.min(
      bandHeight,
      height - y
    );

    const rawStart = y * rowBytes;
    const rawEnd = rawStart + rows * rowBytes;
    const raw = bitmap.subarray(rawStart, rawEnd);

    const payload =
      compression === "packbits"
        ? encodePackBits(raw)
        : raw;

    chunks.push(
      createBandHeader({
        index,
        rows,
        y,
        rawLength: raw.length,
        payloadLength: payload.length,
        rawCrc: crc32(raw)
      }),
      payload
    );
  }

  chunks.push(
    createEndRecord({
      bandCount,
      totalRawLength: bitmap.length,
      bitmapCrc: crc32(bitmap)
    })
  );

  return Buffer.concat(chunks);
}

