'use strict';

const { config } = require('../config');

const LEVELS = Object.freeze({
  error: 0,
  warn: 1,
  info: 2,
  debug: 3,
});

const activeLevel = LEVELS[config.logLevel] ?? LEVELS.info;

function formatMessage(level, message, meta) {
  const entry = {
    ts: new Date().toISOString(),
    level,
    message,
  };

  if (meta !== undefined) {
    entry.meta = meta;
  }

  return JSON.stringify(entry);
}

function log(level, message, meta) {
  if ((LEVELS[level] ?? 99) > activeLevel) return;

  const line = formatMessage(level, message, meta);
  if (level === 'error') {
    console.error(line);
  } else if (level === 'warn') {
    console.warn(line);
  } else {
    console.log(line);
  }
}

const logger = {
  error(message, meta) {
    log('error', message, meta);
  },
  warn(message, meta) {
    log('warn', message, meta);
  },
  info(message, meta) {
    log('info', message, meta);
  },
  debug(message, meta) {
    log('debug', message, meta);
  },
};

module.exports = { logger };
