# Build shooter_hashcat on Linux

These instructions build the current Shooter source natively on 64-bit Linux.
They were verified on Ubuntu 24.04 with Clang 18 and stable Rust.

## Ubuntu prerequisites

Install the C/C++ compiler, build tools, OpenSSL headers, and Python headers:

```bash
sudo apt update
sudo apt install --no-install-recommends \
  build-essential clang curl git libssl-dev make pkg-config python3-dev
```

Shooter includes Rust bridges and feeds that use Rust edition 2024. Install a
current stable toolchain with `rustup`; an older distribution-provided Cargo
may be unable to read their manifests:

```bash
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- \
  -y --profile minimal --default-toolchain stable
source "$HOME/.cargo/env"
```

GPU drivers and their CUDA, HIP, or OpenCL runtime are needed to run attacks,
but they are not needed merely to compile the program.

## Clone and build

Clone this repository, not the original Hashcat repository:

```bash
git clone https://github.com/Shooter3k/shooter_hashcat.git
cd shooter_hashcat
make -j"$(nproc)" ENABLE_LTO=0 CC=clang CXX=clang++
```

The main executable is `hashcat` and the operations companion is `shooterctl`
in the repository root. Confirm both start:

```bash
./hashcat --version
./shooterctl --version
./shooterctl doctor
```

Warnings about deprecated OpenSSL functions do not make the build fail. If
Python development headers are omitted, Hashcat can also warn that Python
plugin modes 72000 or 73000 were skipped.

## Shared-library build

To build Hashcat with `libhashcat.so`, clean the previous objects and set
`SHARED=1`:

```bash
make clean
SHARED=1 make -j"$(nproc)" ENABLE_LTO=0 CC=clang CXX=clang++
```

Shooter compiles every bundled mdxfind static library with `-fPIC`, so both
normal and shared Linux builds can link `bridges/bridge_mdxfind.so`.

## Install

Installation is optional:

```bash
sudo make install
```

Without custom XDG variables, session files are stored under
`$HOME/.local/share/hashcat/sessions`, cached kernels under
`$HOME/.cache/hashcat`, and the potfile under `$HOME/.local/share/hashcat`.

## Other targets

- Windows: follow [how_to_compile.txt](how_to_compile.txt) or use the complete
  Windows release archive.
- Windows from WSL: see [BUILD_WSL.md](BUILD_WSL.md).
- Android: see [BUILD_Android.md](BUILD_Android.md).
- Docker: see [BUILD_Docker.md](BUILD_Docker.md).

Shooter currently validates native Linux and Windows targets in CI. macOS and
BSD are not currently claimed as supported targets for Shooter's additional
bridges and feeds.

## Support diagnostics

`shooterctl support-bundle support-check` creates a manually requested,
privacy-limited installation report. This complements the automatic error log
described below. See [docs/shooterctl.md](docs/shooterctl.md).

## Shared core and package check

The default Linux build places the versioned shared core beside the executable.
Use `make SHARED=0` for the legacy arrangement where each plugin contains its
own core copy. After either build, verify the assembled directory without
requiring a compute device:

```bash
bash tools/test_package.sh . --no-device
```

## Reporting runtime errors

When a normal Hashcat error occurs, Shooter creates one file named like
`shooter_hashcat-error-YYYYMMDD-HHMMSS-PID.log` in the directory where it was
started and prints the exact path. Review the file before sharing it because
arguments, paths, or an individual malformed input line may be private.
`--brain-password` values are redacted automatically. See the
[automatic error-report guide](docs/error-reports.md) for the recorded fields
and the small number of failures that can prevent a report from being written.
