'use strict';

const { EmbedBuilder } = require('discord.js');

const COLORS = Object.freeze({
  primary: 0x5865f2,
  success: 0x57f287,
  warning: 0xfee75c,
  danger: 0xed4245,
  info: 0x00b0f4,
});

const SEVERITY_COLORS = Object.freeze({
  critical: '🔴',
  high: '🟠',
  medium: '🟡',
  low: '🔵',
  info: '⚪',
});

function baseEmbed(title, color = COLORS.primary) {
  return new EmbedBuilder().setColor(color).setTitle(title).setTimestamp();
}

function truncate(text, max = 1024) {
  if (text.length <= max) return text;
  return `${text.slice(0, max - 1)}…`;
}

function obfuscateEmbed(result, { level, watermark }) {
  const { code, meta } = result;
  const embed = baseEmbed(`Obfuscated · ${level}`, meta.securityHints > 0 ? COLORS.warning : COLORS.success)
    .addFields(
      {
        name: 'Size',
        value: `${meta.inputBytes} B → ${meta.outputBytes} B (×${meta.expansion})`,
        inline: true,
      },
      { name: 'Duration', value: `${meta.durationMs} ms`, inline: true },
      { name: 'Renames', value: String(meta.renameCount), inline: true },
      { name: 'Passes', value: truncate(meta.passes.join(', '), 1024) },
      { name: 'Fingerprint', value: `\`${meta.fingerprint}\``, inline: true },
    );

  if (watermark) embed.addFields({ name: 'Watermark', value: watermark, inline: true });
  if (meta.securityHints > 0) {
    embed.addFields({
      name: 'Security hints',
      value: `${meta.securityHints} high/critical finding(s) in the source — run \`/scan\` for details.`,
    });
  }

  return { embed, code };
}

function scanEmbed(scan) {
  const embed = baseEmbed(
    `Security scan · Grade ${scan.metrics.grade}`,
    scan.metrics.grade === 'A' ? COLORS.success : scan.metrics.grade === 'F' ? COLORS.danger : COLORS.warning,
  ).addFields(
    {
      name: 'Metrics',
      value: `${scan.metrics.lines} lines · ${scan.metrics.bytes} B · ${scan.metrics.functions} fn · ${scan.metrics.strings} str`,
    },
    { name: 'Summary', value: truncate(scan.summary, 1024) },
  );

  if (scan.findings.length > 0) {
    const lines = scan.findings
      .slice(0, 10)
      .map(
        (f) =>
          `${SEVERITY_COLORS[f.severity] || '⚪'} **${f.title}**${f.line ? ` (line ${f.line})` : ''}\n${truncate(f.detail, 150)}`,
      );
    embed.addFields({ name: `Findings (${scan.findings.length})`, value: truncate(lines.join('\n'), 1024) });
    if (scan.findings.length > 10) {
      embed.setFooter({ text: `+${scan.findings.length - 10} more findings not shown` });
    }
  }

  return embed;
}

function analyzeEmbed(report) {
  return baseEmbed('Source analysis', COLORS.info)
    .addFields(
      {
        name: 'Overview',
        value: `${report.lines} lines · ${report.bytes} B · complexity **${report.complexity}**`,
      },
      {
        name: 'Tokens',
        value:
          `${report.tokens} significant · ${report.identifiers} idents (${report.uniqueIdentifiers} unique)\n` +
          `${report.functions} functions · ${report.localsDeclared} locals\n` +
          `${report.strings} strings (${report.stringBytes} B) · ${report.numbers} numbers · ${report.comments} comments (${report.commentBytes} B)`,
      },
      {
        name: 'Roblox APIs',
        value: report.robloxApisUsed.length ? truncate(report.robloxApisUsed.join(', '), 1024) : 'none detected',
      },
      {
        name: 'Top identifiers',
        value: report.topIdentifiers.length ? truncate(report.topIdentifiers.join(', '), 1024) : '—',
      },
      { name: 'Recommendations', value: truncate(report.recommendations.map((r) => `• ${r}`).join('\n'), 1024) },
    );
}

function compareEmbed(results) {
  const rows = results.map((r) => {
    if (!r.ok) return `**${r.level}** — failed: \`${r.error}\``;
    return `**${r.level}** — ${r.result.meta.outputBytes} B (×${r.result.meta.expansion}) · ${r.result.meta.passes.length} passes · \`${r.result.meta.fingerprint}\``;
  });
  return baseEmbed('Level comparison', COLORS.info).addFields({ name: 'Results', value: truncate(rows.join('\n'), 4096) });
}

function statsEmbed(snapshot, uptimeMs) {
  const topUsers = snapshot.topUsers.map((u) => `<@${u.id}> — ${u.count}`).join('\n') || '—';
  return baseEmbed('Bot statistics', COLORS.info)
    .addFields(
      { name: 'Obfuscations', value: String(snapshot.totalObfuscations), inline: true },
      { name: 'Scans', value: String(snapshot.totalScans), inline: true },
      { name: 'Failures', value: String(snapshot.failures), inline: true },
      { name: 'Bytes processed', value: `${snapshot.totalBytesIn} → ${snapshot.totalBytesOut} (avg ×${snapshot.avgExpansion})`, inline: false },
      { name: 'Unique users', value: String(snapshot.uniqueUsers), inline: true },
      { name: 'Uptime', value: formatDuration(uptimeMs), inline: true },
      { name: 'Top users', value: truncate(topUsers, 1024) },
    );
}

function helpEmbed() {
  return baseEmbed('Lua/Luau Obfuscator Bot', COLORS.primary)
    .setDescription(
      'Protects Roblox ModuleScripts with multi-pass obfuscation. ' +
        '**Obfuscation raises the cost of casual cracking but is not cryptographic security.** Keep secrets and privileged logic on the server.',
    )
    .addFields(
      {
        name: 'Slash commands',
        value:
          '`/obfuscate file|code [level] [watermark] [ephemeral]`\n' +
          '`/scan file|code` — secrets & risky APIs, grade A–F\n' +
          '`/analyze file|code` — metrics & recommendations\n' +
          '`/compare file|code` — all levels side by side\n' +
          '`/stats` · `/help`',
      },
      {
        name: 'Prefix command',
        value: '`!obfuscate [light|standard|heavy] [wm:Tag]` with a ```lua fence or as a reply to a code message',
      },
      {
        name: 'Levels',
        value:
          '**light** — rename + minify + bootstrap\n' +
          '**standard** — + string encryption/splitting, opaque numbers/booleans/predicates, dead code, junk, proxies, anti-tamper\n' +
          '**heavy** — + control-flow flattening, VM prologue\n' +
          '**maximum** — + closure wrapping, maximum pass densities',
      },
    );
}

function errorEmbed(message, hint) {
  const embed = baseEmbed('Something went wrong', COLORS.danger).setDescription(truncate(message, 4096));
  if (hint) embed.addFields({ name: 'Hint', value: hint });
  return embed;
}

function formatDuration(ms) {
  const s = Math.floor(ms / 1000);
  const d = Math.floor(s / 86400);
  const h = Math.floor((s % 86400) / 3600);
  const m = Math.floor((s % 3600) / 60);
  if (d > 0) return `${d}d ${h}h`;
  if (h > 0) return `${h}h ${m}m`;
  return `${m}m ${s % 60}s`;
}

module.exports = {
  COLORS,
  obfuscateEmbed,
  scanEmbed,
  analyzeEmbed,
  compareEmbed,
  statsEmbed,
  helpEmbed,
  errorEmbed,
  formatDuration,
};
