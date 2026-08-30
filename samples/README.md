# Samples

- `hello.exe` — a minimal, valid PE32 executable **synthesized at build time**
  by `tools/gen_sample_pe.c` (`make sample`). It imports `ExitProcess` and
  `GetStdHandle` from `KERNEL32.dll` and exports `FnOne`/`FnTwo`, so it's ideal
  for exercising Cellar's loader on a machine with no MinGW or Windows
  toolchain.

Because it is generated, `hello.exe` is git-ignored. Drop any genuine Windows
`.exe` files here to test against real-world binaries.
