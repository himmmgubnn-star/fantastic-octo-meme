'use strict';

const crypto = require('crypto');

/**
 * Deterministic PRNG (xorshift32) seeded from crypto so runs are unique
 * but reproducible when a seed is supplied (useful for tests).
 */
class Random {
  /** @param {number | string | undefined} seed */
  constructor(seed) {
    let s;
    if (typeof seed === 'number' && Number.isFinite(seed)) {
      s = seed >>> 0;
    } else if (typeof seed === 'string' && seed.length > 0) {
      const hash = crypto.createHash('sha256').update(seed).digest();
      s = hash.readUInt32BE(0);
    } else {
      s = crypto.randomBytes(4).readUInt32BE(0);
    }
    // xorshift32 rejects 0
    this.state = s === 0 ? 0xdeadbeef : s;
  }

  /** @returns {number} uint32 */
  nextU32() {
    let x = this.state;
    x ^= x << 13;
    x ^= x >>> 17;
    x ^= x << 5;
    this.state = x >>> 0;
    return this.state;
  }

  /** @returns {number} [0, 1) */
  next() {
    return this.nextU32() / 0x100000000;
  }

  /**
   * Inclusive integer range.
   * @param {number} min
   * @param {number} max
   */
  int(min, max) {
    if (max < min) [min, max] = [max, min];
    const span = max - min + 1;
    return min + (this.nextU32() % span);
  }

  /**
   * @template T
   * @param {T[]} items
   * @returns {T}
   */
  pick(items) {
    if (!items.length) throw new Error('Cannot pick from empty array');
    return items[this.int(0, items.length - 1)];
  }

  /**
   * Generate a short opaque identifier.
   * @param {number} length
   */
  ident(length = 8) {
    const alphabet = 'O0Il1ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
    let out = '_';
    // first char after underscore: letter only
    const letters = 'OIlABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz';
    out += letters[this.int(0, letters.length - 1)];
    for (let i = 1; i < length; i += 1) {
      out += alphabet[this.int(0, alphabet.length - 1)];
    }
    return out;
  }

  /** Hex-like garbage name used for string table keys etc. */
  hexName(bytes = 4) {
    let out = '_0x';
    for (let i = 0; i < bytes; i += 1) {
      out += this.nextU32().toString(16).padStart(8, '0').slice(0, 2);
    }
    return out;
  }
}

module.exports = { Random };
