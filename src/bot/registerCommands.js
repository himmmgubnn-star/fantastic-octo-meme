'use strict';

const { config } = require('../config');

/** @returns {import('discord.js').ApplicationCommandDataResolvable[]} */
function buildCommands() {
  return [
    {
      name: 'obfuscate',
      description: 'Obfuscate Lua/Luau source for Roblox',
      options: [
        {
          name: 'file',
          description: 'A .lua / .luau / .txt attachment to obfuscate',
          type: 11, // ATTACHMENT
        },
        {
          name: 'code',
          description: 'Paste Lua source directly (if no file)',
          type: 3, // STRING
        },
        {
          name: 'level',
          description: 'Obfuscation strength',
          type: 3,
          choices: [
            { name: 'light', value: 'light' },
            { name: 'standard', value: 'standard' },
            { name: 'heavy', value: 'heavy' },
          ],
        },
        {
          name: 'watermark',
          description: 'Ownership tag embedded as a header comment',
          type: 3,
          max_length: 48,
        },
        {
          name: 'ephemeral',
          description: 'Reply visible only to you (default true)',
          type: 5, // BOOLEAN
        },
      ],
    },
    {
      name: 'scan',
      description: 'Security-scan Lua source for secrets and risky APIs',
      options: [
        {
          name: 'file',
          description: 'A .lua / .luau / .txt attachment to scan',
          type: 11,
        },
        {
          name: 'code',
          description: 'Paste Lua source directly (if no file)',
          type: 3,
        },
      ],
    },
    {
      name: 'analyze',
      description: 'Static metrics and recommendations for Lua source',
      options: [
        {
          name: 'file',
          description: 'A .lua / .luau / .txt attachment to analyze',
          type: 11,
        },
        {
          name: 'code',
          description: 'Paste Lua source directly (if no file)',
          type: 3,
        },
      ],
    },
    {
      name: 'compare',
      description: 'Run light + standard + heavy obfuscation side by side',
      options: [
        {
          name: 'file',
          description: 'A .lua / .luau / .txt attachment',
          type: 11,
        },
        {
          name: 'code',
          description: 'Paste Lua source directly (if no file)',
          type: 3,
        },
      ],
    },
    {
      name: 'stats',
      description: 'Bot usage statistics',
    },
    {
      name: 'help',
      description: 'How to use the obfuscator bot',
    },
  ];
}

async function registerCommands() {
  const { REST, Routes } = require('discord.js');
  const { assertRuntimeConfig } = require('../config');
  const { logger } = require('../utils/logger');

  assertRuntimeConfig();

  const rest = new REST().setToken(config.token);
  const body = buildCommands();

  if (config.guildId) {
    await rest.put(Routes.applicationGuildCommands(config.clientId, config.guildId), { body });
    logger.info(`Registered ${body.length} guild commands in ${config.guildId}`);
  } else {
    await rest.put(Routes.applicationCommands(config.clientId), { body });
    logger.info(`Registered ${body.length} global commands`);
  }
}

if (require.main === module) {
  registerCommands().catch((err) => {
    console.error('Command registration failed:', err);
    process.exitCode = 1;
  });
}

module.exports = { buildCommands, registerCommands };
