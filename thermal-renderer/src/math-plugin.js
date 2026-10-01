import katex from "katex";

function isEscaped(source, position) {
  let backslashes = 0;

  for (let i = position - 1; i >= 0 && source[i] === "\\"; i--) {
    backslashes++;
  }

  return backslashes % 2 === 1;
}

function renderKatex(tex, displayMode, escapeHtml) {
  try {
    return katex.renderToString(tex, {
      displayMode,
      throwOnError: false,
      strict: "ignore",
      trust: false,
      output: "html",
      maxExpand: 1000,
      maxSize: 100
    });
  } catch {
    return `<code class="math-error">${escapeHtml(tex)}</code>`;
  }
}

function mathInline(state, silent) {
  const start = state.pos;
  const source = state.src;
  let opening;
  let closing;
  let displayMode = false;

  if (source[start] === "$") {
    // $$ 由块级规则处理
    if (source[start + 1] === "$") {
      return false;
    }
    if (isEscaped(source, start)) {
      return false;
    }
    opening = "$";
    closing = "$";
  } else if (
    source[start] === "\\" &&
    (source[start + 1] === "(" ||
      source[start + 1] === "[") &&
    !isEscaped(source, start)
  ) {
    displayMode = source[start + 1] === "[";
    opening = displayMode ? "\\[" : "\\(";
    closing = displayMode ? "\\]" : "\\)";
  } else {
    return false;
  }

  const contentStart = start + opening.length;

  if (/\s/.test(source[contentStart] || "")) {
    return false;
  }

  let end = contentStart;

  while (end < state.posMax) {
    end = source.indexOf(closing, end);

    if (end < 0) {
      return false;
    }

    if (
      !isEscaped(source, end) &&
      !/\s/.test(source[end - 1] || "")
    ) {
      break;
    }

    end++;
  }

  if (end <= contentStart) {
    return false;
  }

  const content = source.slice(contentStart, end);

  if (content.includes("\n")) {
    return false;
  }

  if (!silent) {
    const token = state.push(
      displayMode
        ? "math_display_inline"
        : "math_inline",
      "math",
      0
    );
    token.content = content;
    token.markup = opening;
  }

  state.pos = end + closing.length;
  return true;
}

function mathBlock(state, startLine, endLine, silent) {
  const start =
    state.bMarks[startLine] + state.tShift[startLine];
  const max = state.eMarks[startLine];
  const openingLine = state.src.slice(start, max);

  let opening;
  let closing;

  if (openingLine.startsWith("$$")) {
    opening = "$$";
    closing = "$$";
  } else if (openingLine.startsWith("\\[")) {
    opening = "\\[";
    closing = "\\]";
  } else {
    return false;
  }

  if (silent) {
    return true;
  }

  const firstPart = openingLine.slice(opening.length);
  const sameLineClose = firstPart.lastIndexOf(closing);

  let content = "";
  let nextLine = startLine + 1;
  let found = false;

  if (
    sameLineClose >= 0 &&
    firstPart.slice(
      sameLineClose + closing.length
    ).trim() === ""
  ) {
    content = firstPart.slice(0, sameLineClose);
    found = true;
  } else {
    const lines = [firstPart];

    while (nextLine < endLine) {
      const lineStart =
        state.bMarks[nextLine] + state.tShift[nextLine];
      const lineEnd = state.eMarks[nextLine];
      const currentLine = state.src.slice(lineStart, lineEnd);

      let closePosition = currentLine.indexOf(closing);

      while (
        closePosition >= 0 &&
        currentLine.slice(
          closePosition + closing.length
        ).trim() !== ""
      ) {
        closePosition = currentLine.indexOf(
          closing,
          closePosition + closing.length
        );
      }

      if (closePosition >= 0) {
        lines.push(currentLine.slice(0, closePosition));
        nextLine++;
        found = true;
        break;
      }

      lines.push(currentLine);
      nextLine++;
    }

    content = lines.join("\n");
  }

  if (!found) {
    return false;
  }

  const token = state.push("math_block", "math", 0);
  token.block = true;
  token.content = content.trim();
  token.map = [startLine, nextLine];
  token.markup = opening;

  state.line = nextLine;
  return true;
}

export function mathPlugin(md) {
  md.inline.ruler.before(
    "escape",
    "math_inline",
    mathInline
  );

  md.block.ruler.before(
    "fence",
    "math_block",
    mathBlock,
    {
      alt: [
        "paragraph",
        "reference",
        "blockquote",
        "list"
      ]
    }
  );

  md.renderer.rules.math_inline = (tokens, index) => {
    const html = renderKatex(
      tokens[index].content,
      false,
      md.utils.escapeHtml
    );

    return `<span class="math-inline">${html}</span>`;
  };

  md.renderer.rules.math_display_inline = (
    tokens,
    index
  ) => {
    const html = renderKatex(
      tokens[index].content,
      true,
      md.utils.escapeHtml
    );

    return (
      `<span class="math-block math-block-inline">` +
      `${html}</span>`
    );
  };

  md.renderer.rules.math_block = (tokens, index) => {
    const html = renderKatex(
      tokens[index].content,
      true,
      md.utils.escapeHtml
    );

    return `<div class="math-block">${html}</div>\n`;
  };
}
