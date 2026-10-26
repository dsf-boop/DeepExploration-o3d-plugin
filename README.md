# OMSI 2 3D Model (.o3d) Plugin for Right Hemisphere Deep Exploration 5.7

A high-performance, native 32-bit format plugin (`.x3d` DLL) for **Right Hemisphere Deep Exploration 5.7** to import, inspect, edit, and export **OMSI / OMSI 2** 3D model files (`.o3d`, versions 1 through 7).

---

## 🚀 Installation

1. Close **Deep Exploration 5.7** if it is running.
2. Go to the releases page and download the [latest](https://github.com/dsf-boop/DeepExploration-o3d-plugin/releases/latest) release.
3. Copy `o3d.x3d` into the Deep Exploration `Plugins` directory:
   ```
   <Deep Exploration Installation Folder>\Plugins\o3d.x3d
   ```
   *(e.g., `C:\Program Files (x86)\Right Hemisphere\Deep Exploration 5.7\Plugins\o3d.x3d`)*.
4. Launch **Deep Exploration 5.7**.
5. The formats are now available in the **"File -> Open"** and **"File -> Save As"** dialogs:
   - **OMSI 2 3D Model (*.o3d)**
   - **OMSI 2 Model Config (*.cfg)** *(Import / Batch Load)*

---

## 🛠 Features & Technical Capabilities

### 1. OMSI Model Configuration Batch Import (*.cfg)
- **One-Click Vehicle & Scenery Assembly:** Open any OMSI vehicle or scenery model configuration file (e.g., `model_xxx.cfg`) directly in Deep Exploration via **File -> Open**.
- **Automated Scene Construction:** The plugin parses all `[mesh]` directives in the `.cfg` file, loads every referenced `.o3d` mesh, and arranges them in the scene tree with their respective mesh names.
- **Accurate Spatial Alignment:** Since vertex coordinates in OMSI `.o3d` files are defined in vehicle rest coordinates, all sub-assemblies (body shell, wheels, doors, dashboard instruments, seats, interior fittings) automatically align into a complete, assembled vehicle in the 3D viewport.
- **Shared Material Optimization:** Textures and materials shared across multiple sub-meshes are cached and reused, preventing duplicate material allocations and optimizing viewport performance.

### 2. Comprehensive O3D Version Support (v1 — v7)
- **OMSI 1 & 2 Legacy Formats (v1–v2):** 16-bit vertex and face counters (`uint16_t`).
- **Extended Formats (v3–v7):** 32-bit vertex and face counters (`uint32_t`).
- **High-Poly Mesh Support (v4+ flags):** Supports `long_tris` (32-bit triangle indices) for large meshes.

### 3. Precise Coordinate System Transformation
- Automatic, seamless conversion between OMSI's left-handed DirectX coordinate system ($X$ Right, $Y$ Up, $Z$ Forward) and Deep Exploration's right-handed CAD coordinate system ($X$ Right, $Y$ Forward, $Z$ Up).
- **Correct Model Orientation:** Vehicles and parts sit upright on their wheels on the ground grid, passenger doors remain on the proper side, and hoods face forward.
- **Winding Order & Normals:** Triangles maintain proper front-facing winding orders and external normal directions through reflection transformations.
- **Export Consistency:** The export process performs an exact mathematical inverse transformation, ensuring 100% position and orientation fidelity in OMSI.

### 4. Texture UV Mapping & Multi-Index Vertex Welding
- **DirectX vs. OpenGL UV Inversion:** Automatically handles the vertical texture unwrap difference ($V_{DE} = 1.0 - V_{OMSI}$), preventing upside-down texture mapping.
- **Multi-Index Geometry Unification:** Resolves 3D formats (such as OBJ or CAD models) that store geometry, normals, and UV coordinates in independent index streams (`f v/vt/vn`). The exporter welds vertices per unique `(pos_idx, norm_idx, uv_idx)` tuple, eliminating spiderweb texture stretching and seam artifacts.

### 5. Pivot & Animation Center Preservation (Section 0x79)
- Section `0x79` in OMSI defines the model's pivot/rotation axis (critical for wheels, doors, needles, and the `[origin_from_mesh]` directive).
- **Unmodified Geometry:** The original pivot transformation matrix is stored and written back bit-for-bit upon export.
- **Repositioned Geometry:** If a part is moved or rotated inside Deep Exploration, the plugin recalculates the local coordinates and updates the Section `0x79` matrix accordingly.

### 6. Multi-Material & Texture Support
- **Per-Face Materials:** Fully supports multi-material meshes where each polygon references its own material (`pFaceMats`).
- **Material Attributes:** Imports and exports Diffuse, Specular, Emissive colors, Alpha/Opacity transparency, and Shininess/Specular power.
- **Smart Texture Resolution:** Automatically searches for referenced texture files in the model folder as well as relative paths (`Texture/`, `../Texture/`, `texture/`).

### 7. Universal OMSI 1 & 2 Compatibility
- By default, models are exported in the universal **OMSI Version 1 (v1)** format with standard 16-bit short indices, ensuring seamless compatibility across OMSI 1, OMSI 2, Blender O3D import scripts, 3ds Max tools, and third-party converters.
- Automatically promotes to Version 7 if geometry exceeds 65,535 vertices or faces.

### 8. Safe I/O & Dynamic Diagnostics
- Data stream operations utilize native Deep Exploration host interfaces (`Stream_Write` / `Stream_Read`) with strict pointer safety checks to avoid application crashes.
- Diagnostic logs are automatically written to `o3d_plugin.log` in the plugin directory for easy debugging.

---

## 💻 Building from Source

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
i686-w64-mingw32-g++ -shared -O2 -m32 -static -Wl,--kill-at plugin.def o3d_plugin.cpp -o dist/o3d.x3d
```

---

## 📄 License

This project is licensed under the MIT License.
