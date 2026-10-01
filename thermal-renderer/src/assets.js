import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const moduleDirectory = path.dirname(
  fileURLToPath(import.meta.url)
);

const PROJECT_ROOT = path.resolve(
  moduleDirectory,
  ".."
);

const LOCAL_ASSET_ORIGIN = "https://render.local";

const KATEX_DIRECTORY = path.join(
  PROJECT_ROOT,
  "assets",
  "katex"
);

const KATEX_CSS_PATH = path.join(
  KATEX_DIRECTORY,
  "katex.min.css"
);

const DEFAULT_PRINT_FONT_PATH = fileURLToPath(
  new URL(
    "../assets/fonts/SourceHanSansSC-Regular.otf",
    import.meta.url
  )
);

const configuredPrintFontPath =
  process.env.PRINT_FONT_PATH?.trim();

const PRINT_FONT_PATH =
  configuredPrintFontPath
    ? path.isAbsolute(configuredPrintFontPath)
      ? configuredPrintFontPath
      : path.resolve(
          PROJECT_ROOT,
          configuredPrintFontPath
        )
    : DEFAULT_PRINT_FONT_PATH;

const assets = new Map();

let initialized = false;
let initializationError = null;
let katexCss = "";
let printFontCss = "";

function createAssetError(
  message,
  code = "RENDER_ASSET_MISSING"
) {
  const error = new Error(message);
  error.statusCode = 500;
  error.code = code;
  return error;
}

function contentTypeForFile(filePath) {
  const extension = path.extname(
    filePath
  ).toLowerCase();

  switch (extension) {
    case ".woff2":
      return "font/woff2";

    case ".woff":
      return "font/woff";

    case ".otf":
      return "font/otf";

    case ".ttf":
      return "font/ttf";

    default:
      return "application/octet-stream";
  }
}

function fontFormatForFile(filePath) {
  const extension = path.extname(
    filePath
  ).toLowerCase();

  switch (extension) {
    case ".woff2":
      return "woff2";

    case ".woff":
      return "woff";

    case ".otf":
      return "opentype";

    case ".ttf":
      return "truetype";

    default:
      return "opentype";
  }
}

function addAsset(url, filePath) {
  if (!fs.existsSync(filePath)) {
    throw createAssetError(
      `找不到渲染资源：${filePath}`
    );
  }

  assets.set(url, {
    body: fs.readFileSync(filePath),
    contentType: contentTypeForFile(filePath)
  });
}

function ensureInsideDirectory(
  filePath,
  allowedDirectory
) {
  const relativePath = path.relative(
    allowedDirectory,
    filePath
  );

  if (
    relativePath.startsWith("..") ||
    path.isAbsolute(relativePath)
  ) {
    throw createAssetError(
      `KaTeX CSS 引用了非法路径：${filePath}`,
      "INVALID_KATEX_ASSET_PATH"
    );
  }
}

function buildKatexCss() {
  if (!fs.existsSync(KATEX_CSS_PATH)) {
    throw createAssetError(
      `找不到 KaTeX CSS：${KATEX_CSS_PATH}。` +
      `请运行 npm run prepare:assets。`,
      "KATEX_CSS_MISSING"
    );
  }

  const rawCss = fs.readFileSync(
    KATEX_CSS_PATH,
    "utf8"
  );

  const rewrittenCss = rawCss.replace(
    /url\(([^)]+)\)/g,
    (complete, rawUrl) => {
      const cleanedUrl = rawUrl
        .trim()
        .replace(/^["']|["']$/g, "");

      if (
        cleanedUrl.startsWith("data:") ||
        cleanedUrl.startsWith("#")
      ) {
        return complete;
      }

      if (
        cleanedUrl.startsWith("http:") ||
        cleanedUrl.startsWith("https:")
      ) {
        throw createAssetError(
          `KaTeX CSS 不允许引用远程资源：${cleanedUrl}`,
          "REMOTE_KATEX_ASSET"
        );
      }

      const relativeAssetPath =
        cleanedUrl.split(/[?#]/)[0];

      const absoluteAssetPath = path.resolve(
        KATEX_DIRECTORY,
        relativeAssetPath
      );

      ensureInsideDirectory(
        absoluteAssetPath,
        KATEX_DIRECTORY
      );

      if (!fs.existsSync(absoluteAssetPath)) {
        throw createAssetError(
          `找不到 KaTeX 字体资源：${absoluteAssetPath}`,
          "KATEX_FONT_MISSING"
        );
      }

      const assetUrl =
        `${LOCAL_ASSET_ORIGIN}/katex/` +
        encodeURIComponent(
          path.basename(absoluteAssetPath)
        );

      addAsset(
        assetUrl,
        absoluteAssetPath
      );

      return `url("${assetUrl}")`;
    }
  );

  return rewrittenCss.replace(
    /\/\*# sourceMappingURL=.*?\*\//g,
    ""
  );
}

function buildPrintFontCss() {
  if (!fs.existsSync(PRINT_FONT_PATH)) {
    throw createAssetError(
      `找不到中文打印字体：${PRINT_FONT_PATH}。` +
      `请将字体放入 assets/fonts/，` +
      `或者设置 PRINT_FONT_PATH。`,
      "PRINT_FONT_MISSING"
    );
  }

  const printFontUrl =
    `${LOCAL_ASSET_ORIGIN}/fonts/print-font` +
    path.extname(PRINT_FONT_PATH).toLowerCase();

  addAsset(
    printFontUrl,
    PRINT_FONT_PATH
  );

  return `
@font-face {
  font-family: "PrintCJK";
  src:
    url("${printFontUrl}")
    format("${fontFormatForFile(PRINT_FONT_PATH)}");
  font-style: normal;
  font-weight: 400;
  font-display: block;
}
`;
}

function initializeAssets() {
  if (initialized) {
    return;
  }

  if (initializationError) {
    throw initializationError;
  }

  try {
    assets.clear();

    katexCss = buildKatexCss();
    printFontCss = buildPrintFontCss();

    initialized = true;
  } catch (error) {
    initializationError = error;
    throw error;
  }
}

export function assertAssetsReady() {
  initializeAssets();
}

export function getAssetCss() {
  initializeAssets();

  return {
    katexCss,
    printFontCss
  };
}

export function getAssetStatus() {
  try {
    initializeAssets();

    return {
      ready: true,
      printFontPath: PRINT_FONT_PATH,
      printFontExists: true,
      katexCssPath: KATEX_CSS_PATH,
      katexCssExists: true,
      assetCount: assets.size,
      error: null
    };
  } catch (error) {
    return {
      ready: false,
      printFontPath: PRINT_FONT_PATH,
      printFontExists:
        fs.existsSync(PRINT_FONT_PATH),
      katexCssPath: KATEX_CSS_PATH,
      katexCssExists:
        fs.existsSync(KATEX_CSS_PATH),
      assetCount: assets.size,
      error: {
        code:
          error.code ||
          "ASSET_INITIALIZATION_FAILED",
        message: error.message
      }
    };
  }
}

export async function installAssetInterceptor(
  page
) {
  initializeAssets();

  await page.setRequestInterception(true);

  page.on("request", request => {
    const url = request.url();
    const asset = assets.get(url);

    if (asset) {
      void request.respond({
        status: 200,
        contentType: asset.contentType,
        body: asset.body,
        headers: {
          "Access-Control-Allow-Origin": "*",
          "Cache-Control":
            "public, max-age=31536000, immutable"
        }
      }).catch(() => {});

      return;
    }

    if (
      url.startsWith("data:") ||
      url.startsWith("about:")
    ) {
      void request.continue().catch(() => {});
      return;
    }

    void request.abort(
      "blockedbyclient"
    ).catch(() => {});
  });
}

