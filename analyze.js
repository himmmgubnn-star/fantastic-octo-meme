'use strict';

const { tokenize } = require('./tokenizer');
const { decodeLuaStringToken, collectLocalNames } = require('./passes');
const { ROBLOX_GLOBALS } = require('./keywords');

/**
 * @typedef {object} AnalysisReport
 * @property {number} bytes
 * @property {number} lines
 * @property {number} tokens
 * @property {number} identifiers
 * @property {number} uniqueIdentifiers
 * @property {number} strings
 * @property {number} stringBytes
 * @property {number} numbers
 * @property {number} comments
 * @property {number} commentBytes
 * @property {number} localsDeclared
 * @property {number} functions
 * @property {string[]} robloxApisUsed
 * @property {string[]} topIdentifiers
 * @property {string} complexity
 * @property {string[]} recommendations
 */

/**
 * Static analysis / metrics for a Lua module (pre-obfuscation insights).
 * @param {string} source
 * @returns {AnalysisReport}
 */
function analyzeSource(source) {
  const text = typeof source === 'string' ? source : '';
  const lines = text.split(/\r?\n/);
  const tokens = text.trim() ? tokenize(text) : [];

  let strings = 0;
  let stringBytes = 0;
  let numbers = 0;
  let comments = 0;
  let commentBytes = 0;
  let functions = 0;
  let identifiers = 0;
  /** @type {Map<string, number>} */
  const identFreq = new Map();
  /** @type {Set<string>} */
  const robloxApis = new Set();

  for (const token of tokens) {
    if (token.type === 'eof' || token.type === 'whitespace') continue;
    if (token.type === 'string') {
      strings += 1;
      const raw = decodeLuaStringToken(token.value);
      stringBytes += Buffer.byteLength(raw ?? token.value, 'utf8');
    } else if (token.type === 'number') {
      numbers += 1;
    } else if (token.type === 'comment') {
      comments += 1;
      commentBytes += Buffer.byteLength(token.value, 'utf8');
    } else if (token.type === 'keyword' && token.value === 'function') {
      functions += 1;
    } else if (token.type === 'ident') {
      identifiers += 1;
      identFreq.set(token.value, (identFreq.get(token.value) || 0) + 1);
      if (ROBLOX_GLOBALS.has(token.value)) robloxApis.add(token.value);
    }
  }

  const localsDeclared = collectLocalNames(tokens).size;
  const topIdentifiers = [...identFreq.entries()]
    .filter(([name]) => !ROBLOX_GLOBALS.has(name))
    .sort((a, b) => b[1] - a[1])
    .slice(0, 8)
    .map(([name, count]) => `${name} (×${count})`);

  const bytes = Buffer.byteLength(text, 'utf8');
  const significantTokens = tokens.filter(
    (t) => t.type !== 'eof' && t.type !== 'whitespace' && t.type !== 'comment',
  ).length;

  let complexity = 'simple';
  if (functions > 15 || significantTokens > 800) complexity = 'large';
  else if (functions > 6 || significantTokens > 250) complexity = 'moderate';

  /** @type {string[]} */
  const recommendations = [];
  if (stringBytes > bytes * 0.25) {
    recommendations.push('High string density — use **standard** or **heavy** for string encryption.');
  }
  if (localsDeclared >= 5) {
    recommendations.push('Enough locals to benefit from renaming — any level helps.');
  }
  if (comments > 0) {
    recommendations.push('Comments will be stripped automatically during minify.');
  }
  if (complexity === 'large') {
    recommendations.push('Large module — prefer **standard** over **heavy** if output size matters.');
  }
  if (robloxApis.has('HttpService')) {
    recommendations.push('HttpService detected — run `/scan` before shipping.');
  }
  if (recommendations.length === 0) {
    recommendations.push('**standard** level is a solid default for ModuleScripts.');
  }

  return {
    bytes,
    lines: lines.length,
    tokens: significantTokens,
    identifiers,
    uniqueIdentifiers: identFreq.size,
    strings,
    stringBytes,
    numbers,
    comments,
    commentBytes,
    localsDeclared,
    functions,
    robloxApisUsed: [...robloxApis].sort(),
    topIdentifiers,
    complexity,
    recommendations,
  };
}

module.exports = { analyzeSource };
