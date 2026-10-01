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

const katexCssSource = require.resolve(
  "katex/dist/katex.min.css"
);

const katexDistSource = path.dirname(
  katexCssSource
);

const katexPackageRoot = path.resolve(
  katexDistSource,
  ".."
);

const outputDirectory = path.join(
  projectRoot,
  "assets",
  "katex"
);

const outputFontsDirectory = path.join(
  outputDirectory,
  "fonts"
);

console.log("Preparing KaTeX assets...");
console.log({
  katexDistSource,
  outputDirectory
});

fs.rmSync(outputDirectory, {
  recursive: true,
  force: true
});

fs.mkdirSync(outputFontsDirectory, {
  recursive: true
});

const rawCss = fs.readFileSync(katexCssSource, "utf8");
const fontPaths = new Set();

const optimizedCss = rawCss.replace(
  /src:([^;}]+)/g,
  (complete, sources) => {
    const match = sources.match(
      /url\(([^)]*\.woff2)\)\s*format\((["'])woff2\2\)/
    );

    if (!match) {
      return complete;
    }

    const fontPath = match[1].replace(
      /^["']|["']$/g,
      ""
    );

    fontPaths.add(fontPath);

    return `src:url(${match[1]}) format("woff2")`;
  }
);

if (fontPaths.size === 0) {
  throw new Error("KaTeX CSS 中没有找到 WOFF2 字体");
}

fs.writeFileSync(
  path.join(outputDirectory, "katex.min.css"),
  optimizedCss
);

for (const fontPath of fontPaths) {
  const sourcePath = path.resolve(
    katexDistSource,
    fontPath
  );

  const outputPath = path.join(
    outputFontsDirectory,
    path.basename(fontPath)
  );

  fs.copyFileSync(sourcePath, outputPath);
}

const licenseSource = path.join(
  katexPackageRoot,
  "LICENSE"
);

if (fs.existsSync(licenseSource)) {
  fs.copyFileSync(
    licenseSource,
    path.join(outputDirectory, "LICENSE")
  );
}

const requiredFont = path.join(
  outputFontsDirectory,
  "KaTeX_AMS-Regular.woff2"
);

if (!fs.existsSync(requiredFont)) {
  throw new Error(
    `KaTeX 字体复制失败，找不到：${requiredFont}`
  );
}

const fontFiles = fs.readdirSync(
  outputFontsDirectory
).filter(fileName => {
  return /\.(woff2?|ttf|otf)$/i.test(fileName);
});

console.log(
  `KaTeX assets prepared: ${fontFiles.length} font files`
);
