# Cnake

*Cnake* is a simple text-based snake game written in C.

![Cnake screenshot](docs/cnake-screenshot.svg)

## Build, Run, and Install

Requires Make and a C11 compiler on a POSIX-compatible system.

```bash
make
./cnake
```

Optionally provide a board size and a seed:

```bash
./cnake 6 --seed 1729
```

The seed reproduces the same board and subsequent random tile placements when
using the same C library implementation. Without `--seed`, Cnake seeds the game
from the current time as before.

Install to `/usr/local/bin` by default:

```bash
sudo make install
```

Set `PREFIX` to install elsewhere without elevated permissions:

```bash
make install PREFIX="$HOME/.local"
```

Use the same prefix to uninstall:

```bash
make uninstall PREFIX="$HOME/.local"
```

Packagers can stage an installation by setting `DESTDIR`.

Remove the compiled binary with `make clean`.

## License

Copyright 2026 Aaron Dettmann

Licensed under the Apache License, Version 2.0.
See the [LICENSE](LICENSE) file for details.
