'use strict';

require('dotenv').config();

function parseCsv(value) {
  if (!value || typeof value !== 'string') return [];
  return value
    .split(',')
    .map((part) => part.trim())
    .filter(Boolean);
}

function parsePositiveInt(value, fallback) {
  const n = Number.parseInt(value, 10);
  return Number.isFinite(n) && n > 0 ? n : fallback;
}

const config = Object.freeze({
  token: process.env.DISCORD_TOKEN || '',
  clientId: process.env.DISCORD_CLIENT_ID || '',
  guildId: process.env.DISCORD_GUILD_ID || '',
  allowedUserIds: new Set(parseCsv(process.env.ALLOWED_USER_IDS)),
  allowedGuildIds: new Set(parseCsv(process.env.ALLOWED_GUILD_IDS)),
  maxCodeBytes: parsePositiveInt(process.env.MAX_CODE_BYTES, 65_536),
  maxOutputBytes: parsePositiveInt(process.env.MAX_OUTPUT_BYTES, 262_144),
  rateLimitWindowMs: parsePositiveInt(process.env.RATE_LIMIT_WINDOW_MS, 60_000),
  rateLimitMaxRequests: parsePositiveInt(process.env.RATE_LIMIT_MAX_REQUESTS, 5),
  logLevel: (process.env.LOG_LEVEL || 'info').toLowerCase(),
});

function assertRuntimeConfig() {
  const missing = [];
  if (!config.token) missing.push('DISCORD_TOKEN');
  if (!config.clientId) missing.push('DISCORD_CLIENT_ID');
  if (missing.length > 0) {
    throw new Error(`Missing required environment variables: ${missing.join(', ')}`);
  }
}

module.exports = { config, assertRuntimeConfig };
