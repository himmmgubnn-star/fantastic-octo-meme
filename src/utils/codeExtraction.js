'use strict';

const CODE_FENCE_RE = /```(?:lua|luau)?\s*\n?([\s\S]*?)```/i;

/**
 * Pull Lua source from a Discord message body.
 * Prefers fenced code blocks; falls back to the whole message (minus a leading command).
 *
 * @param {string} content
 * @returns {string}
 */
function extractCodeFromMessage(content) {
  if (typeof content !== 'string') return '';

  const fenced = content.match(CODE_FENCE_RE);
  if (fenced && fenced[1] !== undefined) {
    return fenced[1].replace(/^\uFEFF/, '').trimEnd();
  }

  // Strip a leading prefix command like "!obfuscate" or "/obfuscate "
  const stripped = content
    .replace(/^\s*(?:[!/]obfuscate(?:lua)?|obfuscate)\b\s*/i, '')
    .replace(/^\uFEFF/, '')
    .trim();

  return stripped;
}

/**
 * Normalize uploaded .lua / .luau / .txt attachment text.
 * @param {string} raw
 * @returns {string}
 */
function normalizeAttachmentCode(raw) {
  if (typeof raw !== 'string') return '';
  return raw.replace(/^\uFEFF/, '').replace(/\r\n/g, '\n');
}

module.exports = {
  extractCodeFromMessage,
  normalizeAttachmentCode,
};
