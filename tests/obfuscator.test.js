'use strict';

const test = require('node:test');
const assert = require('node:assert');
const luaparse = require('luaparse');

const { obfuscate, scanSource, analyzeSource, ObfuscationError } = require('../src/obfuscator');

const SAMPLE = `
-- ModuleScript demo
local Config = require(game.ReplicatedStorage.Config)
local function computeDamage(base, multiplier)
    local secret = 'player-damage-key-12345'
    if base > 10 then
        return base * multiplier + #secret
    end
    return base
end

for i = 1, 10 do
    local x = i * 2
end

return { damage = computeDamage(25, 1.5) }
`;

function assertParses(code, label) {
  assert.doesNotThrow(
    () => luaparse.parse(code, { luaVersion: '5.1' }),
    `${label} output is not valid Lua`,
  );
}

test('all levels produce parseable Lua', () => {
  for (const level of ['light', 'standard', 'heavy', 'maximum']) {
    const { code } = obfuscate(SAMPLE, { level, seed: 42 });
    assert.ok(code.length > 0, `${level} produced empty output`);
    assertParses(code, level);
  }
});

test('maximum enables closure wrapping, dead code, boolean obfuscation', () => {
  const { code, meta } = obfuscate(SAMPLE, {
    level: 'maximum',
    seed: 42,
    watermark: 'Max',
  });
  assert.ok(meta.passes.includes('closure-wrap'), 'missing closure-wrap pass');
  assert.ok(meta.passes.includes('dead-code'), 'missing dead-code pass');
  assert.ok(meta.passes.includes('boolean-obfuscation'), 'missing boolean-obfuscation pass');
  assert.match(code, /\(function\(\.\.\.\)/);
});

test('boolean obfuscation replaces literals with equivalent expressions', () => {
  const { Random } = require('../src/obfuscator/random');
  const passes = require('../src/obfuscator/passes');
  const src = 'local a = true local b = false return a or b';
  const out = passes.obfuscateBooleans(src, new Random(5), { density: 1 });
  assertParses(out, 'boolean-obfuscation');
  assert.ok(!/\b(true|false)\b/.test(out), 'boolean literals still present');
});


test('output differs across seeds', () => {
  const a = obfuscate(SAMPLE, { level: 'standard', seed: 1 }).code;
  const b = obfuscate(SAMPLE, { level: 'standard', seed: 2 }).code;
  assert.notStrictEqual(a, b);
});

test('watermark appears in output header', () => {
  const { code, meta } = obfuscate(SAMPLE, { level: 'light', watermark: 'AcmeCorp' });
  assert.strictEqual(meta.watermark, 'AcmeCorp');
  assert.match(code, /AcmeCorp/);
});

test('comments are stripped by minify', () => {
  const { code } = obfuscate('-- secret comment\nlocal x = 1\nreturn x', { level: 'light' });
  assert.ok(!code.includes('secret comment'));
});

test('rejects empty source', () => {
  assert.throws(() => obfuscate('   '), (err) => err instanceof ObfuscationError && err.code === 'EMPTY_SOURCE');
});

test('rejects binary-looking source', () => {
  assert.throws(
    () => obfuscate('local x = 1\x01\x02 return x'),
    (err) => err instanceof ObfuscationError,
  );
});

test('maxOutputBytes cap triggers OUTPUT_TOO_LARGE', () => {
  assert.throws(
    () => obfuscate(SAMPLE, { level: 'heavy', maxOutputBytes: 100 }),
    (err) => err instanceof ObfuscationError && err.code === 'OUTPUT_TOO_LARGE',
  );
});

test('scanSource grades and finds issues', () => {
  const scan = scanSource("local webhook = 'https://discord.com/api/webhooks/123/abc'");
  assert.ok(scan.findings.length > 0);
  assert.ok(['A', 'B', 'C', 'D', 'F'].includes(scan.metrics.grade));
});

test('analyzeSource reports metrics and recommendations', () => {
  const report = analyzeSource(SAMPLE);
  assert.ok(report.functions >= 1);
  assert.ok(report.recommendations.length > 0);
  assert.ok(report.lines > 5);
});
