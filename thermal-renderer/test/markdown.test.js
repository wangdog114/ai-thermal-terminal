import assert from "node:assert/strict";
import test from "node:test";

import { renderMarkdown } from "../src/markdown.js";

test("renders inline and block math", () => {
  assert.match(renderMarkdown("$E=mc^2$"), /math-inline/);
  assert.match(
    renderMarkdown("$$\nx^2+y^2=z^2\n$$"),
    /katex-display/
  );
});

test("renders LaTeX parenthesis and bracket delimiters", () => {
  const inline = renderMarkdown(
    String.raw`行内公式：\(E=mc^2\)。`
  );
  const block = renderMarkdown(
    String.raw`\[
x^2+y^2=z^2
\]`
  );
  const embeddedBlock = renderMarkdown(
    String.raw`公式：\[x=2\]。`
  );

  assert.match(inline, /math-inline/);
  assert.match(inline, /katex/);
  assert.match(block, /math-block/);
  assert.match(block, /katex-display/);
  assert.match(embeddedBlock, /math-block-inline/);
  assert.match(embeddedBlock, /katex-display/);
});

test("does not render escaped LaTeX delimiters as math", () => {
  const html = renderMarkdown(
    String.raw`文本 \\(not-math\\)`
  );

  assert.doesNotMatch(html, /math-inline/);
});

test("does not interpret currency as math", () => {
  const html = renderMarkdown("价格从 $5 到 $10");

  assert.match(html, /\$5 到 \$10/);
  assert.doesNotMatch(html, /math-inline/);
});

test("never discards text after an invalid block close", () => {
  const html = renderMarkdown(
    "$$\nx\n$$ trailing text\n\nafter"
  );

  assert.match(html, /trailing text/);
  assert.match(html, /after/);
});

test("blocks raw HTML and remote images", () => {
  const html = renderMarkdown(
    '<script>alert(1)</script> ![远程](https://example.com/a.png)'
  );

  assert.doesNotMatch(html, /<script>/);
  assert.doesNotMatch(html, /<img/);
  assert.match(html, /图片：远程/);
});
