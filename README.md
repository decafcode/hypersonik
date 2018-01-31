# Hypersonik

Hypersonik is a reimplementation of DirectSound built on top of WASAPI, built with low-latency use cases in mind. This is currently very early alpha software which is missing large chunks of functionality. Patches welcome.

## Building

This project uses the Meson build system, which in turn uses ninja to drive the compilation process (usually). If you are familiar with Autotools, then the commands `meson` and `ninja` act as the rough equivalents of the `./configure` and `make` commands from the procedure for compiling Autotools software packages.

To build this project on Windows, you will first need to install a recent version of MSYS2 and fully update it in accordance with the MSYS2 documentation. Once the base MSYS2 system is installed and fully updated, install the following packages inside the MSYS2 environment:

```
$ pacman -S mingw-w64-i686-meson mingw-w64-i686-toolchain
```

On Linux development hosts, you will need to install suitable equivalents using your package manager. The w64-mingw32 cross-compilation toolchain is required.

Once you have installed the necessary packages on your development platform, a debug build can be compiled as follows (omit `--cross w64-mingw32.txt` if compiling on MSYS2)

```
$ meson debug --cross w64-mingw32.txt
$ cd debug
$ ninja
```

A directory name other than `debug` can be passed to meson if desired.

Alternatively, to make a release build:

```
$ meson release --buildtype release --cross w64-mingw32.txt
$ cd release
$ ninja
```

A `src/dsound.dll` binary will be produced inside the respective build directory.

## License

This project is released under the terms of the MIT License.
