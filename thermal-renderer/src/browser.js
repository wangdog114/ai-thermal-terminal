import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

import puppeteer from "puppeteer-core";

let browserPromise = null;
let chromiumPromise = null;

const moduleDirectory = path.dirname(
  fileURLToPath(import.meta.url)
);

const projectRoot = path.resolve(
  moduleDirectory,
  ".."
);

const chromiumPackDirectory = path.join(
  projectRoot,
  "assets",
  "chromium"
);

const isVercelLinux =
  process.env.VERCEL === "1" &&
  process.platform === "linux";

const al2023LibraryDirectories = [
  "/tmp/al2023/lib",
  "/tmp/al2023/lib64"
];

const chromiumCacheFiles = [
  {
    outputPath: "/tmp/al2023/lib/libnss3.so",
    cleanupPath: "/tmp/al2023"
  },
  {
    outputPath: "/tmp/fonts/fonts.conf",
    cleanupPath: "/tmp/fonts"
  }
];

function createBrowserError(
  message,
  code,
  details
) {
  const error = new Error(message);

  error.code = code;
  error.statusCode = 500;
  error.details = details;

  return error;
}

function checkChromiumAssets() {
  const requiredFiles = [
    "chromium.br",
    "al2023.tar.br",
    "fonts.tar.br"
  ];

  const status = requiredFiles.map(
    fileName => {
      const filePath = path.join(
        chromiumPackDirectory,
        fileName
      );

      return {
        fileName,
        filePath,
        exists: fs.existsSync(filePath)
      };
    }
  );

  const missingFiles = status.filter(
    item => !item.exists
  );

  if (missingFiles.length > 0) {
    throw createBrowserError(
      "Chromium 运行资源不完整",
      "CHROMIUM_ASSETS_MISSING",
      {
        chromiumPackDirectory,
        files: status,
        directoryExists:
          fs.existsSync(
            chromiumPackDirectory
          ),
        directoryContents:
          fs.existsSync(
            chromiumPackDirectory
          )
            ? fs.readdirSync(
                chromiumPackDirectory
              )
            : []
      }
    );
  }
}

async function getChromium() {
  if (!chromiumPromise) {
    chromiumPromise = (async () => {
      const chromiumModule = await import(
        "@sparticuz/chromium"
      );

      const chromium =
        chromiumModule.default;

      /*
       * 当前只渲染二维文本，不需要 WebGL。
       * 关闭图形模式后不需要 SwiftShader。
       */
      chromium.setGraphicsMode = false;

      return chromium;
    })().catch(error => {
      chromiumPromise = null;
      throw error;
    });
  }

  return chromiumPromise;
}

function findNssLibrary() {
  for (
    const directory of
    al2023LibraryDirectories
  ) {
    const filePath = path.join(
      directory,
      "libnss3.so"
    );

    if (fs.existsSync(filePath)) {
      return filePath;
    }
  }

  return null;
}

function prependLibraryPath(
  currentValue,
  directories
) {
  const currentDirectories = String(
    currentValue || ""
  )
    .split(":")
    .filter(Boolean);

  return [
    ...new Set([
      ...directories,
      ...currentDirectories
    ])
  ].join(":");
}

function getDirectoryContents(directory) {
  try {
    return fs.readdirSync(directory);
  } catch (error) {
    return [`读取失败：${error.message}`];
  }
}

function repairIncompleteChromiumCache() {
  if (
    !isVercelLinux ||
    process.env.CHROME_PATH
  ) {
    return;
  }

  const missingCacheFiles =
    chromiumCacheFiles.filter(
      item => !fs.existsSync(item.outputPath)
    );

  if (missingCacheFiles.length === 0) {
    return;
  }

  /*
   * executablePath() 在 /tmp/chromium 存在时会直接返回，
   * 即使配套的 NSS 或字体资源并未成功解压。清掉不完整
   * 缓存的标志文件，让它在本次调用中重新完成全部解压。
   */
  fs.rmSync("/tmp/chromium", {
    force: true
  });

  for (const item of missingCacheFiles) {
    fs.rmSync(item.cleanupPath, {
      recursive: true,
      force: true
    });
  }
}

async function resolveExecutablePath(
  chromium
) {
  /*
   * 本地开发时如果配置了系统 Chrome，
   * 不需要解压 @sparticuz/chromium。
   */
  if (process.env.CHROME_PATH) {
    if (
      !fs.existsSync(
        process.env.CHROME_PATH
      )
    ) {
      throw createBrowserError(
        `CHROME_PATH 指向的文件不存在：` +
          process.env.CHROME_PATH,
        "CHROME_PATH_INVALID",
        {
          chromePath:
            process.env.CHROME_PATH
        }
      );
    }

    return process.env.CHROME_PATH;
  }

  checkChromiumAssets();
  repairIncompleteChromiumCache();

  /*
   * 关键：
   *
   * 显式将 assets/chromium 传给 executablePath()，
   * 不再让它从 node_modules/@sparticuz/chromium/bin
   * 查找资源。
   */
  const executablePath =
    await chromium.executablePath(
      chromiumPackDirectory
    );

  if (!fs.existsSync(executablePath)) {
    throw createBrowserError(
      `Chromium 解压后不存在：${executablePath}`,
      "CHROMIUM_EXECUTABLE_MISSING",
      {
        executablePath,
        tmpContents:
          getDirectoryContents("/tmp")
      }
    );
  }

  if (isVercelLinux) {
    const nssLibrary = findNssLibrary();

    if (!nssLibrary) {
      throw createBrowserError(
        "Chromium 已解压，但找不到 libnss3.so",
        "CHROMIUM_RUNTIME_LIBRARIES_MISSING",
        {
          executablePath,
          chromiumPackDirectory,

          tmpContents:
            getDirectoryContents("/tmp"),

          al2023Contents:
            getDirectoryContents(
              "/tmp/al2023"
            ),

          libraryDirectories:
            al2023LibraryDirectories.map(
              directory => ({
                directory,
                exists:
                  fs.existsSync(directory),
                contents:
                  getDirectoryContents(
                    directory
                  ),
                libnss3Exists:
                  fs.existsSync(
                    path.join(
                      directory,
                      "libnss3.so"
                    )
                  )
              })
            )
        }
      );
    }
  }

  return executablePath;
}

async function launchBrowser() {
  const chromium = await getChromium();

  const executablePath =
    await resolveExecutablePath(chromium);

  const browserEnvironment = {
    ...process.env
  };

  if (
    isVercelLinux &&
    !process.env.CHROME_PATH
  ) {
    browserEnvironment.LD_LIBRARY_PATH =
      prependLibraryPath(
        process.env.LD_LIBRARY_PATH,
        al2023LibraryDirectories
      );
  }

  const browser = await puppeteer.launch({
    executablePath,

    args: [
      ...chromium.args,
      "--disable-dev-shm-usage",
      "--disable-gpu",
      "--no-first-run",
      "--no-zygote"
    ],

    defaultViewport: {
      width: 384,
      height: 1024,
      deviceScaleFactor: 1
    },

    headless: "shell",

    env: browserEnvironment
  });

  browser.on("disconnected", () => {
    browserPromise = null;
  });

  return browser;
}

export async function getBrowser() {
  if (!browserPromise) {
    browserPromise = launchBrowser().catch(
      error => {
        browserPromise = null;
        throw error;
      }
    );
  }

  const browser = await browserPromise;

  if (!browser.connected) {
    browserPromise = null;
    return getBrowser();
  }

  return browser;
}

export function getChromiumStatus() {
  const requiredFiles = [
    "chromium.br",
    "al2023.tar.br",
    "fonts.tar.br",
    "swiftshader.tar.br"
  ];

  return {
    platform: process.platform,
    architecture: process.arch,
    isVercelLinux,
    chromiumPackDirectory,
    packDirectoryExists:
      fs.existsSync(chromiumPackDirectory),
    files: requiredFiles.map(
      fileName => {
        const filePath = path.join(
          chromiumPackDirectory,
          fileName
        );

        let size = null;

        try {
          size = fs.statSync(filePath).size;
        } catch {
          // 文件不存在
        }

        return {
          fileName,
          filePath,
          exists: fs.existsSync(filePath),
          size
        };
      }
    )
  };
}
