import { PAGE_WIDTH, MAX_PAGE_HEIGHT } from "./config.js";
import {
  assertAssetsReady,
  installAssetInterceptor
} from "./assets.js";
import { getBrowser } from "./browser.js";
import { renderMarkdown } from "./markdown.js";
import { buildHtml } from "./template.js";

async function postProcessPage(page) {
  return page.evaluate(async () => {
    const minimumFormulaScale = 0.55;
    let formulasTooWide = 0;

    await document.fonts.load(
      '22px "PrintCJK"',
      "中文字体测试"
    );
    await document.fonts.ready;

    function scaleFormula(box, inner, availableWidth) {
      const rectangle = inner.getBoundingClientRect();

      if (
        availableWidth <= 0 ||
        rectangle.width <= availableWidth
      ) {
        return false;
      }

      const scale = availableWidth / rectangle.width;

      if (scale < minimumFormulaScale) {
        formulasTooWide++;
        return "too-wide";
      }

      box.style.height =
        `${Math.ceil(rectangle.height * scale)}px`;
      box.style.overflow = "hidden";

      inner.style.display = "block";
      inner.style.transformOrigin = "left top";
      inner.style.transform = `scale(${scale})`;

      return "scaled";
    }

    for (
      const box of document.querySelectorAll(".katex-display")
    ) {
      const inner = box.querySelector(".katex");

      if (!inner) {
        continue;
      }

      const availableWidth = box.clientWidth;
      if (
        scaleFormula(box, inner, availableWidth) ===
        "scaled"
      ) {
        box.style.textAlign = "left";
      }
    }

    /*
     * 行内公式作为不可拆分单元正常换行；若公式本身仍比
     * 容器更宽，则独占一行并缩放，避免静默裁掉中间内容。
     */
    for (
      const box of document.querySelectorAll(".math-inline")
    ) {
      const inner = box.querySelector(".katex");
      const parent = box.parentElement;

      if (!inner || !parent) {
        continue;
      }

      const scaleResult = scaleFormula(
          box,
          inner,
          parent.clientWidth
        );

      if (scaleResult === "scaled") {
        box.style.display = "block";
        box.style.width = "100%";
      }
    }

    await new Promise(resolve => {
      requestAnimationFrame(() => {
        requestAnimationFrame(resolve);
      });
    });

    return {
      fontReady: document.fonts.check(
        '22px "PrintCJK"',
        "中文字体测试"
      ),
      formulasTooWide,
      minimumFormulaScale
    };
  });
}

export async function renderMarkdownToPng(
  markdownSource,
  options
) {
  assertAssetsReady();

  const contentHtml = renderMarkdown(markdownSource);
  const documentHtml = buildHtml(contentHtml, options);

  const browser = await getBrowser();
  const page = await browser.newPage();

  try {
    page.setDefaultTimeout(20000);

    await page.setViewport({
      width: PAGE_WIDTH,
      height: 1024,
      deviceScaleFactor: 1,
      isMobile: false
    });

    await installAssetInterceptor(page);

    await page.setContent(documentHtml, {
      waitUntil: "domcontentloaded",
      timeout: 20000
    });

    const postProcess = await postProcessPage(page);

    if (!postProcess.fontReady) {
      const error = new Error("中文打印字体加载失败");
      error.statusCode = 500;
      error.code = "PRINT_FONT_LOAD_FAILED";
      throw error;
    }

    if (postProcess.formulasTooWide > 0) {
      const error = new Error(
        "公式过宽，缩放后将无法清晰打印，请将公式拆分为多行"
      );
      error.statusCode = 422;
      error.code = "FORMULA_TOO_WIDE";
      error.details = {
        formulaCount: postProcess.formulasTooWide,
        minimumScale: postProcess.minimumFormulaScale
      };
      throw error;
    }

    const dimensions = await page.evaluate(() => {
      const paper = document.getElementById("paper");
      const rectangle = paper.getBoundingClientRect();

      return {
        width: Math.ceil(rectangle.width),
        height: Math.max(
          1,
          Math.ceil(
            Math.max(
              rectangle.height,
              paper.scrollHeight
            )
          )
        ),
        scrollWidth: Math.ceil(
          Math.max(
            paper.scrollWidth,
            document.body.scrollWidth,
            document.documentElement.scrollWidth
          )
        )
      };
    });

    if (dimensions.height > MAX_PAGE_HEIGHT) {
      const error = new Error(
        `渲染结果高度 ${dimensions.height}px ` +
        `超过限制 ${MAX_PAGE_HEIGHT}px`
      );

      error.statusCode = 413;
      error.code = "PAGE_TOO_TALL";
      error.details = {
        actualHeight: dimensions.height,
        maxHeight: MAX_PAGE_HEIGHT
      };

      throw error;
    }

    if (dimensions.scrollWidth > PAGE_WIDTH) {
      const error = new Error(
        `渲染内容宽度 ${dimensions.scrollWidth}px ` +
        `超过纸张宽度 ${PAGE_WIDTH}px`
      );

      error.statusCode = 422;
      error.code = "CONTENT_OVERFLOW";
      error.details = {
        actualWidth: dimensions.scrollWidth,
        maxWidth: PAGE_WIDTH
      };

      throw error;
    }

    const png = await page.screenshot({
      type: "png",
      captureBeyondViewport: true,
      omitBackground: false,
      clip: {
        x: 0,
        y: 0,
        width: PAGE_WIDTH,
        height: dimensions.height
      }
    });

    return {
      png,
      width: PAGE_WIDTH,
      height: dimensions.height,
      overflowDetected: false
    };
  } finally {
    await page.close().catch(() => {});
  }
}
