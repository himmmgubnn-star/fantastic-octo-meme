'use strict';

/**
 * Lightweight Lua/Luau tokenizer.
 * Preserves strings and comments exactly so later passes never corrupt literals.
 */

/** @typedef {'whitespace'|'comment'|'string'|'number'|'ident'|'keyword'|'symbol'|'eof'} TokenType */

/**
 * @typedef {object} Token
 * @property {TokenType} type
 * @property {string} value
 * @property {number} line
 * @property {number} column
 */

const { LUA_KEYWORDS } = require('./keywords');

const SYMBOL_STARTS = new Set([
  '+',
  '-',
  '*',
  '/',
  '%',
  '^',
  '#',
  '=',
  '~',
  '<',
  '>',
  '(',
  ')',
  '{',
  '}',
  '[',
  ']',
  ';',
  ':',
  ',',
  '.',
  '&',
  '|',
  '?',
  '!',
]);

/**
 * @param {string} source
 * @returns {Token[]}
 */
function tokenize(source) {
  if (typeof source !== 'string') {
    throw new TypeError('source must be a string');
  }

  /** @type {Token[]} */
  const tokens = [];
  let i = 0;
  let line = 1;
  let column = 1;
  const len = source.length;

  const peek = (offset = 0) => source[i + offset] || '';
  const advance = () => {
    const ch = source[i];
    i += 1;
    if (ch === '\n') {
      line += 1;
      column = 1;
    } else {
      column += 1;
    }
    return ch;
  };

  /**
   * @param {TokenType} type
   * @param {string} value
   * @param {number} startLine
   * @param {number} startColumn
   */
  const push = (type, value, startLine, startColumn) => {
    tokens.push({ type, value, line: startLine, column: startColumn });
  };

  while (i < len) {
    const startLine = line;
    const startColumn = column;
    const ch = peek();

    // Whitespace
    if (ch === ' ' || ch === '\t' || ch === '\r' || ch === '\n' || ch === '\f' || ch === '\v') {
      let value = '';
      while (i < len) {
        const c = peek();
        if (c === ' ' || c === '\t' || c === '\r' || c === '\n' || c === '\f' || c === '\v') {
          value += advance();
        } else {
          break;
        }
      }
      push('whitespace', value, startLine, startColumn);
      continue;
    }

    // Comments: -- or --[[ ]]
    if (ch === '-' && peek(1) === '-') {
      let value = advance() + advance();
      if (peek() === '[') {
        // long comment?
        let eq = 0;
        let j = 1;
        while (peek(j) === '=') {
          eq += 1;
          j += 1;
        }
        if (peek(j) === '[') {
          value += advance(); // [
          for (let k = 0; k < eq; k += 1) value += advance();
          value += advance(); // [
          const close = `]${'='.repeat(eq)}]`;
          while (i < len) {
            if (source.startsWith(close, i)) {
              for (let k = 0; k < close.length; k += 1) value += advance();
              break;
            }
            value += advance();
          }
          push('comment', value, startLine, startColumn);
          continue;
        }
      }
      while (i < len && peek() !== '\n') {
        value += advance();
      }
      push('comment', value, startLine, startColumn);
      continue;
    }

    // Long strings [[ ]] or [=[ ]=]
    if (ch === '[') {
      let eq = 0;
      let j = 1;
      while (peek(j) === '=') {
        eq += 1;
        j += 1;
      }
      if (peek(j) === '[') {
        let value = advance();
        for (let k = 0; k < eq; k += 1) value += advance();
        value += advance();
        const close = `]${'='.repeat(eq)}]`;
        while (i < len) {
          if (source.startsWith(close, i)) {
            for (let k = 0; k < close.length; k += 1) value += advance();
            break;
          }
          value += advance();
        }
        push('string', value, startLine, startColumn);
        continue;
      }
    }

    // Quoted strings
    if (ch === "'" || ch === '"') {
      const quote = advance();
      let value = quote;
      while (i < len) {
        const c = peek();
        if (c === '\\') {
          value += advance();
          if (i < len) value += advance();
          continue;
        }
        value += advance();
        if (c === quote) break;
        if (c === '\n') break; // broken string — keep going without looping forever
      }
      push('string', value, startLine, startColumn);
      continue;
    }

    // Numbers (decimal, hex, scientific)
    if (
      isDigit(ch) ||
      (ch === '.' && isDigit(peek(1))) ||
      (ch === '0' && (peek(1) === 'x' || peek(1) === 'X'))
    ) {
      let value = '';
      if (ch === '0' && (peek(1) === 'x' || peek(1) === 'X')) {
        value += advance() + advance();
        while (i < len && /[0-9a-fA-F_]/.test(peek())) value += advance();
      } else {
        while (i < len && /[0-9_]/.test(peek())) value += advance();
        if (peek() === '.' && peek(1) !== '.') {
          value += advance();
          while (i < len && /[0-9_]/.test(peek())) value += advance();
        }
        if (peek() === 'e' || peek() === 'E') {
          value += advance();
          if (peek() === '+' || peek() === '-') value += advance();
          while (i < len && /[0-9_]/.test(peek())) value += advance();
        }
      }
      push('number', value, startLine, startColumn);
      continue;
    }

    // Identifiers / keywords
    if (isIdentStart(ch)) {
      let value = '';
      while (i < len && isIdentContinue(peek())) value += advance();
      const type = LUA_KEYWORDS.has(value) ? 'keyword' : 'ident';
      push(type, value, startLine, startColumn);
      continue;
    }

    // Multi-char symbols
    if (SYMBOL_STARTS.has(ch)) {
      // compound operators used in Lua/Luau
      const two = ch + peek(1);
      const three = two + peek(2);
      const compounds3 = ['//=', '>>=', '<<=', '...'];
      const compounds2 = [
        '==',
        '~=',
        '<=',
        '>=',
        '..',
        '//',
        '<<',
        '>>',
        '+=',
        '-=',
        '*=',
        '/=',
        '%=',
        '^=',
        '..=',
        '->',
        '::',
        '||',
        '&&',
        '~=',
      ];

      let value;
      if (compounds3.includes(three)) {
        value = advance() + advance() + advance();
      } else if (compounds2.includes(two)) {
        value = advance() + advance();
      } else {
        value = advance();
      }
      push('symbol', value, startLine, startColumn);
      continue;
    }

    // Unknown character — keep as symbol so we never drop source bytes
    push('symbol', advance(), startLine, startColumn);
  }

  push('eof', '', line, column);
  return tokens;
}

/** @param {Token[]} tokens */
function tokensToSource(tokens) {
  let out = '';
  for (const token of tokens) {
    if (token.type === 'eof') break;
    out += token.value;
  }
  return out;
}

function isDigit(ch) {
  return ch >= '0' && ch <= '9';
}

function isIdentStart(ch) {
  return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch === '_';
}

function isIdentContinue(ch) {
  return isIdentStart(ch) || isDigit(ch);
}

module.exports = {
  tokenize,
  tokensToSource,
};
