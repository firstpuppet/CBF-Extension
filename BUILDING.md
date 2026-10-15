# Building on Linux

GD has no native Linux version, so on Linux you build the mod for one of the platforms GD runs on: **Windows** (to play through Wine/Proton) or **Android**. Both work from Linux.

The mod targets **Geode SDK 5.3.0** and **GD 2.2081** (see `mod.json`). Use the same SDK version, or the build may fail or the mod may not load.

## 1. Install the build tools

You need Git, CMake 3.29 or newer, Clang 19 or newer (21+ recommended), LLD, LLVM and Ninja.

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
3. Make sure the SDK version matches the `"geode"` field in `mod.json` (currently `5.3.0`). See `geode sdk --help` for the commands that switch SDK versions.
4. Optional: run `geode config setup` to create a profile pointing at your GD install. Desktop builds are then installed into GD automatically.

## 3a. Build for Windows (play through Wine/Proton)

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

## 3b. Build for Android

1. Download the [Android NDK](https://developer.android.com/ndk/downloads) for Linux and extract it. Use **r28 or newer**: r27 failed to compile Geode 5.3.0's dependencies when testing this.
2. Point `ANDROID_NDK_ROOT` at it, e.g. in your `~/.bashrc`:
   ```sh
   export ANDROID_NDK_ROOT="$HOME/android-ndk-r28c"
   ```
3. Install the Android binaries and build (use `android32` instead for 32-bit phones):
   ```sh
   geode sdk install-binaries -p android64
   geode build -p android64
   ```
4. Copy the `.geode` file from `build-android64` to your phone:
   `/storage/emulated/0/Android/media/com.geode.launcher/game/geode/mods/`

## Building with plain CMake

If you don't want the CLI to drive the build, after steps 1-2 you can run CMake directly. `GEODE_SDK` must be set:

```sh
# Windows (cross-compile), using the toolchain from `geode sdk install-linux`
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_TOOLCHAIN_FILE=/path/to/clang-msvc-sdk/clang-msvc.cmake -DSPLAT_DIR=/path/to/splat
cmake --build build

# Android 64-bit
cmake -B build-android64 -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=23
cmake --build build-android64
```

## Troubleshooting

- **`setup_geode_mod called, but Geode CLI was not found`**: the `geode` binary isn't on your `PATH`. Packaging the `.geode` file needs the CLI. Adding `-DGEODE_DISABLE_CLI_CALLS=ON` to the CMake command skips packaging, which is useful for a compile-only check.
- **`No valid loader binary to link to!`**: run `geode sdk install-binaries` (add `-p android64` / `-p android32` for Android).
- **Errors inside `nontype_functional` (`move_only_function.h`, e.g. `no member named 'is_trivially_default_constructible_v'`)**: this showed up when building Geode 5.3.0's dependencies with the Android NDK. It's a missing `#include <type_traits>` in that third-party header, not a problem in this mod. Adding `#include <type_traits>` to the top of `build-android64/_deps/nontype_functional-src/include/std23/move_only_function.h` and building again works around it.
- **CMake version errors**: make sure `cmake --version` reports 3.29 or newer.

The GitHub Actions workflow in `.github/workflows/multi-platform.yml` builds every platform automatically on push. If Actions are enabled on your repository, that's an easy way to get Windows/macOS/iOS/Android builds without setting anything up locally.

For more details, see the [Geode documentation](https://docs.geode-sdk.org/).
