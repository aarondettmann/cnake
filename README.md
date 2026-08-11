# Cnake

*Cnake* is a simple, text-based snake game for the terminal, written in C.

| Unicode (default) | ASCII (`--ascii`) |
| :---: | :---: |
| ![Cnake Unicode interface](docs/cnake-screenshot.svg) | ![Cnake ASCII interface](docs/cnake-ascii-screenshot.svg) |


**Rules**

The game is won when the entire available board space is filled. Eating snacks
increases the snake's length, while poison shortens it. Colliding with
obstacles or consuming poison at the minimum length is fatal.

**Controls**

```
+---------- CONTROLS ----------+
|           k ^                |
|       h <     > l            |
|           j v                |
+------------------------------+
| ? Show help                  |
| v Toggle debug view          |
| c Add five segments          |
| q Quit                       |
+------------------------------+
```

## Build, Run, and Install

Requires `Make` and a C11 compiler on a POSIX-compatible system.

```bash
make
./cnake
```
Display all options with:

```bash
./cnake --help
```

This prints:

```
Usage: ./cnake [board-size] [options]

Play Cnake with a board size from 4 to 10 (default 5).

Options:
  --help        Display this help and exit
  --version     Display the version and exit
  --ascii       Use pure ASCII characters for all terminal output
  --monochrome  Disable ANSI text styling and use default terminal colors
  --seed UINT   Reproduce a game with an unsigned integer seed
```

Options and the board size can appear in any order.

The seed reproduces the same board and subsequent random tile placements when
using the same C library implementation. A seeded game replays the same board on
every retry; without `--seed`, each retry starts from a new board.

---

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

Remove the compiled binary and `build/` artifacts with `make clean`.

## License

Copyright 2026 Aaron Dettmann

Licensed under the Apache License, Version 2.0. \
See the [LICENSE](LICENSE) file for details.
