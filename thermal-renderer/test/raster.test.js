import assert from "node:assert/strict";
import test from "node:test";

import sharp from "sharp";

import { pngToMonoBitmap } from "../src/raster.js";

test("raster output is MSB-first with black represented by one", async () => {
  const pixels = Buffer.from([
    0, 255, 0, 255, 0, 255, 0, 255,
    255, 0, 255, 0, 255, 0, 255, 0
  ]);

  const png = await sharp(pixels, {
    raw: { width: 8, height: 2, channels: 1 }
  }).png().toBuffer();

  const result = await pngToMonoBitmap(
    png,
    8,
    2,
    { dither: "threshold", threshold: 127 }
  );

  assert.equal(result.rowBytes, 1);
  assert.deepEqual([...result.bitmap], [0xaa, 0x55]);
});

test("repairs faint one-pixel stroke gaps without joining white space", async () => {
  const width = 8;
  const height = 5;
  const pixels = Buffer.alloc(width * height, 255);
  const set = (x, y, value) => {
    pixels[y * width + x] = value;
  };

  set(1, 0, 0);
  set(1, 1, 220);
  set(1, 2, 0);
  set(3, 0, 0);
  set(3, 2, 0);
  set(5, 0, 0);
  set(6, 1, 225);
  set(7, 2, 0);
  set(4, 4, 0);
  set(5, 4, 210);
  set(6, 4, 0);
  set(0, 4, 220);

  const png = await sharp(pixels, {
    raw: { width, height, channels: 1 }
  }).png().toBuffer();

  const result = await pngToMonoBitmap(
    png, width, height,
    { dither: "threshold", threshold: 185 }
  );

  assert.deepEqual([...result.bitmap], [0x54, 0x42, 0x51, 0x00, 0x0e]);

  const dithered = await pngToMonoBitmap(
    png, width, height,
    { dither: "bayer4", threshold: 185 }
  );
  assert.equal(dithered.bitmap[1] & 0x40, 0);
});
