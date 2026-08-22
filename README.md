# Discord Lua/Luau Obfuscator Bot · v1.1

Production Discord bot that obfuscates **Lua / Luau** for **Roblox ModuleScript** protection — plus security scanning, analysis, level comparison, watermarks, and rich interactive replies.

> **Important:** Obfuscation raises the cost of casual cracking. It is **not** cryptographic security. Never put secrets in client or shared modules.

## What’s new in 1.2

| Feature | Command / behavior |
|---|---|
| **New level** | `/obfuscate level:maximum` — everything on, maximum densities |
| Boolean obfuscation | `true`/`false` → opaque equivalent expressions |
| Dead-code injection | Unreachable guarded blocks with junk statement bodies |
| Closure wrapping | Whole chunk wrapped in an IIFE vararg closure (`maximum`) |
| More number forms | Multiplicative-inverse and `math.floor`/modulo encodings added |
| Deterministic builds | `/obfuscate seed:` option — same seed + source = identical output |

## What was new in 1.1

| Feature | Command / behavior |
|---|---|
| Security scanner | `/scan` — secrets, webhooks, `loadstring`, exploit APIs, grade A–F |
| Source analyzer | `/analyze` — metrics, Roblox APIs, recommendations |
| Level compare | `/compare` — light + standard + heavy + maximum in one shot |
| Usage stats | `/stats` — totals, expansion ratio, uptime |
| Help embed | `/help` |
| Watermarks | `watermark:` option / `wm:Name` prefix flag |
| Re-run buttons | Light / Standard / Heavy on every result (10 min session) |
| Reply-to-code | `!obfuscate` as a **reply** to a code message |
| Rich embeds | Size, passes, fingerprint, duration, security hints |
| Stronger passes | String split, junk locals, function proxies, anti-tamper, VM prologue |
| Persistent stats | `data/stats.json` |

## Features (full)

### Obfuscation pipeline

1. VM-lite prologue (`heavy`+)
2. Control-flow flattening (`heavy`+)
3. Opaque predicates
4. Junk locals
5. Dead-code injection
6. Function proxy wrappers
7. Local / parameter renaming (Roblox globals preserved)
8. Opaque numbers (add/sub, bxor, quotient, multiplicative, floor/modulo forms)
9. Boolean opacity (`true`/`false` → constant expressions)
10. String splitting
11. XOR string table encryption
12. Minify / strip comments
13. Closure wrapping (`maximum`)
14. Bootstrap + optional anti-tamper + watermark

### Discord UX

- `/obfuscate` · `/scan` · `/analyze` · `/compare` · `/stats` · `/help`
- Prefix `!obfuscate [light|standard|heavy] [wm:Tag]`
- File attachments (`.lua`, `.luau`, `.txt`) or pasted source
- Ephemeral replies by default
- Per-user rate limits + byte caps + optional allowlists

## Quick start

```bash
cd discord-lua-obfuscator
cp .env.example .env   # set DISCORD_TOKEN + DISCORD_CLIENT_ID
npm install
npm run register-commands   # REQUIRED after upgrading to 1.1 (new slash cmds)
npm start
```

**Enable Message Content Intent** in the Discord Developer Portal (for prefix commands).

## Usage

```
/obfuscate file:Module.lua level:maximum watermark:MyGame seed:build-42 ephemeral:true
/scan file:Module.lua
/analyze code:local x=1 return x
/compare file:Module.lua
/stats
/help
```

Prefix:

````
!obfuscate heavy wm:MyGame
```lua
local x = 1
return x
```
````

Or reply to any message that has code/file with:

```
!obfuscate standard
```

## Levels

| Level | Extra beyond rename+minify |
|---|---|
| `light` | bootstrap |
| `standard` | strings, split, numbers, booleans, junk, dead code, proxies, predicates, anti-tamper |
| `heavy` | + control-flow + VM prologue |
| `maximum` | + closure wrapping, highest pass densities |

## Architecture

```
src/
  index.js
  config.js
  bot/
    commands.js          # all slash + prefix + button handlers
    embeds.js            # rich embed builders
    registerCommands.js
  obfuscator/
    index.js             # obfuscate() pipeline
    passes.js            # all transform passes
    tokenizer.js
    keywords.js
    random.js
    securityScan.js      # /scan engine
    analyze.js           # /analyze engine
  utils/
    logger.js
    rateLimiter.js
    codeExtraction.js
    sessionCache.js      # button re-run sessions
    stats.js             # persistent counters
```

## Environment

| Variable | Required | Default |
|---|---|---|
| `DISCORD_TOKEN` | yes | — |
| `DISCORD_CLIENT_ID` | yes | — |
| `DISCORD_GUILD_ID` | no | — (faster command register) |
| `ALLOWED_USER_IDS` | no | all users |
| `ALLOWED_GUILD_IDS` | no | all guilds |
| `MAX_CODE_BYTES` | no | `65536` |
| `MAX_OUTPUT_BYTES` | no | `262144` |
| `RATE_LIMIT_WINDOW_MS` | no | `60000` |
| `RATE_LIMIT_MAX_REQUESTS` | no | `5` |
| `LOG_LEVEL` | no | `info` |

## Tests

```bash
npm test
```

## Security notes

1. Obfuscation ≠ server authority. Economy / anti-cheat stay server-side.
2. `/scan` is a helper, not a guarantee — review findings yourself.
3. Watermarks are plaintext headers for ownership tagging, not crypto.
4. Button sessions hold source in memory for 10 minutes; single-process only.
5. Never commit `.env` or `data/stats.json` if sensitive.

## License

MIT
