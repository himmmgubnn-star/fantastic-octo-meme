'use strict';

const crypto = require('crypto');
const { Random } = require('./random');
const {
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
} = require('./passes');
const { scanSource } = require('./securityScan');
const { analyzeSource } = require('./analyze');

/** @typedef {'light' | 'standard' | 'heavy'} ObfuscationLevel */

/**
 * @typedef {object} ObfuscateOptions
 * @property {ObfuscationLevel} [level]
 * @property {number | string} [seed]
 * @property {boolean} [minify]
 * @property {boolean} [rename]
 * @property {boolean} [strings]
 * @property {boolean} [numbers]
 * @property {boolean} [controlFlow]
 * @property {boolean} [opaquePredicates]
 * @property {boolean} [bootstrap]
 * @property {boolean} [splitStrings]
 * @property {boolean} [proxies]
 * @property {boolean} [junk]
 * @property {boolean} [vm]
 * @property {boolean} [antiTamper]
 * @property {string} [watermark]
 * @property {number} [maxOutputBytes]
 */

/**
 * @typedef {object} ObfuscateResult
 * @property {string} code
 * @property {{
 *   level: ObfuscationLevel,
 *   inputBytes: number,
 *   outputBytes: number,
 *   passes: string[],
 *   renameCount: number,
 *   durationMs: number,
 *   fingerprint: string,
 *   expansion: number,
 *   watermark: string | null,
 *   securityHints: number,
 * }} meta
 */

const LEVEL_PRESETS = Object.freeze({
  light: {
    minify: true,
    rename: true,
    strings: false,
    numbers: false,
    controlFlow: false,
    opaquePredicates: false,
    bootstrap: true,
    splitStrings: false,
    proxies: false,
    junk: false,
    vm: false,
    antiTamper: false,
  },
  standard: {
    minify: true,
    rename: true,
    strings: true,
    numbers: true,
    controlFlow: false,
    opaquePredicates: true,
    bootstrap: true,
    splitStrings: true,
    proxies: true,
    junk: true,
    vm: false,
    antiTamper: true,
  },
  heavy: {
    minify: true,
    rename: true,
    strings: true,
    numbers: true,
    controlFlow: true,
    opaquePredicates: true,
    bootstrap: true,
    splitStrings: true,
    proxies: true,
    junk: true,
    vm: true,
    antiTamper: true,
  },
});

class ObfuscationError extends Error {
  /**
   * @param {string} message
   * @param {string} [code]
   */
  constructor(message, code = 'OBFUSCATION_ERROR') {
    super(message);
    this.name = 'ObfuscationError';
    this.code = code;
  }
}

/**
 * @param {string} source
 */
function validateSource(source) {
  if (typeof source !== 'string') {
    throw new ObfuscationError('Source code must be a string.', 'INVALID_TYPE');
  }

  const trimmed = source.trim();
  if (!trimmed) {
    throw new ObfuscationError('Source code is empty.', 'EMPTY_SOURCE');
  }

  // eslint-disable-next-line no-control-regex
  if (/[\x00-\x08\x0E-\x1F]/.test(source.slice(0, 4096))) {
    throw new ObfuscationError(
      'Source appears to contain binary data, not Lua text.',
      'BINARY_SOURCE',
    );
  }

  return trimmed;
}

/**
 * Multi-pass Luau-aware obfuscator geared toward Roblox ModuleScripts.
 *
 * @param {string} input
 * @param {ObfuscateOptions} [options]
 * @returns {ObfuscateResult}
 */
function obfuscate(input, options = {}) {
  const started = Date.now();
  const source = validateSource(input);
  const level = options.level && LEVEL_PRESETS[options.level] ? options.level : 'standard';
  const preset = LEVEL_PRESETS[level];

  const flags = {
    minify: options.minify ?? preset.minify,
    rename: options.rename ?? preset.rename,
    strings: options.strings ?? preset.strings,
    numbers: options.numbers ?? preset.numbers,
    controlFlow: options.controlFlow ?? preset.controlFlow,
    opaquePredicates: options.opaquePredicates ?? preset.opaquePredicates,
    bootstrap: options.bootstrap ?? preset.bootstrap,
    splitStrings: options.splitStrings ?? preset.splitStrings,
    proxies: options.proxies ?? preset.proxies,
    junk: options.junk ?? preset.junk,
    vm: options.vm ?? preset.vm,
    antiTamper: options.antiTamper ?? preset.antiTamper,
  };

  const watermark =
    typeof options.watermark === 'string' && options.watermark.trim()
      ? options.watermark.trim().slice(0, 48)
      : null;

  const random = new Random(options.seed);
  /** @type {string[]} */
  const passes = [];
  let code = source;
  let renameCount = 0;

  // Pre-pass security hint count (does not fail the build)
  let securityHints = 0;
  try {
    const scan = scanSource(source);
    securityHints = scan.findings.filter(
      (f) => f.severity === 'critical' || f.severity === 'high',
    ).length;
  } catch {
    securityHints = 0;
  }

  // 1. VM-lite prologue (heavy) — needs original structure
  if (flags.vm) {
    code = vmEncodePrologue(code, random);
    passes.push('vm-prologue');
  }

  // 2. Control-flow flattening
  if (flags.controlFlow) {
    code = flattenControlFlow(code, random);
    passes.push('control-flow');
  }

  // 3. Opaque predicates
  if (flags.opaquePredicates) {
    code = injectOpaquePredicates(code, random, {
      maxInject: level === 'heavy' ? 10 : 5,
    });
    passes.push('opaque-predicates');
  }

  // 4. Junk locals
  if (flags.junk) {
    code = injectJunkLocals(code, random, { count: level === 'heavy' ? 5 : 3 });
    passes.push('junk-locals');
  }

  // 5. Function proxies
  if (flags.proxies) {
    code = wrapFunctionProxies(code, random, { density: level === 'heavy' ? 0.6 : 0.4 });
    passes.push('function-proxies');
  }

  // 6. Rename locals
  if (flags.rename) {
    const renamed = renameLocals(code, random);
    code = renamed.source;
    renameCount = renamed.renameMap.size;
    passes.push('rename-locals');
  }

  // 7. Number opacity
  if (flags.numbers) {
    code = opaqueNumbers(code, random, { density: level === 'heavy' ? 0.75 : 0.5 });
    passes.push('opaque-numbers');
  }

  // 8. String splitting (before encryption)
  if (flags.splitStrings) {
    code = splitStrings(code, random, { density: 0.65 });
    passes.push('string-split');
  }

  // 9. String encryption
  if (flags.strings) {
    code = encryptStrings(code, random, { minLength: 1 });
    passes.push('string-encryption');
  }

  // 10. Minify
  if (flags.minify) {
    code = stripCommentsAndMinify(code);
    passes.push('minify');
  }

  // 11. Bootstrap
  if (flags.bootstrap) {
    code = wrapBootstrap(code, random, {
      watermark: watermark || undefined,
      antiTamper: flags.antiTamper,
    });
    passes.push(flags.antiTamper ? 'bootstrap+antitamper' : 'bootstrap');
  }

  const maxOutput = options.maxOutputBytes ?? 0;
  const outputBytes = Buffer.byteLength(code, 'utf8');
  if (maxOutput > 0 && outputBytes > maxOutput) {
    throw new ObfuscationError(
      `Obfuscated output is ${outputBytes} bytes, exceeding the ${maxOutput} byte limit. Try a lighter level or smaller input.`,
      'OUTPUT_TOO_LARGE',
    );
  }

  const inputBytes = Buffer.byteLength(source, 'utf8');
  const fingerprint = crypto
    .createHash('sha256')
    .update(code)
    .digest('hex')
    .slice(0, 12);

  return {
    code,
    meta: {
      level,
      inputBytes,
      outputBytes,
      passes,
      renameCount,
      durationMs: Date.now() - started,
      fingerprint,
      expansion: inputBytes > 0 ? Number((outputBytes / inputBytes).toFixed(2)) : 0,
      watermark,
      securityHints,
    },
  };
}

module.exports = {
  obfuscate,
  ObfuscationError,
  LEVEL_PRESETS,
  scanSource,
  analyzeSource,
};
