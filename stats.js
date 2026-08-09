'use strict';

const fs = require('fs');
const path = require('path');
const { logger } = require('./logger');

const DEFAULT_PATH = path.join(process.cwd(), 'data', 'stats.json');

/**
 * Lightweight persistent usage stats.
 * Survives restarts; safe for a single bot process.
 */
class StatsStore {
  /** @param {string} [filePath] */
  constructor(filePath = DEFAULT_PATH) {
    this.filePath = filePath;
    /** @type {{
     *   startedAt: string,
     *   totalObfuscations: number,
     *   totalScans: number,
     *   totalBytesIn: number,
     *   totalBytesOut: number,
     *   byLevel: Record<string, number>,
     *   byUser: Record<string, number>,
     *   failures: number,
     *   lastUsedAt: string | null,
     * }} */
    this.data = {
      startedAt: new Date().toISOString(),
      totalObfuscations: 0,
      totalScans: 0,
      totalBytesIn: 0,
      totalBytesOut: 0,
      byLevel: { light: 0, standard: 0, heavy: 0 },
      byUser: {},
      failures: 0,
      lastUsedAt: null,
    };
    this._dirty = false;
    this._flushTimer = null;
    this.load();
  }

  load() {
    try {
      if (!fs.existsSync(this.filePath)) return;
      const raw = fs.readFileSync(this.filePath, 'utf8');
      const parsed = JSON.parse(raw);
      this.data = {
        ...this.data,
        ...parsed,
        byLevel: { ...this.data.byLevel, ...(parsed.byLevel || {}) },
        byUser: { ...(parsed.byUser || {}) },
      };
    } catch (err) {
      logger.warn('Failed to load stats file', {
        error: err instanceof Error ? err.message : String(err),
      });
    }
  }

  persistSoon() {
    this._dirty = true;
    if (this._flushTimer) return;
    this._flushTimer = setTimeout(() => {
      this._flushTimer = null;
      this.flush();
    }, 2_000);
    if (typeof this._flushTimer.unref === 'function') this._flushTimer.unref();
  }

  flush() {
    if (!this._dirty) return;
    try {
      fs.mkdirSync(path.dirname(this.filePath), { recursive: true });
      const tmp = `${this.filePath}.tmp`;
      fs.writeFileSync(tmp, JSON.stringify(this.data, null, 2));
      fs.renameSync(tmp, this.filePath);
      this._dirty = false;
    } catch (err) {
      logger.warn('Failed to persist stats', {
        error: err instanceof Error ? err.message : String(err),
      });
    }
  }

  /**
   * @param {{ userId: string, level: string, bytesIn: number, bytesOut: number, ok: boolean }} entry
   */
  recordObfuscation(entry) {
    if (!entry.ok) {
      this.data.failures += 1;
      this.data.lastUsedAt = new Date().toISOString();
      this.persistSoon();
      return;
    }
    this.data.totalObfuscations += 1;
    this.data.totalBytesIn += entry.bytesIn || 0;
    this.data.totalBytesOut += entry.bytesOut || 0;
    const level = entry.level || 'standard';
    this.data.byLevel[level] = (this.data.byLevel[level] || 0) + 1;
    this.data.byUser[entry.userId] = (this.data.byUser[entry.userId] || 0) + 1;
    this.data.lastUsedAt = new Date().toISOString();
    this.persistSoon();
  }

  recordScan(userId) {
    this.data.totalScans += 1;
    this.data.byUser[userId] = (this.data.byUser[userId] || 0) + 1;
    this.data.lastUsedAt = new Date().toISOString();
    this.persistSoon();
  }

  snapshot() {
    const topUsers = Object.entries(this.data.byUser)
      .sort((a, b) => b[1] - a[1])
      .slice(0, 5)
      .map(([id, count]) => ({ id, count }));

    return {
      ...this.data,
      uniqueUsers: Object.keys(this.data.byUser).length,
      topUsers,
      avgExpansion:
        this.data.totalBytesIn > 0
          ? Number((this.data.totalBytesOut / this.data.totalBytesIn).toFixed(2))
          : 0,
    };
  }
}

module.exports = { StatsStore };
