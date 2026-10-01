import sharp from "sharp";

const BAYER_4X4 = [
   0,  8,  2, 10,
  12,  4, 14,  6,
   3, 11,  1,  9,
  15,  7, 13,  5
];

function thresholdPixel(value, threshold) {
  return value <= threshold;
}

function bayerPixel(value, x, y) {
  const matrixValue =
    BAYER_4X4[(y & 3) * 4 + (x & 3)];

  const threshold = (matrixValue + 0.5) * 16;

  return value < threshold;
}

function hasOppositeInk(data, width, height, x, y, threshold) {
  for (const [dx, dy] of [
    [1, 0], [0, 1], [1, 1], [1, -1]
  ]) {
    const leftX = x - dx;
    const leftY = y - dy;
    const rightX = x + dx;
    const rightY = y + dy;

    if (
      leftX >= 0 && leftX < width &&
      rightX >= 0 && rightX < width &&
      leftY >= 0 && leftY < height &&
      rightY >= 0 && rightY < height &&
      data[leftY * width + leftX] <= threshold &&
      data[rightY * width + rightX] <= threshold
    ) {
      return true;
    }
  }

  return false;
}

/*
 * 输出格式：
 *
 * - 从左到右、从上到下
 * - 每字节包含横向 8 个像素
 * - bit 7 对应最左侧像素
 * - 1 = 黑色
 * - 0 = 白色
 */
export async function pngToMonoBitmap(
  pngBuffer,
  expectedWidth,
  expectedHeight,
  options
) {
  const result = await sharp(pngBuffer)
    .flatten({ background: "#ffffff" })
    .greyscale()
    .raw()
    .toBuffer({ resolveWithObject: true });

  const { data, info } = result;

  if (
    info.width !== expectedWidth ||
    info.height !== expectedHeight
  ) {
    throw new Error(
      `PNG 尺寸不匹配：` +
      `${info.width}x${info.height}，` +
      `期望 ${expectedWidth}x${expectedHeight}`
    );
  }

  if (info.channels !== 1) {
    throw new Error(
      `灰度图通道数异常：${info.channels}`
    );
  }

  const rowBytes = Math.ceil(expectedWidth / 8);
  const bitmap = Buffer.alloc(
    rowBytes * expectedHeight,
    0
  );
  const threshold = options.threshold;
  // At small font sizes antialiased one-pixel joins can be lighter than the
  // threshold. Restore only those bounded joins instead of raising the
  // threshold for the whole page, which would make every glyph heavier.
  const rescueLimit = Math.min(240, threshold + 48);

  for (let y = 0; y < expectedHeight; y++) {
    const sourceRow = y * expectedWidth;
    const targetRow = y * rowBytes;

    for (let x = 0; x < expectedWidth; x++) {
      const grayscale = data[sourceRow + x];

      const black =
        options.dither === "bayer4"
          ? bayerPixel(grayscale, x, y)
          : thresholdPixel(grayscale, threshold) ||
            (grayscale <= rescueLimit &&
              hasOppositeInk(
                data, expectedWidth, expectedHeight,
                x, y, threshold
              ));

      if (black) {
        bitmap[targetRow + (x >> 3)] |=
          0x80 >> (x & 7);
      }
    }
  }

  return {
    bitmap,
    rowBytes
  };
}
