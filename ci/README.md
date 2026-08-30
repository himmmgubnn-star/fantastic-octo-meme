# CI workflow

`ci/ci.yml` is a complete GitHub Actions workflow. It lives here rather than in
`.github/workflows/` because the GitHub App used by this repository's automation
does not have the `workflows` permission, and GitHub rejects any push that
creates or modifies a file under `.github/workflows/` without it.

## Enabling it

If you have push access with the `workflows` scope:

```sh
mkdir -p .github/workflows
git mv ci/ci.yml .github/workflows/ci.yml
git commit -m "ci: enable the Airlock workflow"
git push
```

Alternatively, grant the App **Workflows: read and write** under repository
*Settings → Actions → General → Workflow permissions*, then move the file back.

## Jobs

| Job | What it does | Required? |
|---|---|---|
| `build-and-test` | `make` + `make test` + the compatibility lab + `make fuzz`, on gcc and clang; greps for retired product names in code and tracked paths | yes |
| `sanitizers` | the whole suite under ASan + UBSan with `-fno-sanitize-recover=all` | yes |
| `loader-fuzz` | 40 seeds x 20,000 mutated PEs through the loader under ASan + UBSan | yes |
| `cmake` | configures, builds, and runs `ctest` so the mirror build system cannot rot | yes |
| `android-arm64` | cross-compiles for `arm64-v8a` at API 24 and confirms the artefacts are ARM64. **Compile-only** — it proves nothing about runtime behaviour on a device | yes |
| `format` | `clang-format --dry-run --Werror` | advisory (`continue-on-error: true`) until the tree has been formatted once |

## Status

The YAML parses, and every step that can run in a plain Linux container was run
locally: the make build, the test suite, the sanitizer build, the fuzz campaign,
the brand grep, and the compatibility lab. The GitHub-hosted jobs themselves —
including the CMake build, the NDK cross-compile, and clang-format — have **not**
been executed, because no GitHub Actions run has happened yet. Treat their first
run as unverified.
