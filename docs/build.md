# Building v1.1

Build tools: Windows 10/11 x64, MinGW-w64 GCC/UCRT, CMake, Ninja, rustup with
`stable-x86_64-pc-windows-gnu`. Python plus requirements-dev.txt is needed only for tests.
People using the release EXE need none of these tools.

Download the official [Magisk v31.1 APK](https://github.com/topjohnwu/Magisk/releases/tag/v31.1)
to `.local/magisk/Magisk-v31.1.apk`. CMake and the runtime enforce its pinned
SHA-256 `89cefac0a318afa39fb3880c45c87b036c05bc1ebf02a7a715b7f8d7c26ac6e7`.

```powershell
rustup toolchain install stable-x86_64-pc-windows-gnu --profile minimal
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
python -m pip install -r requirements-dev.txt
python tests/test_engine.py
python tests/test_magisk.py
$env:OFP_MAGISK_APK = (Resolve-Path '.local/magisk/Magisk-v31.1.apk').Path
$env:RUSTFLAGS = '-C linker=rust-lld -C linker-flavor=ld.lld -C link-self-contained=yes'
cargo +stable-x86_64-pc-windows-gnu test --locked --manifest-path native/magisk-engine/Cargo.toml
```

`build/OFPStudio.exe` is the only runtime program. The other EXEs are development
test drivers. `MAGISK_APK` is a CMake cache option for another local APK path.
`OFP_CARGO_DIR` can point to rustup's bin directory if cargo is outside PATH.

## Corresponding source release

Each v1.1 binary release includes a corresponding-source archive. It contains this
application, locked Rust dependency sources (including avbroot), the embedded APK,
and official Magisk v31.1 source including submodules and its native Rust vendors.
The application vendor directory is configured in `.cargo/config.toml`, allowing
Cargo builds without fetching source dependencies. From the archive root run:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMAGISK_APK=resources/Magisk-v31.1.apk
cmake --build build
```

Compiler/SDK installation is still required. Upstream Magisk's Android build is
separate: see `upstream/Magisk/README.MD`, `docs/build.md` and build.py in that source
tree for its required Android SDK/NDK and custom Rust nightly toolchain. Its own
native Cargo vendor configuration is included in that tree. This Windows app embeds
the verified official APK rather than rebuilding Magisk's Android binaries.
