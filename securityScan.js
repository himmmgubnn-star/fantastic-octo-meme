'use strict';

const { tokenize } = require('./tokenizer');
const { decodeLuaStringToken } = require('./passes');

/**
 * @typedef {'critical' | 'high' | 'medium' | 'low' | 'info'} Severity
 *
 * @typedef {object} Finding
 * @property {Severity} severity
 * @property {string} code
 * @property {string} title
 * @property {string} detail
 * @property {number} [line]
 * @property {string} [snippet]
 *
 * @typedef {object} ScanResult
 * @property {Finding[]} findings
 * @property {{ lines: number, bytes: number, functions: number, strings: number, locals: number, comments: number, score: number, grade: string }} metrics
 * @property {string} summary
 */

/** @type {{ id: string, severity: Severity, title: string, pattern: RegExp, detail: string }[]} */
const PATTERN_RULES = [
  {
    id: 'SECRET_DISCORD_TOKEN',
    severity: 'critical',
    title: 'Possible Discord bot token',
    pattern: /[MN][A-Za-z\d]{23,}\.[\w-]{6}\.[\w-]{27,}/g,
    detail: 'Hard-coded Discord tokens can be stolen from dumped modules. Use server-side secrets only.',
  },
  {
    id: 'SECRET_GITHUB_PAT',
    severity: 'critical',
    title: 'Possible GitHub personal access token',
    pattern: /gh[pousr]_[A-Za-z0-9_]{20,}/g,
    detail: 'Rotate this credential immediately and keep it off the client.',
  },
  {
    id: 'SECRET_AWS_KEY',
    severity: 'critical',
    title: 'Possible AWS access key',
    pattern: /AKIA[0-9A-Z]{16}/g,
    detail: 'Cloud keys must never ship inside Roblox modules.',
  },
  {
    id: 'SECRET_GENERIC_KEY',
    severity: 'high',
    title: 'Possible API key / secret assignment',
    pattern:
      /(?:api[_-]?key|secret[_-]?key|auth[_-]?token|private[_-]?key|webhook[_-]?url)\s*=\s*["'][^"']{8,}["']/gi,
    detail: 'Secret-looking assignments should live on a secure backend, not in shared modules.',
  },
  {
    id: 'WEBHOOK_URL',
    severity: 'high',
    title: 'Discord webhook URL',
    pattern: /https:\/\/(?:discord\.com|discordapp\.com)\/api\/webhooks\/\d+\/[\w-]+/gi,
    detail: 'Embedded webhooks are commonly stolen from client modules and spammed.',
  },
  {
    id: 'LOADSTRING',
    severity: 'high',
    title: 'loadstring / load usage',
    pattern: /\bloadstring\s*\(|\bload\s*\(/g,
    detail: 'Dynamic code loading is a favorite of dumpers and malware. Prefer static modules.',
  },
  {
    id: 'GETFENV',
    severity: 'medium',
    title: 'getfenv / setfenv usage',
    pattern: /\bgetfenv\s*\(|\bsetfenv\s*\(/g,
    detail: 'Environment tampering is rare in legitimate Roblox code and attracts reverse engineers.',
  },
  {
    id: 'HTTP_SERVICE',
    severity: 'medium',
    title: 'HttpService usage',
    pattern: /HttpService|RequestAsync|GetAsync|PostAsync|JSONEncode/g,
    detail: 'Ensure requests go to your own backend and never embed auth headers in the module.',
  },
  {
    id: 'REQUIRE_HTTP',
    severity: 'high',
    title: 'require of non-numeric / remote-looking target',
    pattern: /require\s*\(\s*["']https?:/gi,
    detail: 'Remote require patterns are unsafe and unsupported on Roblox.',
  },
  {
    id: 'SYNAPSE_LIKE',
    severity: 'medium',
    title: 'Executor / exploit API identifiers',
    pattern:
      /\b(?:syn|getgenv|getrenv|getsenv|getrawmetatable|setclipboard|writefile|readfile|isfolder|makefolder|Drawing|hookfunction|hookmetamethod|checkcaller|newcclosure)\b/g,
    detail: 'These names usually indicate exploit-oriented code, not a normal game module.',
  },
  {
    id: 'HARDCODED_PASSWORD',
    severity: 'high',
    title: 'Hard-coded password-like string',
    pattern: /password\s*=\s*["'][^"']+["']/gi,
    detail: 'Passwords in modules are visible to anyone who obtains the source or a dump.',
  },
];

/**
 * @param {string} source
 * @returns {ScanResult}
 */
function scanSource(source) {
  if (typeof source !== 'string' || !source.trim()) {
    return {
      findings: [
        {
          severity: 'info',
          code: 'EMPTY',
          title: 'Empty source',
          detail: 'Nothing to scan.',
        },
      ],
      metrics: emptyMetrics(),
      summary: 'No source provided.',
    };
  }

  /** @type {Finding[]} */
  const findings = [];
  const lines = source.split(/\r?\n/);

  for (const rule of PATTERN_RULES) {
    rule.pattern.lastIndex = 0;
    let match = rule.pattern.exec(source);
    // Cap matches per rule to avoid spam
    let hits = 0;
    while (match && hits < 8) {
      const idx = match.index;
      const line = lineNumberAt(source, idx);
      findings.push({
        severity: rule.severity,
        code: rule.id,
        title: rule.title,
        detail: rule.detail,
        line,
        snippet: trimSnippet(lines[line - 1] || match[0]),
      });
      hits += 1;
      match = rule.pattern.exec(source);
    }
  }

  // Structural checks via tokenizer
  let tokens;
  try {
    tokens = tokenize(source);
  } catch {
    tokens = [];
  }

  let functions = 0;
  let strings = 0;
  let locals = 0;
  let comments = 0;
  /** @type {string[]} */
  const longStrings = [];

  for (const token of tokens) {
    if (token.type === 'keyword' && token.value === 'function') functions += 1;
    if (token.type === 'keyword' && token.value === 'local') locals += 1;
    if (token.type === 'comment') comments += 1;
    if (token.type === 'string') {
      strings += 1;
      const raw = decodeLuaStringToken(token.value);
      if (raw && raw.length >= 80) {
        longStrings.push(raw.slice(0, 120));
      }
    }
  }

  if (longStrings.length > 0) {
    findings.push({
      severity: 'low',
      code: 'LONG_STRING',
      title: `${longStrings.length} long string literal(s)`,
      detail:
        'Long plaintext strings are easy markers for dumpers. Standard/heavy obfuscation encrypts them.',
    });
  }

  if (comments > 15) {
    findings.push({
      severity: 'info',
      code: 'MANY_COMMENTS',
      title: 'Many comments in source',
      detail: 'Comments are stripped during obfuscation, which removes useful hints for crackers.',
    });
  }

  // Balanced block heuristic
  const depth = roughBlockBalance(source);
  if (depth !== 0) {
    findings.push({
      severity: 'medium',
      code: 'UNBALANCED_BLOCKS',
      title: 'Possibly unbalanced blocks',
      detail: `Keyword depth ended at ${depth}. The script may not parse in Luau as-is.`,
    });
  }

  const bytes = Buffer.byteLength(source, 'utf8');
  const metrics = {
    lines: lines.length,
    bytes,
    functions,
    strings,
    locals,
    comments,
    score: 0,
    grade: 'A',
  };

  metrics.score = scoreFindings(findings);
  metrics.grade = gradeFromScore(metrics.score);

  const critical = findings.filter((f) => f.severity === 'critical').length;
  const high = findings.filter((f) => f.severity === 'high').length;
  const summary =
    findings.length === 0
      ? 'No issues detected. Still keep privileged logic on the server.'
      : `Found ${findings.length} issue(s): ${critical} critical, ${high} high. Grade ${metrics.grade}.`;

  // Sort by severity
  const order = { critical: 0, high: 1, medium: 2, low: 3, info: 4 };
  findings.sort((a, b) => order[a.severity] - order[b.severity]);

  return { findings, metrics, summary };
}

function emptyMetrics() {
  return {
    lines: 0,
    bytes: 0,
    functions: 0,
    strings: 0,
    locals: 0,
    comments: 0,
    score: 0,
    grade: 'A',
  };
}

/** @param {Finding[]} findings */
function scoreFindings(findings) {
  const weights = { critical: 40, high: 20, medium: 10, low: 4, info: 0 };
  let score = 0;
  for (const f of findings) score += weights[f.severity] || 0;
  return Math.min(100, score);
}

/** @param {number} score */
function gradeFromScore(score) {
  if (score >= 60) return 'F';
  if (score >= 40) return 'D';
  if (score >= 25) return 'C';
  if (score >= 10) return 'B';
  return 'A';
}

/** @param {string} source @param {number} index */
function lineNumberAt(source, index) {
  let line = 1;
  for (let i = 0; i < index && i < source.length; i += 1) {
    if (source[i] === '\n') line += 1;
  }
  return line;
}

/** @param {string} text */
function trimSnippet(text) {
  const t = (text || '').trim().replace(/\s+/g, ' ');
  return t.length > 90 ? `${t.slice(0, 87)}...` : t;
}

/** @param {string} source */
function roughBlockBalance(source) {
  const tokens = tokenize(source);
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

module.exports = {
  scanSource,
  PATTERN_RULES,
};
