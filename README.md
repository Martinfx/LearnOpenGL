# FPS Engine

Vlastní 3D FPS engine v C++20 a OpenGL 4.5/4.6 s level editorem v Qt 6.
Běží na **Windows, Linuxu a FreeBSD**.

Návrh architektury a milníků: https://claude.ai/artifact/LzheekHA7ScTb93AKpf7Xq

## Struktura

| Složka | Obsah |
| --- | --- |
| `engine/` | Knihovna enginu: renderer, FPS kamera, game loop s pevným krokem, fyzika (Jolt), hráč, import glTF, formát úrovní |
| `game/` | Spustitelná hra (`fps_game`), okno a vstup přes GLFW |
| `editor/` | Level editor (`fps_editor`) v Qt 6 Widgets, viewport používá stejný renderer jako hra |
| `tests/` | Testy jádra bez OpenGL (CTest) |
| `assets/` | Ukázková úroveň `levels/test.level` a model `models/ramp.gltf` |

## Závislosti

Build bere knihovny nejdřív ze systému a chybějící si stáhne sám (GLM, GLFW, Jolt Physics).
glad a cgltf jsou přímo v repozitáři.
Qt 6 se nestahuje, musí být nainstalované.

**FreeBSD**
```sh
pkg install git cmake ninja glfw glm qt6-base
```

**Linux (Debian/Ubuntu)**
```sh
sudo apt install build-essential cmake ninja-build libglfw3-dev libglm-dev qt6-base-dev libgl-dev
```

**Windows**: Visual Studio 2022 (C++ workload), CMake a Qt 6 z [online installeru](https://www.qt.io/download-qt-installer) (MSVC 2022 64-bit).

## Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```
Na Windows přidej `-DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2022_64`.
Bez Qt jde postavit jen hra: `-DENGINE_BUILD_EDITOR=OFF`.

Výstupy jsou v `build/bin/`.

## Ovládání

**Hra** (`fps_game [soubor.level]`): WASD pohyb, myš rozhlížení, mezerník skok, Shift sprint, Esc uvolní myš.

**Editor** (`fps_editor [soubor.level]`): pravé tlačítko + WASD/QE létání (kolečko mění rychlost),
levé tlačítko výběr, F zaměří výběr, Ctrl+B přidá box, Ctrl+I importuje glTF model, Ctrl+D duplikuje, Del smaže,
**F5 spustí úroveň ve hře**.

## Modely z Blenderu

File > Export > glTF 2.0 (`.glb` nebo `.gltf`), Y nahoru (výchozí). V editoru Ctrl+I.
Model je zároveň statická kolize (Jolt MeshShape), takže po něm jde chodit. Barva se bere z Base Color materiálu.

## Stav

Milník 1 hotový: hráč (Jolt CharacterVirtual) chodí po úrovni z boxů a glTF modelů, schody, svahy, skok.
Další je editor pro celou hru: entity a komponenty, gizmo, undo/redo (viz plán v dokumentu).
