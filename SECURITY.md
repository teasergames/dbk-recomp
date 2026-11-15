# Security policy

## Reporting a vulnerability

Please report security problems privately, not in a public issue: open the repository's **Security** tab and choose
**Report a vulnerability** (GitHub private vulnerability reporting). Say what is affected, how to reproduce it and
which build you used (`dkb.exe --version`, or the commit).

You should get a first answer within a few days. Once a fix is on `bananza`, the report is published with credit to
you, unless you ask not to be named.

## What is in scope

- The runtime (`runtime/`), the code generator (`recomp/`), the installer (`installer/`) and the tools (`tools/`).
- Release packages built from this repository.

Out of scope: the game itself, and third-party code we only bundle (report those upstream; the list is in
`THIRD_PARTY_NOTICES.md`).

## Supported versions

Fixes go to the `bananza` branch and the next release. Older releases are not patched.

## Please do not send

Game files, keys or dumps derived from the game (shader binaries, GPU fault dumps, minidumps). Describe the problem
and the steps instead; if a file is needed, we will ask for it privately.
