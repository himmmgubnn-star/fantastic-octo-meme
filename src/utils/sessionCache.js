'use strict';

const crypto = require('crypto');

/**
 * Short-lived in-memory cache so button interactions can re-run obfuscation
 * without the user re-uploading source. Entries expire automatically.
 */
class SessionCache {
  /**
   * @param {{ ttlMs?: number, maxEntries?: number }} [options]
   */
  constructor(options = {}) {
    this.ttlMs = options.ttlMs ?? 10 * 60 * 1000;
    this.maxEntries = options.maxEntries ?? 200;
    /** @type {Map<string, { expiresAt: number, value: any }>} */
    this.map = new Map();
  }

  /**
   * @param {any} value
   * @returns {string} session id
   */
  put(value) {
    this.sweep();
    while (this.map.size >= this.maxEntries) {
      const oldest = this.map.keys().next().value;
      if (oldest === undefined) break;
      this.map.delete(oldest);
    }
    const id = crypto.randomBytes(8).toString('hex');
    this.map.set(id, { expiresAt: Date.now() + this.ttlMs, value });
    return id;
  }

  /**
   * @param {string} id
   * @returns {any | null}
   */
  get(id) {
    const entry = this.map.get(id);
    if (!entry) return null;
    if (Date.now() > entry.expiresAt) {
      this.map.delete(id);
      return null;
    }
    return entry.value;
  }

  /** @param {string} id */
  delete(id) {
    this.map.delete(id);
  }

  sweep() {
    const now = Date.now();
    for (const [id, entry] of this.map) {
      if (now > entry.expiresAt) this.map.delete(id);
    }
  }

  get size() {
    return this.map.size;
  }
}

module.exports = { SessionCache };
