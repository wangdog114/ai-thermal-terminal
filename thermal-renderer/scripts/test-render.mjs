import assert from "node:assert/strict";
import fs from "node:fs/promises";

import { crc32 } from "../src/crc32.js";

const renderUrl = new URL(
  process.env.RENDER_URL ||
  "http://127.0.0.1:3000/render"
);

const token =
  process.env.RENDER_TOKEN ||
  "development-token";

const authorization = `Bearer ${token}`;

function decodePackBits(input) {
  const output = [];
  let position = 0;

  while (position < input.length) {
    const control = input[position++];

    if (control <= 127) {
      const length = control + 1;
      const end = position + length;

      assert.ok(end <= input.length, "PackBits literal 越界");

      for (; position < end; position++) {
        output.push(input[position]);
      }
    } else if (control >= 129) {
      assert.ok(position < input.length, "PackBits run 缺少字节");

      const value = input[position++];
      const length = 257 - control;

      for (let index = 0; index < length; index++) {
        output.push(value);
      }
    }
  }

  return Buffer.from(output);
}

function verifyTpb(document, source) {
  assert.equal(document.subarray(0, 4).toString("ascii"), "TPB1");
  assert.equal(document.readUInt8(4), 1);
  assert.equal(document.readUInt8(5), 0b00000111);
  assert.equal(document.readUInt8(6), 1);
  assert.equal(document.readUInt16LE(8), 32);
  assert.equal(document.readUInt16LE(10), 384);
  assert.equal(
    document.readUInt32LE(24),
    crc32(Buffer.from(source, "utf8"))
  );
  assert.equal(
    document.readUInt32LE(28),
    crc32(document.subarray(0, 28))
  );

  const compression = document.readUInt8(7);
  const height = document.readUInt32LE(12);
  const bandCount = document.readUInt16LE(18);
  const expectedRawLength = document.readUInt32LE(20);
  const rawBands = [];
  let position = 32;

  for (let index = 0; index < bandCount; index++) {
    assert.equal(
      document.subarray(position, position + 4).toString("ascii"),
      "BAND"
    );
    assert.equal(document.readUInt16LE(position + 4), index);

    const rawLength = document.readUInt32LE(position + 12);
    const payloadLength = document.readUInt32LE(position + 16);
    const rawCrc = document.readUInt32LE(position + 20);
    const payload = document.subarray(
      position + 24,
      position + 24 + payloadLength
    );

    const raw =
      compression === 1
        ? decodePackBits(payload)
        : Buffer.from(payload);

    assert.equal(raw.length, rawLength);
    assert.equal(crc32(raw), rawCrc);
    rawBands.push(raw);
    position += 24 + payloadLength;
  }

  assert.equal(
    document.subarray(position, position + 4).toString("ascii"),
    "END!"
  );

  const bitmap = Buffer.concat(rawBands);

  assert.equal(bitmap.length, expectedRawLength);
  assert.equal(bitmap.length, 48 * height);
  assert.equal(document.readUInt32LE(position + 12), crc32(bitmap));
  assert.equal(position + 16, document.length);

  return {
    width: 384,
    height,
    bandCount,
    rawBytes: bitmap.length,
    documentBytes: document.length
  };
}

async function request(path, options = {}) {
  const url = new URL(path, renderUrl);

  return fetch(url, {
    ...options,
    signal: AbortSignal.timeout(60_000)
  });
}

async function expectStatus(response, expectedStatus) {
  const message =
    response.status === expectedStatus
      ? undefined
      : await response.text();

  assert.equal(response.status, expectedStatus, message);
}

async function render(markdown, format, options = {}) {
  return request(`/render?format=${format}`, {
    method: "POST",
    headers: {
      "Authorization": authorization,
      "Content-Type": "application/json"
    },
    body: JSON.stringify({ markdown, options })
  });
}

const health = await request("/health", {
  headers: { "Authorization": authorization }
});

await expectStatus(health, 200);

const healthBody = await health.json();

assert.equal(healthBody.ok, true);
assert.equal(healthBody.width, 384);
assert.equal(healthBody.assets.ready, true);
assert.equal(healthBody.chromium.ready, true);
assert.ok(healthBody.probe.height < 256);

const unauthorizedHealth = await request("/health");
assert.equal(unauthorizedHealth.status, 401);

const empty = await render("   ", "png");
assert.equal(empty.status, 400);
assert.equal((await empty.json()).error.code, "EMPTY_MARKDOWN");

const short = await render("A", "png", {
  margin: 0,
  bottomFeed: 0
});

await expectStatus(short, 200);
assert.equal(short.headers.get("content-type"), "image/png");
assert.ok(Number(short.headers.get("x-tpb-height")) < 256);

const blockFormula = await render("$$\nx^2+y^2=z^2\n$$", "png");
await expectStatus(blockFormula, 200);

const longFormula =
  "前缀 $x_{" +
  "1234567890".repeat(2) +
  "}=y$ 后缀";

const longFormulaResponse = await render(longFormula, "png");
await expectStatus(longFormulaResponse, 200);
assert.equal(
  longFormulaResponse.headers.get("x-render-overflow"),
  null
);

const unreadableFormula =
  "$x_{" +
  "1234567890".repeat(10) +
  "}=y$";

const unreadableFormulaResponse = await render(
  unreadableFormula,
  "png"
);

assert.equal(unreadableFormulaResponse.status, 422);
assert.equal(
  (await unreadableFormulaResponse.json()).error.code,
  "FORMULA_TOO_WIDE"
);

const markdown = await fs.readFile(
  new URL("../examples/sample.md", import.meta.url),
  "utf8"
);

const tpbResponse = await render(markdown, "tpb");
await expectStatus(tpbResponse, 200);
assert.equal(
  tpbResponse.headers.get("content-type"),
  "application/vnd.thermal-bitmap"
);

const document = Buffer.from(await tpbResponse.arrayBuffer());
const tpb = verifyTpb(document, markdown);

for (const privatePath of [
  "/src/render.js",
  "/package-lock.json",
  "/assets/fonts/SourceHanSansSC-Regular.otf",
  "/api/render"
]) {
  const privateResponse = await request(privatePath);
  assert.equal(
    privateResponse.status,
    404,
    `${privatePath} 不应公开`
  );
}

console.log({
  healthDurationMs: healthBody.durationMs,
  shortHeight: Number(short.headers.get("x-tpb-height")),
  longFormulaHeight: Number(
    longFormulaResponse.headers.get("x-tpb-height")
  ),
  tpb
});
