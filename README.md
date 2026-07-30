# Cnake

*Cnake* is a simple text-based snake game written in C.

![Cnake screenshot](docs/cnake-screenshot.svg)

## Build and Run

Requires GCC and Make.

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

Remove the compiled binary with `make clean`.

## License

Copyright 2026 Aaron Dettmann

Licensed under the Apache License, Version 2.0.
See the [LICENSE](LICENSE) file for details.
