import MarkdownIt from "markdown-it";
import { mathPlugin } from "./math-plugin.js";

const markdown = new MarkdownIt({
  html: false,
  xhtmlOut: false,
  breaks: false,
  linkify: true,
  typographer: false
});

markdown.use(mathPlugin);

/*
 * 禁止加载远程图片。
 *
 * 这样既可以避免 Chromium 访问任意 URL，也可以避免：
 * - SSRF
 * - 渲染超时
 * - 图片导致点阵体积膨胀
 * - 不可控的灰度和功耗
 */
markdown.renderer.rules.image = (tokens, index) => {
  const token = tokens[index];
  const alt = markdown.utils.escapeHtml(
    token.content || token.attrGet("alt") || ""
  );

  if (alt) {
    return `<span class="image-placeholder">[图片：${alt}]</span>`;
  }

  return `<span class="image-placeholder">[图片]</span>`;
};

export function renderMarkdown(markdownSource) {
  return markdown.render(markdownSource);
}

