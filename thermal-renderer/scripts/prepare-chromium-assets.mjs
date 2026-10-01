import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import { fileURLToPath } from "node:url";

const require = createRequire(import.meta.url);

const scriptDirectory = path.dirname(
  fileURLToPath(import.meta.url)
);

const projectRoot = path.resolve(
  scriptDirectory,
  ".."
);

function findPackageRoot() {
  let currentDirectory = path.dirname(
    require.resolve("@sparticuz/chromium")
  );

  while (true) {
    const packageJsonPath = path.join(
      currentDirectory,
      "package.json"
    );

    if (fs.existsSync(packageJsonPath)) {
      try {
        const packageJson = JSON.parse(
          fs.readFileSync(
            packageJsonPath,
            "utf8"
          )
        );

        if (
          packageJson.name ===
          "@sparticuz/chromium"
        ) {
          return currentDirectory;
        }
      } catch {
        // 继续向上搜索
      }
    }

    const parentDirectory =
      path.dirname(currentDirectory);

    if (parentDirectory === currentDirectory) {
      break;
    }

    currentDirectory = parentDirectory;
  }

  throw new Error(
    "找不到 @sparticuz/chromium 的包根目录"
  );
}

const chromiumPackageRoot = findPackageRoot();

const sourceDirectory = path.join(
  chromiumPackageRoot,
  "bin"
);

const outputDirectory = path.join(
  projectRoot,
  "assets",
  "chromium"
);

const requiredFiles = [
  "chromium.br",
  "al2023.tar.br",
  "fonts.tar.br",
  "swiftshader.tar.br"
];

if (!fs.existsSync(sourceDirectory)) {
  throw new Error(
    `找不到 Chromium 压缩资源目录：${sourceDirectory}`
  );
}

fs.rmSync(outputDirectory, {
  recursive: true,
  force: true
});

fs.mkdirSync(outputDirectory, {
  recursive: true
});

for (const fileName of requiredFiles) {
  const sourcePath = path.join(
    sourceDirectory,
    fileName
  );

  const outputPath = path.join(
    outputDirectory,
    fileName
  );

  if (!fs.existsSync(sourcePath)) {
    throw new Error(
      `Chromium 包缺少运行资源：${sourcePath}`
    );
  }

  fs.copyFileSync(sourcePath, outputPath);
}

const licenseCandidates = [
  path.join(chromiumPackageRoot, "LICENSE"),
  path.join(chromiumPackageRoot, "LICENSE.md")
];

for (const licensePath of licenseCandidates) {
  if (fs.existsSync(licensePath)) {
    fs.copyFileSync(
      licensePath,
      path.join(
        outputDirectory,
        path.basename(licensePath)
      )
    );

    break;
  }
}

const files = fs.readdirSync(
  outputDirectory
);

console.log("Chromium assets prepared:", {
  sourceDirectory,
  outputDirectory,
  files
});
