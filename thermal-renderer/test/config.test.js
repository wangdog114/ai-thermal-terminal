import assert from "node:assert/strict";
import test from "node:test";

import { normalizeOptions } from "../src/config.js";

test("normalizes numeric rendering options", () => {
  const options = normalizeOptions({
    fontSize: 100,
    lineHeight: 0,
    margin: -1,
    bandHeight: 300
  });

  assert.equal(options.fontSize, 30);
  assert.equal(options.lineHeight, 1.1);
  assert.equal(options.margin, 0);
  assert.equal(options.bandHeight, 300);
});

test("rejects unsupported enum values", () => {
  assert.throws(
    () => normalizeOptions({ compression: "gzip" }),
    error => error.code === "INVALID_OPTION"
  );
});
