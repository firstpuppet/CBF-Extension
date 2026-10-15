# Building on Linux

This mod only supports **Windows**. GD has no native Linux version, so on Linux you cross-compile the Windows build and play through Wine/Proton.

The mod targets **GD 2.2081** and needs **Geode 5.3.0 or newer** at runtime (see `mod.json`). Build it with the **latest** Geode SDK. Geode always pulls the newest GD bindings, which only match the current SDK, so an older SDK such as 5.3.0 fails to compile.

This setup was tested on Ubuntu 24.04 with Clang 19, CMake 3.31 and Geode SDK 5.11.1, producing `ClickBetweenFrames.dll`.

## 1. Install the build tools

You need Git, CMake 3.29 to 3.31, Clang 19 or newer (21+ recommended), LLD, LLVM and Ninja.

CMake 4.x currently doesn't work with Geode's Windows cross-compiling toolchain: it links with the system `ld` instead of `lld-link` and fails during configuration. If your distro ships CMake 4, install 3.31 alongside it, for example with `pipx install "cmake<3.32"`.

```sh
# Debian / Ubuntu / Linux Mint
sudo apt install clang clang-tools lld llvm cmake git ninja-build

# Fedora
sudo dnf install clang lld llvm cmake git ninja-build

# Arch
sudo pacman -S clang lld llvm cmake git ninja
```

Some distros (e.g. Debian 12, older Linux Mint) ship a CMake older than 3.29. In that case install a newer one, e.g. from the [Kitware APT repository](https://apt.kitware.com/). Check your versions with `cmake --version` and `clang --version`.

## 2. Install the Geode CLI and SDK

1. Download the Linux build of the Geode CLI from the [CLI releases page](https://github.com/geode-sdk/cli/releases), extract it and put the `geode` binary somewhere on your `PATH` (e.g. `~/.local/bin`).
   Check it works with `geode --version`.
2. Install the SDK:
   ```sh
   geode sdk install
   ```
   This sets the `GEODE_SDK` environment variable. Restart your shell afterwards so it is picked up.
3. Keep the SDK up to date (`geode sdk update`). See `geode sdk --help` for the other SDK commands.
4. Optional: run `geode config setup` to create a profile pointing at your GD install. Desktop builds are then installed into GD automatically.

## 3. Build

Install the Windows SDK and the cross-compiling CMake toolchain, plus the prebuilt Geode binaries:

```sh
geode sdk install-linux
geode sdk install-binaries
```

Then, from the root of this repository:

```sh
geode build --ninja
```

The packaged mod (`syzzi.click_between_frames.geode`) ends up in the `build` folder. If you set up a profile in step 2, it is also copied into your GD `geode/mods` folder.

If you installed the Windows SDK/toolchain some other way, pass the paths yourself:

```sh
geode build -- -DCMAKE_TOOLCHAIN_FILE=/path/to/clang-msvc-sdk/clang-msvc.cmake -DSPLAT_DIR=/path/to/splat
```

## Building with plain CMake

If you don't want the CLI to drive the build, after steps 1-2 you can run CMake directly. `GEODE_SDK` must be set:

```sh
# using the toolchain from `geode sdk install-linux`
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TRY_COMPILE_CONFIGURATION=Release \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/clang-msvc-sdk/clang-msvc.cmake -DSPLAT_DIR=/path/to/splat -DHOST_ARCH=x64
cmake --build build
```

If your Clang isn't the default `clang` on your `PATH` (e.g. it's installed as `clang-19`), also pass `-DLLVM_VER=19 -DCLANG_VER=19`.

Configuring without a Windows toolchain fails on purpose with `This mod only supports Windows`.

## Running the unit tests

The timing maths in `src/core/timing.hpp` (step splitting, step counts, 2.1/2.2 detection) doesn't depend on Geode or Windows. It has unit tests you can run natively on Linux with any C++20 compiler:

```sh
cmake -S tests -B build-tests
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Run them after changing anything in `src/core/`. CI runs them on every push.

## Troubleshooting

- **`setup_geode_mod called, but Geode CLI was not found`**: the `geode` binary isn't on your `PATH`. Packaging the `.geode` file needs the CLI. Adding `-DGEODE_DISABLE_CLI_CALLS=ON` to the CMake command skips packaging, which is useful for a compile-only check.
- **`No valid loader binary to link to!`**: run `geode sdk install-binaries`.
- **`This mod only supports Windows`**: CMake isn't using the Windows cross-compiling toolchain. Use `geode build` after `geode sdk install-linux`, or pass `CMAKE_TOOLCHAIN_FILE` as shown above.
- **CMake version errors**: make sure `cmake --version` reports 3.29 to 3.31.
- **`/usr/bin/ld: Error: unable to disambiguate: -defaultlib:libcmt`** while configuring: you're using CMake 4.x. Switch to CMake 3.31 (see step 1).
- **`lld-link: error: could not open 'msvcrtd.lib'`** while configuring: CMake's compiler check builds in Debug mode, and the Windows SDK unpacked for cross-compiling has no debug runtime. Add `-DCMAKE_TRY_COMPILE_CONFIGURATION=Release`.
- **`error: class member cannot be redeclared` in `CCFileUtilsWin32.h`**: your Geode SDK is older than the bindings it downloaded. Update the SDK (`geode sdk update`).
- **`geode sdk install-linux` fails with `JSON response doesn't contain 'assets'`**: GitHub's API refused the request, usually because of its rate limit. Wait and try again later.

The GitHub Actions workflow in `.github/workflows/multi-platform.yml` builds the mod automatically on push. If Actions are enabled on your repository, that's an easy way to get a build without setting anything up locally.

For more details, see the [Geode documentation](https://docs.geode-sdk.org/).
