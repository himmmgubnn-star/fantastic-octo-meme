'use strict';

const { LUA_KEYWORDS, ROBLOX_GLOBALS } = require('./keywords');
const { tokenize, tokensToSource } = require('./tokenizer');

/**
 * @typedef {import('./random').Random} Random
 * @typedef {import('./tokenizer').Token} Token
 */

/**
 * Strip comments and collapse insignificant whitespace while preserving
 * required separators between identifiers/keywords/numbers.
 * @param {string} source
 */
function stripCommentsAndMinify(source) {
  const tokens = tokenize(source);
  /** @type {Token[]} */
  const kept = [];

  for (const token of tokens) {
    if (token.type === 'comment' || token.type === 'eof') continue;
    if (token.type === 'whitespace') {
      // defer whitespace decisions to emit phase
      kept.push({ ...token, value: ' ' });
      continue;
    }
    kept.push(token);
  }

  let out = '';
  /** @type {Token | null} */
  let prev = null;

  for (const token of kept) {
    if (token.type === 'whitespace') {
      // only emit space when two tokens would otherwise glue together
      continue;
    }

    if (prev && needsSpaceBetween(prev, token)) {
      out += ' ';
    }
    out += token.value;
    prev = token;
  }

  return out;
}

/**
 * @param {Token} left
 * @param {Token} right
 */
function needsSpaceBetween(left, right) {
  const leftGlue = left.type === 'ident' || left.type === 'keyword' || left.type === 'number';
  const rightGlue = right.type === 'ident' || right.type === 'keyword' || right.type === 'number';
  if (leftGlue && rightGlue) return true;

  // prevent `1..2` ambiguity issues already fine; handle `- -` -> `--`
  if (left.value === '-' && right.value === '-') return true;
  if (left.value === '/' && right.value === '/') return true;
  if (left.value.endsWith('-') && right.type === 'number') return true;

  return false;
}

/**
 * Collect local variable / function names that are safe to rename.
 * Heuristic scope walk over tokens — good enough for ModuleScripts without
 * pulling in a full Luau parser dependency.
 *
 * @param {Token[]} tokens
 * @returns {Set<string>}
 */
function collectLocalNames(tokens) {
  /** @type {Set<string>} */
  const locals = new Set();

  for (let i = 0; i < tokens.length; i += 1) {
    const token = tokens[i];
    if (token.type !== 'keyword') continue;

    if (token.value === 'local') {
      // local function name
      const afterLocal = nextSignificant(tokens, i + 1);
      if (!afterLocal) continue;

      if (afterLocal.token.type === 'keyword' && afterLocal.token.value === 'function') {
        const nameTok = nextSignificant(tokens, afterLocal.index + 1);
        if (nameTok && nameTok.token.type === 'ident' && isRenameable(nameTok.token.value)) {
          locals.add(nameTok.token.value);
        }
        // also capture parameters
        collectParamList(tokens, nameTok ? nameTok.index + 1 : afterLocal.index + 1, locals);
        continue;
      }

      // local a, b, c = ...
      let idx = i + 1;
      while (idx < tokens.length) {
        const n = nextSignificant(tokens, idx);
        if (!n) break;
        if (n.token.type === 'ident' && isRenameable(n.token.value)) {
          locals.add(n.token.value);
          idx = n.index + 1;
          const comma = nextSignificant(tokens, idx);
          if (comma && comma.token.type === 'symbol' && comma.token.value === ',') {
            idx = comma.index + 1;
            continue;
          }
        }
        break;
      }
      continue;
    }

    if (token.value === 'function') {
      // function foo() / function a.b:c() — only plain local-style names when preceded by nothing global-ish
      // Capture parameters for all function forms.
      const maybeName = nextSignificant(tokens, i + 1);
      if (maybeName && maybeName.token.type === 'ident') {
        // function Name(...)  — treat as renameable only if not a method path
        const afterName = nextSignificant(tokens, maybeName.index + 1);
        if (afterName && afterName.token.type === 'symbol' && afterName.token.value === '(') {
          if (isRenameable(maybeName.token.value)) {
            // top-level function declarations are global in Lua — do NOT rename the name
          }
          collectParamList(tokens, maybeName.index + 1, locals);
        } else {
          // function a.b:c( — skip to '('
          let scan = maybeName.index + 1;
          while (scan < tokens.length) {
            const t = tokens[scan];
            if (t.type === 'symbol' && t.value === '(') {
              collectParamList(tokens, scan, locals);
              break;
            }
            scan += 1;
          }
        }
      } else if (maybeName && maybeName.token.type === 'symbol' && maybeName.token.value === '(') {
        // anonymous function
        collectParamList(tokens, maybeName.index, locals);
      }
      continue;
    }

    if (token.value === 'for') {
      // for i = ... / for k, v in ...
      let idx = i + 1;
      while (idx < tokens.length) {
        const n = nextSignificant(tokens, idx);
        if (!n) break;
        if (n.token.type === 'ident' && isRenameable(n.token.value)) {
          locals.add(n.token.value);
          idx = n.index + 1;
          const sep = nextSignificant(tokens, idx);
          if (sep && sep.token.type === 'symbol' && sep.token.value === ',') {
            idx = sep.index + 1;
            continue;
          }
        }
        break;
      }
    }
  }

  return locals;
}

/**
 * @param {Token[]} tokens
 * @param {number} openParenIndex  index at or before '('
 * @param {Set<string>} locals
 */
function collectParamList(tokens, openParenIndex, locals) {
  let idx = openParenIndex;
  const open = nextSignificant(tokens, idx);
  if (!open || open.token.type !== 'symbol' || open.token.value !== '(') return;

  idx = open.index + 1;
  while (idx < tokens.length) {
    const n = nextSignificant(tokens, idx);
    if (!n) return;
    if (n.token.type === 'symbol' && n.token.value === ')') return;

    if (n.token.type === 'ident' && n.token.value !== '...' && isRenameable(n.token.value)) {
      locals.add(n.token.value);
    }
    // Luau typed params: name: Type — skip until comma or ')'
    idx = n.index + 1;
    while (idx < tokens.length) {
      const t = tokens[idx];
      if (t.type === 'symbol' && (t.value === ',' || t.value === ')')) {
        if (t.value === ')') return;
        idx += 1;
        break;
      }
      idx += 1;
    }
  }
}

/**
 * @param {Token[]} tokens
 * @param {number} from
 * @returns {{ token: Token, index: number } | null}
 */
function nextSignificant(tokens, from) {
  for (let i = from; i < tokens.length; i += 1) {
    const t = tokens[i];
    if (t.type === 'whitespace' || t.type === 'comment') continue;
    if (t.type === 'eof') return null;
    return { token: t, index: i };
  }
  return null;
}

function isRenameable(name) {
  if (!name || name === '...') return false;
  if (LUA_KEYWORDS.has(name)) return false;
  if (ROBLOX_GLOBALS.has(name)) return false;
  if (name.startsWith('__')) return false; // metamethods / reserved conventions
  return true;
}

/**
 * Rename collected locals to opaque identifiers.
 * @param {string} source
 * @param {Random} random
 */
function renameLocals(source, random) {
  const tokens = tokenize(source);
  const locals = collectLocalNames(tokens);

  if (locals.size === 0) {
    return { source, renameMap: new Map() };
  }

  /** @type {Map<string, string>} */
  const renameMap = new Map();
  const used = new Set();

  for (const name of locals) {
    let nextName;
    do {
      nextName = random.ident(random.int(6, 10));
    } while (used.has(nextName) || LUA_KEYWORDS.has(nextName) || ROBLOX_GLOBALS.has(nextName));
    used.add(nextName);
    renameMap.set(name, nextName);
  }

  // Walk tokens and rename idents that are not property accesses (a.b / a:b / { b = })
  for (let i = 0; i < tokens.length; i += 1) {
    const token = tokens[i];
    if (token.type !== 'ident') continue;
    if (!renameMap.has(token.value)) continue;

    const prev = previousSignificant(tokens, i - 1);
    if (prev && prev.type === 'symbol' && (prev.value === '.' || prev.value === ':')) {
      continue; // property / method name
    }

    // table field declaration: { Name = ... } or Name = when preceded by separator and followed by =
    // Only skip when clearly a literal key: previous is '{' or ',' and next is '='
    const next = nextSignificant(tokens, i + 1);
    if (
      prev &&
      prev.type === 'symbol' &&
      (prev.value === '{' || prev.value === ',') &&
      next &&
      next.token.type === 'symbol' &&
      next.token.value === '='
    ) {
      continue;
    }

    // ["Name"] style already string — fine
    // typed Luau: name: Type — still rename the binding
    token.value = /** @type {string} */ (renameMap.get(token.value));
  }

  return { source: tokensToSource(tokens), renameMap };
}

/**
 * @param {Token[]} tokens
 * @param {number} from
 * @returns {Token | null}
 */
function previousSignificant(tokens, from) {
  for (let i = from; i >= 0; i -= 1) {
    const t = tokens[i];
    if (t.type === 'whitespace' || t.type === 'comment') continue;
    return t;
  }
  return null;
}

/**
 * Encode a JS string into a Lua-safe double-quoted literal with escapes.
 * @param {string} value
 */
function luaStringLiteral(value) {
  let out = '"';
  for (let i = 0; i < value.length; i += 1) {
    const c = value[i];
    const code = value.charCodeAt(i);
    if (c === '\\') out += '\\\\';
    else if (c === '"') out += '\\"';
    else if (c === '\n') out += '\\n';
    else if (c === '\r') out += '\\r';
    else if (c === '\t') out += '\\t';
    else if (c === '\0') out += '\\0';
    else if (code < 32 || code === 127) out += `\\${code}`;
    else out += c;
  }
  out += '"';
  return out;
}

/**
 * Decode a Lua string token into a raw JS string (best-effort for short/long forms).
 * @param {string} tokenValue
 * @returns {string | null} null if unsupported
 */
function decodeLuaStringToken(tokenValue) {
  if (!tokenValue) return null;

  // Long string
  if (tokenValue.startsWith('[')) {
    const match = tokenValue.match(/^\[(=*)\[([\s\S]*)\]\1\]$/);
    if (!match) return null;
    let inner = match[2];
    // Lua skips a leading newline in long strings
    if (inner.startsWith('\r\n')) inner = inner.slice(2);
    else if (inner.startsWith('\n')) inner = inner.slice(1);
    return inner;
  }

  const quote = tokenValue[0];
  if (quote !== '"' && quote !== "'") return null;
  if (tokenValue[tokenValue.length - 1] !== quote) return null;

  let out = '';
  for (let i = 1; i < tokenValue.length - 1; i += 1) {
    const c = tokenValue[i];
    if (c !== '\\') {
      out += c;
      continue;
    }
    i += 1;
    if (i >= tokenValue.length - 1) break;
    const e = tokenValue[i];
    switch (e) {
      case 'a':
        out += '\x07';
        break;
      case 'b':
        out += '\b';
        break;
      case 'f':
        out += '\f';
        break;
      case 'n':
        out += '\n';
        break;
      case 'r':
        out += '\r';
        break;
      case 't':
        out += '\t';
        break;
      case 'v':
        out += '\v';
        break;
      case '\\':
      case '"':
      case "'":
      case '\n':
        out += e;
        break;
      case 'x': {
        const hex = tokenValue.slice(i + 1, i + 3);
        if (/^[0-9a-fA-F]{2}$/.test(hex)) {
          out += String.fromCharCode(Number.parseInt(hex, 16));
          i += 2;
        } else {
          out += e;
        }
        break;
      }
      case 'u': {
        // Luau \u{...}
        if (tokenValue[i + 1] === '{') {
          const end = tokenValue.indexOf('}', i + 2);
          if (end !== -1) {
            const hex = tokenValue.slice(i + 2, end);
            const cp = Number.parseInt(hex, 16);
            if (Number.isFinite(cp)) out += String.fromCodePoint(cp);
            i = end;
            break;
          }
        }
        out += e;
        break;
      }
      default: {
        if (e >= '0' && e <= '9') {
          let digits = e;
          while (digits.length < 3 && i + 1 < tokenValue.length - 1) {
            const n = tokenValue[i + 1];
            if (n < '0' || n > '9') break;
            digits += n;
            i += 1;
          }
          out += String.fromCharCode(Number.parseInt(digits, 10) % 256);
        } else {
          out += e;
        }
      }
    }
  }
  return out;
}

/**
 * Extract string literals into an encrypted string table and replace them
 * with runtime decoder calls. Leaves empty strings and very short strings
 * alone when encryption would bloat more than it helps — still encrypts
 * everything by default for protection.
 *
 * @param {string} source
 * @param {Random} random
 * @param {{ minLength?: number }} [options]
 */
function encryptStrings(source, random, options = {}) {
  const minLength = options.minLength ?? 1;
  const tokens = tokenize(source);

  /** @type {string[]} */
  const table = [];
  /** @type {Map<string, number>} */
  const indexByRaw = new Map();

  const xorKey = random.int(1, 255);
  const decoderName = random.ident(8);
  const tableName = random.ident(8);
  const keyName = random.ident(6);

  for (const token of tokens) {
    if (token.type !== 'string') continue;
    const raw = decodeLuaStringToken(token.value);
    if (raw === null) continue;
    if (raw.length < minLength) continue;

    let idx = indexByRaw.get(raw);
    if (idx === undefined) {
      idx = table.length;
      indexByRaw.set(raw, idx);
      table.push(raw);
    }
    token.value = `${decoderName}(${tableName}[${idx + 1}])`;
  }

  if (table.length === 0) {
    return source;
  }

  const encodedLiterals = table.map((raw) => {
    const bytes = [];
    for (let i = 0; i < raw.length; i += 1) {
      const code = raw.charCodeAt(i);
      // Keep within byte range for string.char; multi-byte unicode split
      if (code <= 255) {
        bytes.push(code ^ xorKey);
      } else {
        // encode as UTF-8 bytes then xor
        const utf8 = Buffer.from(raw[i], 'utf8');
        for (const b of utf8) bytes.push(b ^ xorKey);
      }
    }
    // Store as a Lua string of xored bytes via \ddd escapes (safe binary)
    let lit = '"';
    for (const b of bytes) {
      lit += `\\${b}`;
    }
    lit += '"';
    return lit;
  });

  const preamble = [
    `local ${keyName}=${xorKey}`,
    `local ${tableName}={${encodedLiterals.join(',')}}`,
    `local function ${decoderName}(s)local t={}for i=1,#s do t[i]=string.char(bit32.bxor(string.byte(s,i),${keyName}))end return table.concat(t)end`,
  ].join(';');

  return `${preamble};${tokensToSource(tokens)}`;
}

/**
 * Wrap numbers in opaque arithmetic expressions that fold at runtime.
 * Skips indexes that would break table/list readability too aggressively
 * by only transforming a subset.
 *
 * @param {string} source
 * @param {Random} random
 * @param {{ density?: number }} [options] probability 0..1
 */
function opaqueNumbers(source, random, options = {}) {
  const density = options.density ?? 0.55;
  const tokens = tokenize(source);

  for (let i = 0; i < tokens.length; i += 1) {
    const token = tokens[i];
    if (token.type !== 'number') continue;
    if (random.next() > density) continue;

    // Don't touch hex or numbers with underscores / scientific for safety
    if (/[_xXeE]/.test(token.value)) continue;

    const n = Number(token.value);
    if (!Number.isFinite(n) || !Number.isInteger(n)) continue;
    if (Math.abs(n) > 1_000_000) continue;

    const a = random.int(2, 40);
    const b = random.int(2, 40);
    const form = random.int(0, 3);

    let expr;
    switch (form) {
      case 0: {
        // (n + a) - a
        expr = `((${n + a})-${a})`;
        break;
      }
      case 1: {
        // (n - a) + a
        expr = `((${n - a})+${a})`;
        break;
      }
      case 2: {
        // bit32.bxor(bit32.bxor(n, a), a)
        expr = `bit32.bxor(bit32.bxor(${n},${a}),${a})`;
        break;
      }
      default: {
        // n = q*b + r
        const q = Math.trunc(n / b);
        const r = n - q * b;
        expr = `((${q})*${b}+${r})`;
        break;
      }
    }

    // Replace number token with symbol soup — tokenizer won't reparse; inject as symbol
    token.type = 'symbol';
    token.value = expr;
  }

  return tokensToSource(tokens);
}

/**
 * Inject opaque predicates and dead branches that never change control flow.
 * @param {string} source
 * @param {Random} random
 * @param {{ maxInject?: number }} [options]
 */
function injectOpaquePredicates(source, random, options = {}) {
  const maxInject = options.maxInject ?? 6;
  const lines = source.split('\n');
  if (lines.length < 2) return source;

  const injections = Math.min(maxInject, Math.max(1, Math.floor(lines.length / 8)));
  const usedLines = new Set();

  for (let n = 0; n < injections; n += 1) {
    let lineIdx = random.int(0, lines.length - 1);
    let guard = 0;
    while (usedLines.has(lineIdx) && guard < 10) {
      lineIdx = random.int(0, lines.length - 1);
      guard += 1;
    }
    usedLines.add(lineIdx);

    const a = random.int(1, 20);
    const b = a; // equal — predicate always true/false predictably
    const alwaysTrue = random.next() < 0.5;

    let stub;
    if (alwaysTrue) {
      // if a==a then <empty> else dead end
      const dead = random.ident(5);
      stub = `if ${a}==${b} then else local ${dead}=nil;${dead}=${dead} end;`;
    } else {
      const dead = random.ident(5);
      stub = `if ${a}~=${b} then local ${dead}=0;while ${dead}<0 do ${dead}=${dead}+1 end end;`;
    }

    lines[lineIdx] = `${stub}${lines[lineIdx]}`;
  }

  return lines.join('\n');
}

/**
 * Control-flow flattening for top-level statement chunks separated by
 * semicolons / newlines. Conservative: only flattens when we detect a
 * simple linear sequence without unbalanced blocks.
 *
 * @param {string} source
 * @param {Random} random
 */
function flattenControlFlow(source, random) {
  // Split into top-level statements using a lightweight block-depth scan
  const statements = splitTopLevelStatements(source);
  if (statements.length < 3 || statements.length > 40) {
    return source; // too small or too large for meaningful flattening
  }

  // Reject if any statement still has unbalanced block keywords (nested chunks)
  for (const stmt of statements) {
    if (blockDepthDelta(stmt) !== 0) return source;
  }

  const stateName = random.ident(7);
  const order = statements.map((_, idx) => idx);
  // Fisher-Yates shuffle of dispatch order mapping
  const dispatch = order.slice();
  for (let i = dispatch.length - 1; i > 0; i -= 1) {
    const j = random.int(0, i);
    [dispatch[i], dispatch[j]] = [dispatch[j], dispatch[i]];
  }

  // state values for each original index
  const stateOf = order.map(() => random.int(100, 999));
  // ensure unique states
  const seen = new Set();
  for (let i = 0; i < stateOf.length; i += 1) {
    while (seen.has(stateOf[i])) stateOf[i] = random.int(100, 9999);
    seen.add(stateOf[i]);
  }

  const endState = random.int(10000, 20000);
  const cases = [];

  for (let originalIndex = 0; originalIndex < statements.length; originalIndex += 1) {
    const nextState =
      originalIndex + 1 < statements.length ? stateOf[originalIndex + 1] : endState;
    const body = statements[originalIndex].trim();
    if (!body) continue;
    cases.push(
      `elseif ${stateName}==${stateOf[originalIndex]} then ${body};${stateName}=${nextState}`,
    );
  }

  if (cases.length < 3) return source;

  // first case uses if, rest elseif — fix first prefix
  cases[0] = cases[0].replace(/^elseif/, 'if');

  return `local ${stateName}=${stateOf[0]};while ${stateName}~=${endState} do ${cases.join(' ')} else ${stateName}=${endState} end end`;
}

/**
 * @param {string} source
 * @returns {string[]}
 */
function splitTopLevelStatements(source) {
  const tokens = tokenize(source);
  /** @type {string[]} */
  const statements = [];
  let depth = 0;
  let current = '';
  let pending = '';

  /**
   * Block depth rules (Lua):
   * - function / if / repeat / bare do  → +1
   * - for / while headers already open a block; their trailing `do` must NOT +1 again
   * - end / until → -1
   * Bracket depth is tracked separately so table/call commas don't split statements.
   */
  let expectingDo = false;
  let bracketDepth = 0;

  for (let i = 0; i < tokens.length; i += 1) {
    const token = tokens[i];
    if (token.type === 'eof') break;

    if (token.type === 'keyword') {
      if (token.value === 'function' || token.value === 'if' || token.value === 'repeat') {
        depth += 1;
        expectingDo = false;
      } else if (token.value === 'for' || token.value === 'while') {
        depth += 1;
        expectingDo = true;
      } else if (token.value === 'do') {
        if (expectingDo) {
          expectingDo = false;
        } else {
          depth += 1;
        }
      } else if (token.value === 'end' || token.value === 'until') {
        depth = Math.max(0, depth - 1);
        expectingDo = false;
      }
    } else if (token.type === 'symbol') {
      if (token.value === '(' || token.value === '{' || token.value === '[') {
        bracketDepth += 1;
      } else if (token.value === ')' || token.value === '}' || token.value === ']') {
        bracketDepth = Math.max(0, bracketDepth - 1);
      }
    }

    current += token.value;

    const atTop = depth === 0 && bracketDepth === 0;
    const isSep =
      atTop &&
      ((token.type === 'symbol' && token.value === ';') ||
        (token.type === 'whitespace' && token.value.includes('\n')));

    if (isSep) {
      const clean = current.trim().replace(/;+\s*$/, '').trim();
      if (clean) statements.push(clean);
      current = '';
    } else {
      pending = current;
    }
  }

  const tail = (current || pending).trim().replace(/;+\s*$/, '').trim();
  if (tail) statements.push(tail);

  return statements.filter(Boolean);
}

/**
 * @param {string} stmt
 */
function blockDepthDelta(stmt) {
  const tokens = tokenize(stmt);
  let depth = 0;
  let expectingDo = false;
  for (const token of tokens) {
    if (token.type !== 'keyword') continue;
    if (token.value === 'function' || token.value === 'if' || token.value === 'repeat') {
      depth += 1;
      expectingDo = false;
    } else if (token.value === 'for' || token.value === 'while') {
      depth += 1;
      expectingDo = true;
    } else if (token.value === 'do') {
      if (expectingDo) expectingDo = false;
      else depth += 1;
    } else if (token.value === 'end' || token.value === 'until') {
      depth -= 1;
      expectingDo = false;
    }
  }
  return depth;
}

/**
 * Final bootstrap wrapper — adds a decoy env and anti-tamper noise.
 * @param {string} source
 * @param {Random} random
 * @param {{ watermark?: string, antiTamper?: boolean }} [options]
 */
function wrapBootstrap(source, random, options = {}) {
  const bag = random.ident(8);
  const decoy = random.ident(7);
  const stamp = random.int(1e6, 9e6);
  const watermark = sanitizeWatermark(options.watermark);
  const header = watermark
    ? `-- obfuscated by discord-lua-obfuscator | build ${stamp} | ${watermark}`
    : `-- obfuscated by discord-lua-obfuscator | build ${stamp}`;

  /** @type {string[]} */
  const lines = [
    header,
    `local ${bag}=table.freeze and table.freeze({}) or {}`,
    `local ${decoy}=function(...) return ... end`,
  ];

  if (options.antiTamper) {
    const probe = random.ident(6);
    // Cheap integrity noise: decoy proxy + env shape check. Not cryptographic.
    lines.push(
      `local ${probe}=newproxy and newproxy(true) or {}`,
      `if type(string)~="table"or type(table)~="table"or type(bit32)~="table"then return end`,
    );
  }

  lines.push('do', source, 'end');
  return lines.join('\n');
}

/** @param {string | undefined} value */
function sanitizeWatermark(value) {
  if (!value || typeof value !== 'string') return '';
  return value
    .replace(/[\r\n]+/g, ' ')
    .replace(/[^\w .\-#@|/]/g, '')
    .trim()
    .slice(0, 48);
}

/**
 * Split string literals into concatenated chunks so simple greps miss them.
 * Runs BEFORE encryptStrings when both are enabled.
 * @param {string} source
 * @param {Random} random
 * @param {{ minLength?: number, density?: number }} [options]
 */
function splitStrings(source, random, options = {}) {
  const minLength = options.minLength ?? 4;
  const density = options.density ?? 0.7;
  const tokens = tokenize(source);

  for (const token of tokens) {
    if (token.type !== 'string') continue;
    if (random.next() > density) continue;
    const raw = decodeLuaStringToken(token.value);
    if (raw === null || raw.length < minLength) continue;
    // Skip long strings — encryption handles them better
    if (raw.length > 64) continue;

    const parts = [];
    let i = 0;
    while (i < raw.length) {
      const take = Math.min(raw.length - i, random.int(1, Math.min(4, raw.length - i)));
      parts.push(luaStringLiteral(raw.slice(i, i + take)));
      i += take;
    }
    if (parts.length < 2) continue;
    token.type = 'symbol';
    token.value = `(${parts.join('..')})`;
  }

  return tokensToSource(tokens);
}

/**
 * Wrap selected local function declarations in an extra proxy call layer.
 * @param {string} source
 * @param {Random} random
 * @param {{ density?: number }} [options]
 */
function wrapFunctionProxies(source, random, options = {}) {
  const density = options.density ?? 0.45;
  const tokens = tokenize(source);
  /** @type {string[]} */
  const wrappers = [];

  for (let i = 0; i < tokens.length; i += 1) {
    const token = tokens[i];
    if (token.type !== 'keyword' || token.value !== 'local') continue;

    const afterLocal = nextSignificant(tokens, i + 1);
    if (!afterLocal || afterLocal.token.type !== 'keyword' || afterLocal.token.value !== 'function') {
      continue;
    }
    if (random.next() > density) continue;

    const nameTok = nextSignificant(tokens, afterLocal.index + 1);
    if (!nameTok || nameTok.token.type !== 'ident') continue;

    // Only plain `local function name(` forms
    const open = nextSignificant(tokens, nameTok.index + 1);
    if (!open || open.token.type !== 'symbol' || open.token.value !== '(') continue;

    const original = nameTok.token.value;
    const proxy = random.ident(7);
    // Rename declaration to proxy, then assign original = wrapper(proxy) later is hard
    // without full AST. Instead inject after the function's matching end.
    const endIdx = findMatchingEnd(tokens, afterLocal.index);
    if (endIdx < 0) continue;

    const wrapName = random.ident(6);
    wrappers.push({ after: endIdx, code: `;local ${original}=(function(${wrapName})return function(...)return ${wrapName}(...)end end)(${original})` });
    // Point declaration name at a temp, then restore via wrapper using original binding —
    // simpler approach: keep name, wrap in-place after end by rebinding:
    // local function foo() ... end  =>  local function foo() ... end; foo = (function(f) return function(...) return f(...) end end)(foo)
    // But `local function foo` already bound foo. Reassignment works for local.
    void proxy;
  }

  if (wrappers.length === 0) return source;

  // Apply from the end so indices stay valid
  wrappers.sort((a, b) => b.after - a.after);
  for (const w of wrappers) {
    const t = tokens[w.after];
    if (!t) continue;
    t.value = `${t.value}${w.code}`;
  }

  return tokensToSource(tokens);
}

/**
 * Find the `end` matching a block-opening keyword at openIdx.
 * @param {Token[]} tokens
 * @param {number} openIdx
 */
function findMatchingEnd(tokens, openIdx) {
  let depth = 0;
  let expectingDo = false;
  for (let i = openIdx; i < tokens.length; i += 1) {
    const token = tokens[i];
    if (token.type !== 'keyword') continue;
    if (token.value === 'function' || token.value === 'if' || token.value === 'repeat') {
      depth += 1;
      expectingDo = false;
    } else if (token.value === 'for' || token.value === 'while') {
      depth += 1;
      expectingDo = true;
    } else if (token.value === 'do') {
      if (expectingDo) expectingDo = false;
      else depth += 1;
    } else if (token.value === 'end' || token.value === 'until') {
      depth -= 1;
      if (depth === 0 && token.value === 'end') return i;
    }
  }
  return -1;
}

/**
 * Inject junk constant locals that are never read (noise for static readers).
 * @param {string} source
 * @param {Random} random
 * @param {{ count?: number }} [options]
 */
function injectJunkLocals(source, random, options = {}) {
  const count = options.count ?? random.int(2, 5);
  /** @type {string[]} */
  const junk = [];
  for (let i = 0; i < count; i += 1) {
    const name = random.ident(random.int(5, 8));
    const form = random.int(0, 3);
    if (form === 0) junk.push(`local ${name}=${random.int(0, 9999)}`);
    else if (form === 1) junk.push(`local ${name}=${luaStringLiteral(random.hexName(3))}`);
    else if (form === 2) junk.push(`local ${name}={}`);
    else junk.push(`local ${name}=function()return ${random.int(0, 9)}end`);
  }
  return `${junk.join(';')};${source}`;
}

/**
 * VM-lite: encode short linear top-level assignments into a tiny opcode dispatcher.
 * Very conservative — skips if the chunk is not a simple straight-line prologue.
 * @param {string} source
 * @param {Random} random
 */
function vmEncodePrologue(source, random) {
  const statements = splitTopLevelStatements(source);
  if (statements.length < 4 || statements.length > 12) return source;

  // Only encode plain `local name = <simple>` statements at the start
  /** @type {{ name: string, expr: string }[]} */
  const locals = [];
  let consumed = 0;
  for (const stmt of statements) {
    const m = stmt.match(/^local\s+([A-Za-z_][\w]*)\s*=\s*(.+)$/);
    if (!m) break;
    // reject nested blocks / functions
    if (/\bfunction\b|\bdo\b|\bif\b/.test(m[2])) break;
    if (blockDepthDelta(m[2]) !== 0) break;
    locals.push({ name: m[1], expr: m[2].trim() });
    consumed += 1;
    if (consumed >= 8) break;
  }

  if (locals.length < 3) return source;

  const rest = statements.slice(consumed).join(';\n');
  const vmName = random.ident(6);
  const opLoad = random.int(10, 30);
  const opEnd = random.int(40, 60);
  const pc = random.ident(4);
  const ops = random.ident(5);
  const slot = random.ident(5);

  /** @type {string[]} */
  const bytecode = [];
  /** @type {string[]} */
  const bindings = [];

  for (let i = 0; i < locals.length; i += 1) {
    // opcode, dest index, immediate expression stored beside
    bytecode.push(`{${opLoad},${i + 1}}`);
  }
  bytecode.push(`{${opEnd},0}`);

  const exprTable = locals.map((l) => l.expr).join(',');
  for (let i = 0; i < locals.length; i += 1) {
    bindings.push(`local ${locals[i].name}=${slot}[${i + 1}]`);
  }

  const exprs = random.ident(5);
  const vm = [
    `local ${ops}={${bytecode.join(',')}}`,
    `local ${exprs}={${exprTable}}`,
    `local ${slot}={}`,
    `local ${pc}=1`,
    `while true do`,
    `local _op=${ops}[${pc}];if not _op then break end`,
    `if _op[1]==${opLoad} then ${slot}[_op[2]]=${exprs}[_op[2]];${pc}=${pc}+1`,
    `elseif _op[1]==${opEnd} then break`,
    `else break end`,
    `end`,
    bindings.join(';'),
  ].join(' ');

  return rest ? `${vm};${rest}` : vm;
}

module.exports = {
  stripCommentsAndMinify,
  renameLocals,
  encryptStrings,
  opaqueNumbers,
  injectOpaquePredicates,
  flattenControlFlow,
  wrapBootstrap,
  splitStrings,
  wrapFunctionProxies,
  injectJunkLocals,
  vmEncodePrologue,
  // exported for tests
  collectLocalNames,
  decodeLuaStringToken,
  luaStringLiteral,
  tokenize,
  sanitizeWatermark,
};
