# Compiling hashcat with MSYS2

Tested on Windows 11 23H2 x64

## Prerequisites

1. **Install MSYS2** from the [official website](https://www.msys2.org/) and follow steps 1–7.
2. **Open the MSYS2 MINGW64 terminal**
   Do *not* use the plain MSYS or UCRT terminals.
3. **Update the base system:**

    ```sh
    pacman -Syu
    ```

    If prompted to close the terminal after core updates, do so, then reopen **MINGW64** and run:

    ```sh
    pacman -Syu
    ```

4. **Install build dependencies:**

     ```sh
     export _GW="mingw-w64-x86_64"
     pacman -S --needed git make gcc libiconv-devel python3 \
       $_GW-clang $_GW-rustup $_GW-toolchain $_GW-llvm $_GW-lld $_GW-openssl
     ```

   `$_GW-openssl` supplies the OpenSSL 3 headers and static crypto library
   required by the mdxfind bridge. The resulting bridge does not require a
   separate OpenSSL DLL at runtime.

5. **Ensure MinGW toolchain is first on PATH for this session:**

   ```sh
   export PATH="/mingw64/bin:$PATH"
   ```

6. **Set up Rust:**

   ```sh
   rustup default stable && rustup target add x86_64-pc-windows-gnu
   ```

## Build

1. **Fetch the latest hashcat_shooter source code from GitHub:**

   ```sh
   git clone https://github.com/Shooter3k/hashcat_shooter.git
   cd hashcat_shooter
   ```

2. **Compile:**

   ```sh
   make -j"$(nproc)" WIN_PYTHON=""
   ```

   > Upstream uses `make WIN_PYTHON=""`; the `-j$(nproc)` just speeds things up.
   >
   > To rebuild cleanly later, use:
   >
   > ```sh
   > make clean && make -j"$(nproc)" WIN_PYTHON=""
   > ```

---

## Running

### Running inside MSYS2

```sh
./hashcat.exe
```

### Running outside the MSYS2 shell (portable setup)

If you do not want to add `/mingw64/bin` to `PATH`, copy the dependent DLLs
reported by the tools below next to `hashcat.exe`. A standard build commonly
needs:

* `libiconv-2.dll`

(this can be found in `msys64/mingw64/bin`)

To verify which dependencies are missing:

#### Option A: `ldd`

```sh
ldd ./hashcat.exe
```

#### Option B: `ntldd`

```sh
pacman -S --needed mingw-w64-x86_64-ntldd
ntldd -R ./hashcat.exe
```

`ntldd -R` recursively shows transitive DLLs; copy anything not in `C:\Windows\System32` to the same folder as `hashcat.exe`.

## Post-build sanity check

To confirm GPU/OpenCL devices are detected:

```sh
./hashcat.exe -I
```

If your GPUs appear here, your build is good to go.
