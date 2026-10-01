export const PAGE_WIDTH = 384;

function readIntegerEnv(name, fallback, min, max) {
  const value = Number.parseInt(process.env[name] ?? "", 10);

  if (!Number.isFinite(value)) {
    return fallback;
  }

  return Math.min(max, Math.max(min, value));
}

export const MAX_PAGE_HEIGHT = readIntegerEnv(
  "MAX_PAGE_HEIGHT",
  20000,
  1000,
  30000
);

export const MAX_MARKDOWN_CHARS = readIntegerEnv(
  "MAX_MARKDOWN_CHARS",
  100000,
  1000,
  500000
);

export const MAX_REQUEST_BYTES = Math.max(
  512 * 1024,
  MAX_MARKDOWN_CHARS * 4 + 16 * 1024
);

export const DEBUG_PNG_MAX_HEIGHT = 6000;

function numberInRange(value, fallback, min, max) {
  const number = Number(value);

  if (!Number.isFinite(number)) {
    return fallback;
  }

  return Math.min(max, Math.max(min, number));
}

function integerInRange(value, fallback, min, max) {
  return Math.round(numberInRange(value, fallback, min, max));
}

function enumValue(value, fallback, allowed, name) {
  if (value === undefined || value === null || value === "") {
    return fallback;
  }

  if (!allowed.includes(value)) {
    const error = new Error(
      `${name} 必须是以下值之一：${allowed.join(", ")}`
    );
    error.statusCode = 400;
    error.code = "INVALID_OPTION";
    throw error;
  }

  return value;
}

export function normalizeOptions(input = {}) {
  if (
    input === null ||
    typeof input !== "object" ||
    Array.isArray(input)
  ) {
    const error = new Error("options 必须是对象");
    error.statusCode = 400;
    error.code = "INVALID_OPTIONS";
    throw error;
  }

  return {
    fontSize: integerInRange(input.fontSize, 22, 14, 30),
    lineHeight: numberInRange(input.lineHeight, 1.45, 1.1, 1.9),
    margin: integerInRange(input.margin, 12, 0, 32),
    bottomFeed: integerInRange(input.bottomFeed, 24, 0, 96),
    threshold: integerInRange(input.threshold, 185, 0, 255),
    bandHeight: integerInRange(input.bandHeight, 256, 32, 1024),

    dither: enumValue(
      input.dither,
      "threshold",
      ["threshold", "bayer4"],
      "dither"
    ),

    compression: enumValue(
      input.compression,
      "packbits",
      ["none", "packbits"],
      "compression"
    )
  };
}

