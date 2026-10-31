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
| `3rdparty/` | Knihovny třetích stran v původní podobě (needitují se), viz [`3rdparty/README.md`](3rdparty/README.md) |
| `assets/` | Ukázková úroveň, modely, zbraně (`weapons/*.weapon`), AI skript `scripts/enemy.lua` |

## Závislosti

Všechny knihovny třetích stran jsou v `3rdparty/` beze změn (glad, cgltf, stb_image, GLM, GLFW,
Jolt Physics, Lua 5.4, sol2), build nic nestahuje. GLM, GLFW a Jolt se berou ze systému, pokud
jsou nainstalované (vypnutí: `-DENGINE_USE_SYSTEM_DEPS=OFF`). Qt 6 musí být nainstalované.

**FreeBSD**
```sh
pkg install cmake ninja glfw glm qt6-base
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

**Hra** (`fps_game [soubor.level]`): WASD pohyb, myš rozhlížení, mezerník skok, Shift sprint,
levé tlačítko střelba, R přebití, 1–9 nebo kolečko výběr zbraně, Esc uvolní myš.

**Editor** (`fps_editor [soubor.level]`): pravé tlačítko + WASD/QE létání (kolečko mění rychlost),
levé tlačítko výběr, F zaměří výběr, Ctrl+B přidá box, Ctrl+I importuje glTF model, Ctrl+D duplikuje, Del smaže,
**F5 spustí úroveň ve hře**.

## Skriptování a AI (Lua)

Entity (nepřátelé, pickupy, triggery…) se přidávají v editoru (Ctrl+E přidá nepřítele) a každá může mít
Lua skript. Skript definuje `on_start(self)` a `on_update(self, dt)`; API je popsané v
[`assets/scripts/API.md`](assets/scripts/API.md).

Ukázková AI nepřítele [`assets/scripts/enemy.lua`](assets/scripts/enemy.lua) hlídkuje kolem místa,
kde stojí, hráče vidí v zorném úhlu (a neprojde přes zdi), pronásleduje ho, útočí, a když ho ztratí,
hledá ho na posledním známém místě a vrací se zpět. Parametry jsou na začátku skriptu. Chyba ve skriptu
vypne jen danou entitu a vypíše se do konzole hry.

### Script editor

Skripty se píšou přímo v editoru: tlačítko **Edit script** u entity nebo menu Script (New script,
Open script). Každý skript má vlastní záložku vedle 3D pohledu. Umí zvýraznění Lua, čísla řádků,
kontrolu syntaxe při psaní (chybný řádek je červeně podtržený a popsaný dole), doplňování API enginu
(Ctrl+Space, nebo samo po `game.`, `self:`…), automatické odsazení, Ctrl+/ komentář, Ctrl+F hledání,
Ctrl+H nahrazování, Ctrl+G skok na řádek. Ctrl+S uloží skript v aktivní záložce; Play a uložení úrovně
uloží i otevřené skripty. Po uložení se obnoví parametry skriptu v Properties.

## Materiály a textury

Materiál je textový soubor `.material` (barva, textura, opakování, odlesk, ostrost odlesku, svit,
promítání ve světových souřadnicích). Editor: menu Assets > Material editor (Ctrl+M): seznam materiálů,
všechna nastavení a živý náhled na kouli a kostce. Změny se hned projeví i ve 3D pohledu.
Box dostane materiál v Properties > Material; u glTF modelu jde v tabulce materiálových slotů každý
materiál z Blenderu nahradit materiálem enginu (dvojklik).

Texture editor (Assets > Edit texture, nebo Edit u textury materiálu): opakování, filtrování,
mipmapy, sRGB, jas, kontrast, sytost, tónování, převrácení, náhled 3 × 3 pro kontrolu švů. Obrázek
se nemění, nastavení se ukládá vedle něj jako `obrazek.png.texture`.

Editor ve vývojovém buildu ukládá assety přímo do `assets/` ve zdrojovém stromu (vypnutí:
`-DENGINE_EDITOR_SOURCE_ASSETS=OFF`), takže změny jdou rovnou do gitu a build je nepřepíše.

## Animované postavy

Entita může mít animovaný glTF model (skeleton, skiny, klipy): Properties > Character model.
Model se automaticky přizpůsobí výšce entity (Size Y) a postaví se na zem; Model scale a Model yaw ho
doladí. V poli Animation se vybírá klip, který editor přehrává jako náhled a který hraje, když skript
žádný nevybere. Skript přehrává klipy přes `self:play("Running")` (plynulé prolínání mezi klipy).
AI nepřítele má klipy jako parametry (`anim_idle`, `anim_walk`, `anim_run`, `anim_attack`,
`anim_death`), v editoru se vybírají ze seznamu klipů modelu. Mrtvý nepřítel zůstane ležet.

Ukázková postava je robot od Quaterniuse (CC0) se 14 animacemi, viz `assets/models/characters/CREDITS.md`.
Z Blenderu: armatura + akce, export glTF 2.0 s volbou Animation (každá akce = jeden klip).

## Co se nastavuje v editoru

Všechno, co hra používá, se mění v editoru a ukládá do souborů úrovně nebo assetů:

| Co | Kde v editoru | Uloženo v |
| --- | --- | --- |
| Geometrie, modely, entity, skripty | Outliner + Properties | `.level` |
| Model postavy, měřítko, natočení, výchozí animace | Properties > Character model, Animation | `.level` |
| Lua skripty | Záložky script editoru (Edit script, menu Script) | `scripts/*.lua` |
| Materiály, materiály boxů a slotů modelů | Material editor (Ctrl+M), Properties > Material | `materials/*.material`, `.level` |
| Nastavení a úpravy textur | Texture editor | `textures/*.png.texture` |
| Parametry AI a dalších skriptů | Properties > Script parameters (deklaruje je skript v `properties`) | `.level` (po entitách) |
| Pohyb a zdraví hráče, start, slunce | Properties > Level | `.level` |
| Výzbroj hráče a parametry zbraní | Záložka Weapons | `.level` + `weapons/*.weapon` |

## Zbraně

Každá zbraň je soubor `.weapon` (model, poškození, kadence, automatická/poloautomatická, broky,
zásobník, přebíjení, rozptyl, dostřel, poloha v ruce). Ukázkové: Repeater (automat) a Scatter Blaster
(brokovnice). Modely jsou z Kenney Blaster Kit pod licencí CC0, viz `assets/models/weapons/CREDITS.md`.

## Modely z Blenderu

File > Export > glTF 2.0 (`.glb` nebo `.gltf`), Y nahoru (výchozí). V editoru Ctrl+I.
Model je zároveň statická kolize (Jolt MeshShape), takže po něm jde chodit. Barva se bere z Base Color materiálu.

## Stav

Hotovo: hráč (Jolt CharacterVirtual), glTF modely s texturami, entity s Lua skripty, AI nepřátel,
zbraně se střelbou a zdravím nepřátel, animované postavy (skeletal animace, GPU skinning), script editor,
materiály a textury s editory, editor pro všechny parametry.
Další: PBR (normal/roughness mapy), nové modely a textury, nůž/samopal/pistole, hierarchie scény, animation editor,
zvuk (OpenAL), environment editor.
