import {
  getAssetCss
} from "./assets.js";

export function buildHtml(contentHtml, options) {
  const {
    fontSize,
    lineHeight,
    margin,
    bottomFeed
  } = options;

  const {
    katexCss,
    printFontCss
  } = getAssetCss();

  return `<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta
  http-equiv="Content-Security-Policy"
  content="
    default-src 'none';
    style-src 'unsafe-inline';
    font-src https://render.local data:;
    img-src data:;
  "
>
<style>
${katexCss}
${printFontCss}

:root {
  color-scheme: light;
}

@page {
  size: 384px auto;
  margin: 0;
}

* {
  box-sizing: border-box;
}

html,
body {
  width: 384px;
  min-width: 384px;
  max-width: 384px;
  margin: 0;
  padding: 0;
  overflow-x: hidden;
  background: #ffffff;
  color: #000000;
}

body {
  font-family:
    "PrintCJK",
    sans-serif;
  font-size: ${fontSize}px;
  line-height: ${lineHeight};
  font-weight: 300;
  font-synthesis: none;
  text-rendering: optimizeLegibility;
  overflow-wrap: anywhere;
  word-break: normal;
}

#paper {
  display: flow-root;
  width: 384px;
  min-height: 1px;
  padding:
    ${margin}px
    ${margin}px
    ${margin + bottomFeed}px
    ${margin}px;
  background: #ffffff;
}

h1,
h2,
h3,
h4,
h5,
h6 {
  margin:
    0.8em
    0
    0.35em;
  padding: 0;
  line-height: 1.2;
  font-weight: 400;
  overflow-wrap: anywhere;
}

h1:first-child,
h2:first-child,
h3:first-child {
  margin-top: 0;
}

h1 {
  font-size: 1.48em;
  border-bottom: 2px solid #000000;
  padding-bottom: 0.16em;
}

h2 {
  font-size: 1.3em;
  border-bottom: 1px solid #000000;
  padding-bottom: 0.12em;
}

h3 {
  font-size: 1.16em;
}

h4,
h5,
h6 {
  font-size: 1em;
}

p {
  margin: 0.42em 0;
}

strong,
b {
  font-weight: 400;
}

ul,
ol {
  margin: 0.4em 0;
  padding-left: 1.35em;
}

li {
  margin: 0.18em 0;
}

li > p {
  margin: 0.15em 0;
}

blockquote {
  margin: 0.55em 0;
  padding: 0.12em 0 0.12em 0.6em;
  border-left: 4px solid #000000;
}

blockquote > :first-child {
  margin-top: 0;
}

blockquote > :last-child {
  margin-bottom: 0;
}

hr {
  height: 2px;
  margin: 0.75em 0;
  padding: 0;
  border: 0;
  background: #000000;
}

code {
  font-family:
    "PrintCJK",
    monospace;
  font-size: 0.86em;
  border: 1px solid #000000;
  padding: 0 0.15em;
  overflow-wrap: anywhere;
  word-break: break-all;
}

pre {
  width: 100%;
  margin: 0.55em 0;
  padding: 0.4em;
  border: 1px solid #000000;
  white-space: pre-wrap;
  word-break: break-all;
  overflow-wrap: anywhere;
}

pre code {
  display: block;
  padding: 0;
  border: 0;
  font-size: 0.82em;
  line-height: 1.35;
}

table {
  width: 100%;
  margin: 0.55em 0;
  border-collapse: collapse;
  table-layout: fixed;
  font-size: 0.8em;
  line-height: 1.3;
}

th,
td {
  border: 1px solid #000000;
  padding: 0.22em;
  vertical-align: top;
  overflow-wrap: anywhere;
  word-break: break-all;
}

th {
  font-weight: 400;
  border-bottom-width: 2px;
}

a,
.link {
  color: #000000;
  text-decoration: underline;
  text-decoration-thickness: 1px;
  overflow-wrap: anywhere;
}

del,
s {
  text-decoration-thickness: 2px;
}

.image-placeholder {
  display: inline-block;
  border: 1px solid #000000;
  padding: 0.1em 0.25em;
}

.math-inline {
  display: inline-block;
  max-width: 100%;
  vertical-align: middle;
}

.math-block {
  width: 100%;
  margin: 0.55em 0;
  overflow: hidden;
}

.katex-display {
  width: 100%;
  margin: 0;
  overflow: hidden;
}

.math-error {
  border: 1px solid #000000;
}
</style>
</head>
<body>
  <main id="paper">${contentHtml}</main>
</body>
</html>`;
}
