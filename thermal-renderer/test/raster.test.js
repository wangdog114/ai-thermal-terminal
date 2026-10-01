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
