# OpenWrt APK WCX

[Русская версия](README.md)

A read-only Total Commander plugin for OpenWrt packages on Windows 10 and 11. Browse the package contents, view and extract regular files, or use **Test archives** to verify their SHA-256 checksums. The plugin does not verify package signatures or run package scripts.

The plugin supports uncompressed ADB and raw DEFLATE (`ADBd` and `ADBc` with algorithm 1). Both x86 and x64 builds passed the DLL tests, including extraction and checksum verification using the original compressed OpenWrt 25.12.0 test package. Builds configured with `APKVIEW_NO_ZLIB` can only read uncompressed packages.

## Install in Total Commander

Open `apkview.zip` **in Total Commander** and confirm the plugin installation. If automatic installation is disabled, install it manually:

1. Extract `apkview.wcx` and `apkview.wcx64` into the same directory.
2. In Total Commander, go to **Configuration → Options → Packer → Configure packer extension WCXs**.
3. Enter `apk` without a dot and select the plugin using **New type**.

Double-click a package or press **Ctrl+PgDn** to open it. Use **F3** to view a file and **F5** to extract it. Android packages use the same `.apk` extension, but the plugin only recognizes ADB containers with the `pckg` package schema.

To disable the plugin, remove the extension association. Close Total Commander before replacing either plugin file during an update.

## Build and package

You need Visual Studio 2022 Build Tools with the C++ toolchain for Win32 and x64, the Windows SDK, and CMake 3.24 or later. If `cmake` and `ctest` are not on your `PATH`, open Developer PowerShell for VS 2022 or use the executables bundled with Visual Studio.

CMake verifies the SHA-256 checksum of the [zlib 1.4 source archive](https://zlib.net/zlib-1.4.tar.gz), then builds the C sources listed in the project as a static library without running third-party build scripts. If you already have the archive, pass `-DAPKVIEW_ZLIB_ARCHIVE=C:/path/to/zlib-1.4.tar.gz` to both configure commands. CMake still checks the checksum.

```powershell
cmake --preset win-x86
cmake --build --preset win-x86-release
ctest --preset win-x86-release
cmake --preset win-x64
cmake --build --preset win-x64-release
ctest --preset win-x64-release
```

To test against a real package, pass `-DAPKVIEW_REAL_PACKAGE=C:/path/to/zlib-1.3.1-r1.apk` to the configure commands. CMake checks the checksum of the official test package and registers the `real_openwrt_package` test.

Once both architectures build and pass their tests, install the files into a new staging directory and ZIP its contents:

```powershell
$stage = "dist/stage-$([guid]::NewGuid().ToString('N'))"
cmake --install build/win-x86 --config Release --prefix $stage
cmake --install build/win-x64 --config Release --prefix $stage
Compress-Archive -Path "$stage/*" -DestinationPath dist/apkview.zip
```

### Development build without zlib

```powershell
cmake --preset win-x64 -B build/no-zlib-x64 -DAPKVIEW_NO_ZLIB=ON
cmake --build build/no-zlib-x64 --config Release
ctest --test-dir build/no-zlib-x64 -C Release --output-on-failure
```

For Win32, replace `x64` with `x86`. The DLL test calls the same plugin exports as Total Commander, but it does not test the Total Commander interface.

## Limitations

- APK v3 packages only. Android APKs, APK v2, IPK, Zstandard compression, and `packages.adb` indexes are not supported.
- Symbolic links, hard links, and special files appear in the listing but cannot be extracted. Exclude them from a multi-file selection before extracting.
- Extraction requires a local absolute path with a drive letter and no reparse points. The plugin rejects ambiguous or invalid names instead of rewriting them.
- Limits: 16 MiB of metadata, 50,000 entries, 8 MiB of indexed names, 64 components per path, and an 8 GiB uncompressed stream.
- Compression applies to the entire stream, so per-file compressed sizes are not displayed.

## License

The plugin is licensed under the GNU Affero General Public License, version 3 only (`AGPL-3.0-only`). See [LICENSE](LICENSE) for the full text. zlib is distributed under its [own license](https://zlib.net/zlib_license.html), included in the installer ZIP as `LICENSE-zlib.txt`.

## References

- [Total Commander Packer Plugin SDK](https://ghisler.github.io/WCX-SDK/table_of_contents.htm)
- [apk-tools source code](https://gitlab.alpinelinux.org/alpine/apk-tools)
- [OpenWrt 25.12.0 test package](https://downloads.openwrt.org/releases/25.12.0/packages/x86_64/base/zlib-1.3.1-r1.apk)
