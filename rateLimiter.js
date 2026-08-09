'use strict';

/**
 * Sliding-window rate limiter keyed by arbitrary string identifiers.
 * In-memory only — swap for Redis if you run multiple bot processes.
 */
class RateLimiter {
  /**
   * @param {{ windowMs: number, maxRequests: number }} options
   */
  constructor({ windowMs, maxRequests }) {
    if (!Number.isFinite(windowMs) || windowMs <= 0) {
      throw new Error('windowMs must be a positive number');
    }
    if (!Number.isFinite(maxRequests) || maxRequests <= 0) {
      throw new Error('maxRequests must be a positive number');
    }

    this.windowMs = windowMs;
    this.maxRequests = maxRequests;
    /** @type {Map<string, number[]>} */
    this.hits = new Map();
  }

  /**
   * @param {string} key
   * @returns {{ allowed: boolean, remaining: number, retryAfterMs: number }}
   */
  consume(key) {
    const now = Date.now();
    const windowStart = now - this.windowMs;
    const timestamps = (this.hits.get(key) || []).filter((ts) => ts > windowStart);

    if (timestamps.length >= this.maxRequests) {
      const oldest = timestamps[0];
      const retryAfterMs = Math.max(0, oldest + this.windowMs - now);
      this.hits.set(key, timestamps);
      return { allowed: false, remaining: 0, retryAfterMs };
    }

    timestamps.push(now);
    this.hits.set(key, timestamps);
    return {
      allowed: true,
      remaining: this.maxRequests - timestamps.length,
      retryAfterMs: 0,
    };
  }

  /** Drop expired entries to bound memory growth. */
  sweep() {
    const now = Date.now();
    const windowStart = now - this.windowMs;

    for (const [key, timestamps] of this.hits) {
      const alive = timestamps.filter((ts) => ts > windowStart);
      if (alive.length === 0) {
        this.hits.delete(key);
      } else {
        this.hits.set(key, alive);
      }
    }
  }
}

module.exports = { RateLimiter };
