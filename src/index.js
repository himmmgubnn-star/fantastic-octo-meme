'use strict';

const { Client, GatewayIntentBits, Events } = require('discord.js');
const { config, assertRuntimeConfig } = require('./config');
const { logger } = require('./utils/logger');
const { registerCommands } = require('./bot/registerCommands');
const commands = require('./bot/commands');

assertRuntimeConfig();

const client = new Client({
  intents: [
    GatewayIntentBits.Guilds,
    GatewayIntentBits.GuildMessages,
    GatewayIntentBits.MessageContent,
  ],
});

client.once(Events.ClientReady, async (ready) => {
  logger.info(`Logged in as ${ready.user.tag}`);
  try {
    await registerCommands();
  } catch (err) {
    logger.error('Slash command registration failed', { error: String(err) });
  }
});

client.on(Events.InteractionCreate, (interaction) => {
  if (interaction.isChatInputCommand()) {
    commands.handleInteraction(interaction).catch((err) => logger.error('Handler error', { error: String(err) }));
  } else if (interaction.isButton()) {
    commands.handleButton(interaction).catch((err) => logger.error('Button error', { error: String(err) }));
  }
});

client.on(Events.MessageCreate, (message) => {
  commands.handleMessage(message).catch((err) => logger.error('Message error', { error: String(err) }));
});

client.on(Events.Error, (err) => logger.error('Client error', { error: String(err) }));

process.on('SIGINT', () => {
  logger.info('Shutting down…');
  commands.stats.flush();
  client.destroy();
  process.exit(0);
});
process.on('SIGTERM', () => {
  commands.stats.flush();
  client.destroy();
  process.exit(0);
});

logger.info('Starting bot…');
client.login(config.token).catch((err) => {
  logger.error('Login failed — check DISCORD_TOKEN / network', { error: err.message });
  process.exit(1);
});
