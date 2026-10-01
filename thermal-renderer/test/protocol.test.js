import assert from "node:assert/strict";
import test from "node:test";

import { crc32 } from "../src/crc32.js";
import {
  buildTpbDocument,
  encodePackBits
} from "../src/protocol.js";

function decodePackBits(input) {
  const output = [];

  for (let position = 0; position < input.length;) {
    const control = input[position++];

    if (control <= 127) {
      const length = control + 1;

      for (let index = 0; index < length; index++) {
        output.push(input[position++]);
      }
    } else if (control >= 129) {
      const value = input[position++];

      for (let index = 0; index < 257 - control; index++) {
        output.push(value);
      }
    }
  }

  return Buffer.from(output);
}

test("CRC32 matches the standard vector", () => {
  assert.equal(crc32(Buffer.from("123456789")), 0xcbf43926);
});

test("PackBits round trips literal and run boundaries", () => {
  for (const length of [0, 1, 2, 3, 127, 128, 129, 255, 256, 1024]) {
    const literal = Buffer.alloc(length);

    for (let index = 0; index < length; index++) {
      literal[index] = (index * 67 + length) & 0xff;
    }

    assert.deepEqual(
      decodePackBits(encodePackBits(literal)),
      literal
    );

    const run = Buffer.alloc(length, 0x5a);

    assert.deepEqual(
      decodePackBits(encodePackBits(run)),
      run
    );
  }
});

test("TPB document contains verifiable bands and end record", () => {
  const width = 384;
  const height = 5;
  const bitmap = Buffer.alloc((width / 8) * height, 0xaa);
  const source = "协议测试";
  const document = buildTpbDocument({
    bitmap,
    width,
    height,
    bandHeight: 2,
    compression: "packbits",
    source
  });

  assert.equal(document.subarray(0, 4).toString("ascii"), "TPB1");
  assert.equal(document.readUInt16LE(10), width);
  assert.equal(document.readUInt32LE(12), height);
  assert.equal(document.readUInt16LE(18), 3);
  assert.equal(document.readUInt32LE(20), bitmap.length);
  assert.equal(document.readUInt32LE(24), crc32(Buffer.from(source)));
  assert.equal(document.readUInt32LE(28), crc32(document.subarray(0, 28)));

  let position = 32;
  const decoded = [];

  for (let index = 0; index < 3; index++) {
    assert.equal(
      document.subarray(position, position + 4).toString("ascii"),
      "BAND"
    );

    const rawLength = document.readUInt32LE(position + 12);
    const payloadLength = document.readUInt32LE(position + 16);
    const payload = document.subarray(
      position + 24,
      position + 24 + payloadLength
    );
    const raw = decodePackBits(payload);

    assert.equal(raw.length, rawLength);
    assert.equal(
      document.readUInt32LE(position + 20),
      crc32(raw)
    );
    decoded.push(raw);
    position += 24 + payloadLength;
  }

  assert.equal(
    document.subarray(position, position + 4).toString("ascii"),
    "END!"
  );
  assert.deepEqual(Buffer.concat(decoded), bitmap);
  assert.equal(document.readUInt32LE(position + 12), crc32(bitmap));
  assert.equal(position + 16, document.length);
});
