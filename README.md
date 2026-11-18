<div align="center">
  <img src="logo.svg" alt="logo" width="750">
</div>

<p align="center">
   <a href="https://github.com/dsf-boop/DeepExploration-o3d-plugin/releases/latest"><img alt="version" src="https://img.shields.io/github/v/release/dsf-boop/DeepExploration-o3d-plugin?label=version&style=for-the-badge"></a>
   <a href="LICENSE"><img alt="license" src="https://img.shields.io/github/license/dsf-boop/DeepExploration-o3d-plugin?style=for-the-badge"></a>
</p>

A high-performance, native 32-bit format plugin (`.x3d` DLL) for **Right Hemisphere Deep Exploration 5.7** to import, inspect, edit, and export **OMSI 1 & 2** 3D model files (`.o3d`).

## Installation

1. Close **Deep Exploration 5.7** if it is running.
2. Go to the releases page and download the [latest release](https://github.com/dsf-boop/DeepExploration-o3d-plugin/releases/latest).
3. Copy `o3d.x3d` into the Deep Exploration `Plugins` directory:

   ```
   <Deep Exploration Installation Folder>\Plugins\o3d.x3d
   ```
   *(e.g., `C:\Program Files (x86)\Right Hemisphere\Deep Exploration 5.7\Plugins\o3d.x3d`)*.
4. Launch **Deep Exploration 5.7**.
5. The formats are now available in the **"File -> Open"** and **"File -> Save As"** dialogs:
   - **OMSI 2 3D Model (*.o3d)**
   - **OMSI 2 Model Config (*.cfg)** *(Import / Batch Load)*

## Features

- **One-Click Vehicle Assembly:** Open any OMSI model configuration file (e.g., `model_EN92.cfg`) directly in Deep Exploration via **File -> Open**.
- **Full Version Support:** Supports all .o3d file versions:
  - **Classic Formats (v1-v2):** 16-bit vertex and face counters (`uint16_t`).
  - **Extended Formats (v3-v7):** 32-bit vertex and face counters (`uint32_t`).
  - **High-Poly Mesh Support (v4+ flags):** Supports `long_tris` (32-bit triangle indices) for large meshes.
- **Pivot Point Preservation:** The plugin correctly imports and exports the pivot point (`0x79` section).
- **Smart Texture Resolution:** The plugin automatically searches for referenced texture files in the model folder as well as along relative paths.

## Building from Source

The plugin is written in pure C++ (Win32 API & standard C Runtime) and has zero external dependencies.

### Prerequisites
A 32-bit MinGW-w64 / GCC compiler (such as `i686-w64-mingw32-g++` from MSYS2, MinGW, or LLVM-MinGW).

### Automated Build
Run the provided build script:
```cmd
build.bat
```
The script automatically locates a 32-bit GCC compiler in your system `PATH` or standard installation directories (MSYS2, WinGet LLVM-MinGW, MinGW) and compiles the plugin directly to `dist\o3d.x3d`.

### Manual Compilation
You can also compile manually with any MinGW-w64 32-bit compiler:
```cmd
i686-w64-mingw32-g++ -shared -O2 -m32 -static -Wl,--kill-at plugin.def o3d_plugin.cpp -o dist\o3d.x3d
```

## License

This project is licensed under the MIT License.
