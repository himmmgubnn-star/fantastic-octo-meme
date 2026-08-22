'use strict';

const test = require('node:test');
const assert = require('node:assert');
const { buildCommands } = require('../src/bot/registerCommands');
const embeds = require('../src/bot/embeds');

test('slash command definitions are well-formed', () => {
  const cmds = buildCommands();
  const names = cmds.map((c) => c.name);
  assert.deepStrictEqual(names.sort(), ['analyze', 'compare', 'help', 'obfuscate', 'scan', 'stats']);
  for (const cmd of cmds) {
    assert.ok(cmd.description.length > 0, `${cmd.name} missing description`);
    if (cmd.options) {
      for (const opt of cmd.options) {
        assert.ok(opt.name && opt.description, `${cmd.name}/${opt.name} malformed`);
      }
    }
  }
});

test('embed builders return valid payloads', () => {
  const { obfuscate } = require('../src/obfuscator');
  const result = obfuscate('local x = "hello" return x', { level: 'standard', seed: 3 });

  const obf = embeds.obfuscateEmbed(result, { level: 'standard', watermark: null });
  assert.ok(obf.embed.data.fields.length > 0);

  const scanEmbed = embeds.scanEmbed(require('../src/obfuscator').scanSource('local x = 1'));
  assert.ok(scanEmbed.data.title.includes('Grade'));

  const report = require('../src/obfuscator').analyzeSource('local x = 1');
  assert.ok(embeds.analyzeEmbed(report).data.title.length > 0);
  assert.ok(embeds.helpEmbed().data.fields.length > 0);
});
