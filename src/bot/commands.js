'use strict';

const {
  AttachmentBuilder,
  ActionRowBuilder,
  ButtonBuilder,
  ButtonStyle,
} = require('discord.js');
const { config } = require('../config');
const { obfuscate, ObfuscationError, scanSource, analyzeSource } = require('../obfuscator/index');
const { RateLimiter } = require('../utils/rateLimiter');
const { SessionCache } = require('../utils/sessionCache');
const { StatsStore } = require('../utils/stats');
const { extractCodeFromMessage, normalizeAttachmentCode } = require('../utils/codeExtraction');
const { logger } = require('../utils/logger');
const embeds = require('./embeds');

const rateLimiter = new RateLimiter({
  windowMs: config.rateLimitWindowMs,
  maxRequests: config.rateLimitMaxRequests,
});
const sessionCache = new SessionCache({ ttlMs: 10 * 60 * 1000 });
const stats = new StatsStore();
const startedAt = Date.now();

setInterval(() => rateLimiter.sweep(), 5 * 60 * 1000).unref();
setInterval(() => stats.flush(), 60 * 1000).unref();

// ---------------------------------------------------------------------------
// Guards
// ---------------------------------------------------------------------------

/**
 * @param {import('discord.js').Interaction | import('discord.js').Message} ctx
 * @returns {string | null} rejection reason
 */
function checkAccess(ctx) {
  if (config.allowedUserIds.size > 0 && !config.allowedUserIds.has(ctx.user?.id ?? ctx.author?.id)) {
    return 'You are not allowed to use this bot.';
  }
  const guildId = ctx.guildId;
  if (guildId && config.allowedGuildIds.size > 0 && !config.allowedGuildIds.has(guildId)) {
    return 'This bot is not enabled on this server.';
  }
  return null;
}

function checkRateLimit(userId) {
  const { allowed, retryAfterMs } = rateLimiter.consume(userId);
  if (!allowed) {
    const seconds = Math.ceil(retryAfterMs / 1000);
    return `Rate limited. Try again in ${seconds}s.`;
  }
  return null;
}

// ---------------------------------------------------------------------------
// Source resolution
// ---------------------------------------------------------------------------

const ATTACHMENT_EXTS = ['.lua', '.luau', '.txt'];

async function resolveSource(interactionOrMessage, options = {}) {
  const isInteraction = typeof options.getCode !== 'function';
  let code = '';
  let sourceLabel = 'pasted code';

  if (isInteraction) {
    const fileOption = options.file ?? null;
    if (fileOption) {
      const url = fileOption.url;
      const name = fileOption.name || 'source.lua';
      const extOk = ATTACHMENT_EXTS.some((ext) => name.toLowerCase().endsWith(ext));
      if (!extOk) throw new ObfuscationError(`Unsupported file type \`${name}\`. Use ${ATTACHMENT_EXTS.join(', ')}.`, 'BAD_ATTACHMENT');
      const res = await fetch(url);
      if (!res.ok) throw new ObfuscationError('Failed to download the attached file.', 'DOWNLOAD_FAILED');
      const raw = await res.text();
      if (Buffer.byteLength(raw, 'utf8') > config.maxCodeBytes) {
        throw new ObfuscationError(
          `Source exceeds the ${config.maxCodeBytes} byte limit.`,
          'SOURCE_TOO_LARGE',
        );
      }
      code = normalizeAttachmentCode(raw);
      sourceLabel = name;
    }

    const pasted = options.code;
    if ((!code || !code.trim()) && typeof pasted === 'string' && pasted.trim()) {
      if (Buffer.byteLength(pasted, 'utf8') > config.maxCodeBytes) {
        throw new ObfuscationError(
          `Source exceeds the ${config.maxCodeBytes} byte limit.`,
          'SOURCE_TOO_LARGE',
        );
      }
      code = normalizeAttachmentCode(pasted);
      sourceLabel = 'pasted code';
    }
  }

  if (!code.trim()) {
    throw new ObfuscationError(
      'No source provided. Attach a `.lua`/`.luau`/`.txt` file or paste code.',
      'NO_SOURCE',
    );
  }

  return { code, sourceLabel };
}

async function replyPayload(interaction, { embed, files, ephemeral }) {
  const payload = { embeds: [embed], files, ephemeral };
  if (interaction.deferred || interaction.replied) {
    await interaction.editReply(payload).catch(() => {});
  } else {
    await interaction.reply(payload).catch(() => {});
  }
}

/** Build a rerun-button row bound to cached source. */
function buildRerunRow(source) {
  const sessionId = sessionCache.put({ source });
  const row = new ActionRowBuilder().addComponents(
    ['light', 'standard', 'heavy'].map((level) =>
      new ButtonBuilder()
        .setCustomId(`rerun:${sessionId}:${level}`)
        .setLabel(level[0].toUpperCase() + level.slice(1))
        .setStyle(ButtonStyle.Secondary),
    ),
  );
  return row;
}

/** Deliver obfuscated code as inline block or attachment. */
function obfuscatedDeliverables(result) {
  const { code, meta } = result;
  if (code.length <= 1800) {
    return { files: [], content: `\`\`\`lua\n${code}\n\`\`\`` };
  }
  const filename = `obfuscated_${meta.fingerprint}.lua`;
  return { files: [new AttachmentBuilder(Buffer.from(code, 'utf8'), { name: filename })], content: null };
}

function runObfuscateForLevel(source, level, watermark) {
  return obfuscate(source, { level, watermark: watermark || undefined, maxOutputBytes: config.maxOutputBytes });
}

// ---------------------------------------------------------------------------
// Slash handlers
// ---------------------------------------------------------------------------

async function handleObfuscate(interaction) {
  const opts = interaction.options;
  await interaction.deferReply({ ephemeral: opts.getBoolean('ephemeral') ?? true });

  const { code, sourceLabel } = await resolveSource(interaction, {
    file: opts.getAttachment('file'),
    code: opts.getString('code'),
  });
  const level = opts.getString('level') || 'standard';
  const watermark = opts.getString('watermark');

  const result = runObfuscateForLevel(code, level, watermark);
  stats.recordObfuscation({
    userId: interaction.user.id,
    level,
    bytesIn: result.meta.inputBytes,
    bytesOut: result.meta.outputBytes,
    ok: true,
  });

  const built = embeds.obfuscateEmbed(result, { level, watermark });
  const deliver = obfuscatedDeliverables(result);

  await replyPayload(interaction, {
    embed: built.embed,
    files: deliver.files,
    ephemeral: opts.getBoolean('ephemeral') ?? true,
  }).then(async (msg) => {
    // Content can't be part of defer/editReply with embed cleanly — send as followUp when needed
    if (deliver.content && !interaction.replied) {
      await interaction.followUp({ content: deliver.content, ephemeral: opts.getBoolean('ephemeral') ?? true }).catch(() => {});
    }
    void msg;
    void sourceLabel;
  });
}

async function handleScan(interaction) {
  await interaction.deferReply({ ephemeral: true });
  const { code } = await resolveSource(interaction, {
    file: interaction.options.getAttachment('file'),
    code: interaction.options.getString('code'),
  });
  const scan = scanSource(code);
  stats.recordScan(interaction.user.id);
  await replyPayload(interaction, { embed: embeds.scanEmbed(scan), files: [], ephemeral: true });
}

async function handleAnalyze(interaction) {
  await interaction.deferReply({ ephemeral: true });
  const { code } = await resolveSource(interaction, {
    file: interaction.options.getAttachment('file'),
    code: interaction.options.getString('code'),
  });
  const report = analyzeSource(code);
  await replyPayload(interaction, { embed: embeds.analyzeEmbed(report), files: [], ephemeral: true });
}

async function handleCompare(interaction) {
  await interaction.deferReply({ ephemeral: true });
  const { code } = await resolveSource(interaction, {
    file: interaction.options.getAttachment('file'),
    code: interaction.options.getString('code'),
  });

  const results = ['light', 'standard', 'heavy'].map((level) => {
    try {
      return { level, ok: true, result: runObfuscateForLevel(code, level) };
    } catch (err) {
      return { level, ok: false, error: err instanceof Error ? err.message : String(err) };
    }
  });

  const best = results.find((r) => r.ok && r.result.meta.outputBytes <= config.maxOutputBytes);
  const row = best ? buildRerunRow(code) : null;

  await interaction.editReply({
    embeds: [embeds.compareEmbed(results)],
    ...(row ? { components: [row] } : {}),
    files: [],
  });
}

async function handleStats(interaction) {
  await interaction.deferReply({ ephemeral: true });
  stats.flush();
  await replyPayload(interaction, {
    embed: embeds.statsEmbed(stats.snapshot(), Date.now() - startedAt),
    files: [],
    ephemeral: true,
  });
}

async function handleHelp(interaction) {
  await replyPayload(interaction, { embed: embeds.helpEmbed(), files: [], ephemeral: true });
}

/** @param {import('discord.js').ChatInputCommandInteraction} interaction */
async function handleInteraction(interaction) {
  try {
    const denied = checkAccess(interaction);
    if (denied) {
      await interaction.reply({ content: denied, ephemeral: true }).catch(() => {});
      return;
    }
    const limited = checkRateLimit(interaction.user.id);
    if (limited && interaction.commandName !== 'help' && interaction.commandName !== 'stats') {
      await interaction.reply({ content: limited, ephemeral: true }).catch(() => {});
      return;
    }

    switch (interaction.commandName) {
      case 'obfuscate':
        return await handleObfuscate(interaction);
      case 'scan':
        return await handleScan(interaction);
      case 'analyze':
        return await handleAnalyze(interaction);
      case 'compare':
        return await handleCompare(interaction);
      case 'stats':
        return await handleStats(interaction);
      case 'help':
        return await handleHelp(interaction);
      default:
        return;
    }
  } catch (err) {
    stats.recordObfuscation({ userId: interaction.user?.id ?? '?', level: '?', ok: false });
    logger.error('Interaction failed', {
      command: interaction.commandName,
      error: err instanceof Error ? err.message : String(err),
    });
    const embed =
      err instanceof ObfuscationError
        ? embeds.errorEmbed(err.message)
        : embeds.errorEmbed('Unexpected internal error.', 'Try again; if it persists, reduce input size.');
    if (interaction.deferred || interaction.replied) {
      await interaction.editReply({ embeds: [embed], files: [] }).catch(() => {});
    } else {
      await interaction.reply({ embeds: [embed], ephemeral: true }).catch(() => {});
    }
  }
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

/** @param {import('discord.js').ButtonInteraction} interaction */
async function handleButton(interaction) {
  const [, sessionId, level] = interaction.customId.split(':');
  const session = sessionCache.get(sessionId);
  if (!session) {
    await interaction.reply({
      embeds: [embeds.errorEmbed('This session expired (10 min). Re-run `/obfuscate`.')],
      ephemeral: true,
    });
    return;
  }
  await interaction.deferUpdate().catch(() => {});

  try {
    const result = runObfuscateForLevel(session.source, level);
    stats.recordObfuscation({
      userId: interaction.user.id,
      level,
      bytesIn: result.meta.inputBytes,
      bytesOut: result.meta.outputBytes,
      ok: true,
    });
    const built = embeds.obfuscateEmbed(result, { level, watermark: result.meta.watermark });
    const deliver = obfuscatedDeliverables(result);
    await interaction.followUp({
      embeds: [built.embed],
      files: deliver.files,
      content: deliver.content ?? undefined,
      ephemeral: true,
    });
  } catch (err) {
    logger.warn('Button rerun failed', { level, error: String(err) });
    await interaction.followUp({
      embeds: [embeds.errorEmbed(err instanceof Error ? err.message : String(err))],
      ephemeral: true,
    });
  }
}

// ---------------------------------------------------------------------------
// Prefix commands
// ---------------------------------------------------------------------------

/** @param {import('discord.js').Message} message */
async function handleMessage(message) {
  if (message.author.bot) return;
  const isCommand = /^!obfuscate\b/i.test(message.content);
  const isReplyCommand =
    /^!obfuscate/i.test(message.content) &&
    message.reference &&
    message.type === 19; // REPLY

  if (!isCommand) return;

  const denied = checkAccess(message);
  if (denied) {
    await message.reply(denied).catch(() => {});
    return;
  }
  const limited = checkRateLimit(message.author.id);
  if (limited) {
    await message.reply(limited).catch(() => {});
    return;
  }

  const flags = message.content.replace(/^!obfuscate/i, '').trim();
  const wmMatch = flags.match(/wm:(\S+)/i);
  const watermark = wmMatch ? wmMatch[1] : null;
  const levelMatch = flags.match(/\b(light|standard|heavy)\b/i);
  const level = levelMatch ? levelMatch[1].toLowerCase() : 'standard';

  let source = '';

  // Reply-to-code takes precedence
  if (isReplyCommand) {
    try {
      const ref = await message.fetchReference();
      if (ref.attachments.size > 0) {
        const att = ref.attachments.first();
        const res = await fetch(att.url);
        source = normalizeAttachmentCode(await res.text());
      } else {
        source = extractCodeFromMessage(ref.content);
      }
    } catch {
      source = '';
    }
  }

  if (!source.trim() && message.attachments.size > 0) {
    const att = message.attachments.find((a) =>
      ATTACHMENT_EXTS.some((ext) => a.name.toLowerCase().endsWith(ext)),
    ) || message.attachments.first();
    const res = await fetch(att.url);
    source = normalizeAttachmentCode(await res.text());
  }

  if (!source.trim()) {
    source = extractCodeFromMessage(message.content);
  }

  if (!source.trim()) {
    await message.reply(
      'No code found. Use a ```lua fence, attach a `.lua` file, or reply to a code message.',
    ).catch(() => {});
    return;
  }

  try {
    const result = runObfuscateForLevel(source, level, watermark);
    stats.recordObfuscation({
      userId: message.author.id,
      level,
      bytesIn: result.meta.inputBytes,
      bytesOut: result.meta.outputBytes,
      ok: true,
    });
    const built = embeds.obfuscateEmbed(result, { level, watermark: result.meta.watermark });
    const deliver = obfuscatedDeliverables(result);
    const row = buildRerunRow(source);
    await message.reply({
      embeds: [built.embed],
      components: [row],
      files: deliver.files,
      ...(deliver.content ? { content: deliver.content.slice(0, 2000) } : {}),
    });
  } catch (err) {
    stats.recordObfuscation({ userId: message.author.id, level, ok: false });
    logger.warn('Prefix command failed', { error: String(err) });
    await message.reply({
      embeds: [embeds.errorEmbed(err instanceof Error ? err.message : String(err))],
    }).catch(() => {});
  }
}

module.exports = {
  handleInteraction,
  handleButton,
  handleMessage,
  stats,
};
