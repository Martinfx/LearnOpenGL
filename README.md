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
levé tlačítko střelba, pravé tlačítko míření přes mířidla (přiblížení, menší rozptyl; nastavení `aim_*` v panelu Weapons), R přebití, 1–9 nebo kolečko výběr zbraně, Esc uvolní myš.

**Editor** (`fps_editor [soubor.level]`): pravé tlačítko + WASD/QE létání (kolečko mění rychlost),
levé tlačítko výběr, F zaměří výběr, Ctrl+B přidá box, Ctrl+I importuje glTF model, Ctrl+D duplikuje, Del smaže,
**F5 spustí úroveň ve hře**.

**Level Zóna** (`fps_game assets/levels/zone.level`, v editoru File > Open): dálnice E-95 ucpaná zrezlými auty
a autobusy, železniční hřbitov s vagony a táborem stalkerů (start), město s nemocnicí a paneláky, elektrárna
s chladicími věžemi, řeka s rozbitými mosty a mrtvé město. Do paneláků, nemocnice, strojovny, dílny, vagonů,
autobusů a mezi nohy chladicích věží se dá vejít (kolize podle meshe, schody do všech pater). Plán:
`assets/levels/source/zone_map.png` (`draw_zone_map.py` ho překreslí z levelu i po úpravách v editoru).
Výchozí rozložení vytvořil `assets/levels/source/make_zone_level.py`, modely `assets/models/zone/gen/`
generuje `assets/models/zone/source/build_zone.py`.

**Level Ulice u nemocnice** (`fps_game assets/levels/hospital_street.level`): menší ručně složená mapa
s úkolem. Start v táboře stalkerů u vagonů, cíl je skrýš s léky v horním patře nemocnice (`objective.lua`
ohlásí nalezení), pak návrat do tábora. Hlavní cesta vede dírou v plotu kolem havarované evakuační kolony,
boční cesta přes garáže a dvůr po požárním schodišti. Bandité hlídají ulici (odstřelovač v paneláku),
vchod a chodby nemocnice; na východě je staré město (scéna "Postwar City", CC-BY, viz
`assets/models/zone/postwar/CREDITS.md`). Na mapě (M) jsou značky míst. Plán s trasami:
`assets/levels/source/hospital_street_map.png`, rozložení `assets/levels/source/make_street_level.py`.

**Level Skrunda-1** (`fps_game assets/levels/skrunda1.level`): opuštěné sovětské radarové městečko Skrunda-1
v Lotyšsku. Budovy, ulice a rybník jsou 1:1 podle OpenStreetMap (© OpenStreetMap contributors, ODbL,
`assets/levels/source/osm/`), budovy mají skutečné rozměry půdorysů: chruščovky, posádková nemocnice
(Госпиталь), štáb, obchod „Venta", jídelna, vodárna. Textury jsou reálné fotoskeny Poly Haven (CC0).
Úkol: najít PDA stalkera Ljochy ve 3. patře nemocnice a vrátit se do Štábu, volitelně jeho skrýš ve vodárně.
Trasy: hlavní po Nové ulici pod odstřelovačem, boční přes dvory k zadnímu vchodu nemocnice, třetí k vodárně.
Plán s trasami: `assets/levels/source/skrunda1_plan.png` (`draw_skrunda_plan.py` ho překreslí z levelu).
Zem je terén enginu (`skrunda1.terrain`, 577 × 577 výšek po 1 m, 4 vrstvy textur) a stromy, keře a tráva
jsou jeho foliage, takže se upravují štětci v režimech Landscape a Foliage. Silnice, cesty a rybník jsou
přehozené overlay modely ve skupině Terrain. Budovy: `python3 -I assets/models/zone/source/build_skrunda.py`,
overlaye `... build_skrunda.py overlays`, rozložení, terén a foliage `python3 -I assets/levels/source/make_skrunda_level.py`
(přepíše i ruční úpravy terénu).
Vzhled je laděný podle reálných fotek Skrundy-1 (jaro): souvislá loňská slaměná tráva, břízy, světlá
silikátová cihla, prázdná okna, stékající špína pod parapety a střechou (alfa decaly, atlas `zone/textures/grime_albedo.png`
z `skrunda.make_grime()`), nepořádek ve skupině Clutter (papíry v bytech, suť pod balkony, kořist banditů).
Barvu zemních vrstev ladí materiály `assets/materials/skrunda/terrain_*.material` (editovatelné v editoru).

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

Renderer je fyzikálně založený (PBR, metal/roughness jako glTF a Blender Principled BSDF): albedo,
normal mapa (OpenGL konvence, zelená nahoru), roughness, metallic, ambient occlusion a emissive mapa.
glTF modely tyto mapy načítají samy. Materiál enginu je textový soubor `.material` (barva, roughness,
metallic, síla normal mapy, svit, mapy, opakování, promítání ve světových souřadnicích). Editor: menu Assets > Material editor (Ctrl+M): seznam materiálů,
všechna nastavení a mapy a živý náhled na kouli a kostce. Změny se hned projeví i ve 3D pohledu.
Box dostane materiál v Properties > Material; u glTF modelu jde v tabulce materiálových slotů každý
materiál z Blenderu nahradit materiálem enginu (dvojklik).

- **Instance materiálu** (tlačítko New instance): `parent "zaklad.material"` a jen změněné hodnoty
  (tučné popisky); ostatní se řídí rodičem. Reset to parent vrátí změny.
- **Detail mapy**: šedá detail mapa a detail normal mapa, jemně opakované (Detail tiling), slábnou se
  vzdáleností (Detail distance). Zrno betonu, škrábance na kovu zblízka.
- **Blend vrstvy a vertex paint**: materiál může mít Blend layer 1 a 2 (jiné materiály, např. mech a
  bláto). V režimu **Paint** (Shift+6) se štětcem maluje na vybraný model: Layer 1 (červená), Layer 2
  (zelená) a Wet (mokrý, tmavší a lesklý povrch). Okraj sleduje jas textury vrstvy (mech nejdřív v
  prasklinách), ostrost nastaví Blend sharpness. Barvy vrcholů se ukládají do levelu u každé kopie modelu.

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

Nepřátelé jsou zatím lidský manekýn od Quaterniuse (CC0) se 46 animacemi (chůze, běh, míření a střelba z pistole, zásah, smrt), viz `assets/models/characters/CREDITS.md`. Skript `enemy.lua` je ozbrojený člověk: hlídkuje, pronásleduje, z dálky střílí a zblízka udeří.
Z Blenderu: armatura + akce, export glTF 2.0 s volbou Animation (každá akce = jeden klip).

## Načítání

- **Hra:** level se načítá na pozadí. Všechny soubory modelů se čtou paralelně na pracovních vláknech a každý jen jednou. Pak se meshe a textury pošlou na GPU, takže se ve hře už nedonahrávají. Mezitím běží načítací obrazovka s obrázkem, titulkem, nápovědou, progressbarem a právě načítanou položkou. Okno po celou dobu reaguje.
- **Nastavení obrazovky:** v editoru v Details > Level > Loading screen (Image, Title, Hint). V levelu jsou to řádky `loading_image`, `loading_title` a `loading_hint`.
- **Editor:** při startu se ukáže úvodní obrazovka s progressbarem, dokud se nenačte první level. Při otevírání levelu je progress nad 3D pohledem a ve stavovém řádku.
- **Písmo:** DejaVu Sans v `assets/fonts` (licence v `LICENSE-DejaVu.txt`), vykreslené přes stb_truetype.

## Mapa (PDA)

- **Minimapa** vpravo nahoře se otáčí s hráčem (poloměr 60 m), ukazuje značky a souřadnice.
- **Klávesa M** otevře celou mapu (PDA), sever nahoře, mřížka po 50 m, šipka hráče a popisky značek.
- **Značky:** v editoru Add > Map marker; jméno entity je popisek. Ve hře jsou neviditelné.
- **Obrázek mapy:** Details > Level > Map (Image, Bounds X/Z/size v metrech). Bez obrázku hra při načtení vyrenderuje level shora. V levelu řádky `map_image` a `map_bounds minX minZ size`.

## Rozložení editoru

- **Hlavní lišta:** Save, Undo/Redo, Add (box, nepřítel, entita, glTF), Play / Pause (F6) / Stop (Shift+F5) a kvalita viewportu. Hra běží jako podproces editoru a její výstup jde do Output Logu.
- **Řádek režimů:** Select, Place a Model (Shift+1..3) přepínají levý panel. Vedle jsou Focus, To floor, Duplicate a Delete.
- **Levý panel Model:** nástroje sítě, Modify Texture a Active Material. Funkční jsou Delete, Duplicate, Align, Scale, Shift, Rotate, Fit, Justify a číselné pole posunu, měřítka a rotace textury boxu (ukládá se jako řádek `  uv` v levelu). Apply Material dá aktivní materiál na výběr. Extrude, Inset, Bevel a další nástroje sítě čekají na editovatelné meshe.
- **Lišta viewportu:** menu voleb, pohled (perspektiva, top, front, side, čtyři pohledy), režim Lit / Unlit / Lighting only / Wireframe (Alt+1..4), nástroje Q W E R, přichytávání posunu, úhlu a měřítka (každé jde vypnout) a rychlost kamery. Ortho pohledy jsou v Lit režimu drátové (vypíná se v menu voleb).
- **Mřížka a měřítko:** metrová mřížka (1 m, každých 10 m výraznější, osy X červeně a Z modře) se přepíná klávesou G. V menu voleb viewportu je i referenční člověk 1,8 m u startu hráče. Oko hráče je ve výšce 1,7 m (`assets/SCALE.md`).
- **LOD:** model v Details má v sekci Rendering položky LOD model a LOD distance. Při vložení `foo.glb` se `foo_lod1.glb` ze stejné složky připojí sám. V levelu je to řádek `  lod "cesta" vzdálenost`.
- **Plovoucí Transform:** Location, Rotation a Scale/Size výběru. S **Delta Mode** se zadaná hodnota přičte (měřítko vynásobí) a pole se vynuluje.
- **Outliner:** hledání, sloupce Item Label a Type, Add menu a počet objektů.
- **Details:** jméno výběru, Add (materiál, skript, model postavy), hledání a kategorie General, Transform, Rendering, Character, Script, Level a All. Řádky, které pro výběr neplatí, jsou skryté.
- **Content Browser:** strom složek s Favorites (pravé tlačítko na složce), drobečková cesta, Add (složka, materiál, skript, level), Import (kopie souborů do složky) a Save All.
- **Stavový řádek:** Content Drawer, Output Log a příkazový řádek Cmd (`help` vypíše příkazy, například `view quad`, `shading wireframe`, `quality 1`, `r.ssao 0`, `select Jméno`, `stat`).

## Co se nastavuje v editoru

Všechno, co hra používá, se mění v editoru a ukládá do souborů úrovně nebo assetů:

| Co | Kde v editoru | Uloženo v |
| --- | --- | --- |
| Geometrie, modely, entity, skripty | Outliner + Properties | `.level` |
| Model postavy, měřítko, natočení, výchozí animace | Properties > Character model, Animation | `.level` |
| Lua skripty | Záložky script editoru (Edit script, menu Script) | `scripts/*.lua` |
| Materiály, materiály boxů a slotů modelů | Material editor (Ctrl+M), Properties > Material | `materials/*.material`, `.level` |
| Nastavení a úpravy textur | Texture editor | `textures/*.png.texture` |
| Vertex paint modelů (mech, bláto, vlhkost) | Režim Paint (Shift+6) | `.level` |
| Parametry AI a dalších skriptů | Properties > Script parameters (deklaruje je skript v `properties`) | `.level` (po entitách) |
| Pohyb a zdraví hráče, start, slunce | Properties > Level | `.level` |
| Výzbroj hráče a parametry zbraní | Záložka Weapons | `.level` + `weapons/*.weapon` |

## Hierarchie a vazby

- **Hierarchy** (vlevo): skupiny a objekty jako strom. Přetažením na jiný objekt se z něj stane potomek,
  přetažením mezi položky sourozenec, do prázdna kořen. F2 přejmenuje, pravé tlačítko vytvoří nebo zruší skupinu.
  Posunutí rodiče posune i potomky. Jména jsou unikátní, protože potomek odkazuje na rodiče jménem.
- **Relations** (pod ní): u vybraného objektu rodič a potomci, soubory, které používá (model, skript, postava,
  materiály a jejich textury), a které další objekty používají stejný soubor. Dvojklik otevře materiál,
  texturu nebo skript v editoru, případně vybere objekt.
- V `.level` (verze 5) jsou řádky `group "jméno" "rodič"` a pod objektem `  parent "jméno"`.

## Zbraně

Každá zbraň je soubor `.weapon` (typ střelná / chladná, model, poškození, kadence, automatická/poloautomatická,
broky, zásobník, přebíjení, rozptyl, dostřel, poloha, natočení a měřítko v ruce, poloha záblesku). Výzbroj
testovací úrovně: 1 AK-74M, 2 pistole, 3 nůž, 4 samopal.

- **Nůž** (`type melee`): bez nábojů a přebíjení, `fire_interval` je doba seku, `spread` šířka oblouku
  a `range` dosah (1,8 m). Sek trefí nejbližšího nepřítele v oblouku i vedle zdi.
- **Animované ruce**: když je model zbraně glTF s kostrou (ruce + zbraň), vyberou se v editoru klipy
  `anim_idle`, `anim_fire`, `anim_reload` a `anim_draw`. Přebíjení a sek se natáhnou na nastavený čas.
  Bez klipů se zbraň hýbe procedurálně (houpání při chůzi, zpětný ráz, přebití, sek).
  Volitelně `anim_reload_empty` + `reload_empty_time` (přebití z prázdného zásobníku s natažením závěru)
  a `anim_walk` (smyčka při chůzi).

AK-74M má profesionální ruce a animace od Cranshe (CC-BY 4.0, kredit v `assets/models/weapons/fps/CREDITS.md`,
hra ho vypisuje při startu). Nůž, pistole a samopal mají ruce v pohledu z první osoby (`assets/models/weapons/fps/`, CC0, viz README a
CREDITS tamtéž) s klipy idle, výstřel/sek, přebití a tasení.

## Render

Jeden renderer pro hru, viewport editoru, náhledy materiálů i miniatury: HDR, kaskádové stíny od slunce,
depth pre-pass, SSAO, PBR (GGX) s osvětlením z procedurální oblohy (IBL: předfiltrovaná cubemapa + BRDF LUT),
výšková mlha se září ke slunci, bloom, ACES, barevné ladění (saturace, kontrast, tón), viněta, zrno a FXAA.

- **Prostředí** (dok *Environment*): slunce, obloha, oblačnost, mlha, expozice, bloom, AO, vzdálenost stínů
  a barevné ladění. Ukládá se do levelu (řádky `env klíč hodnoty`). Presety: jasný den, zatažená Zóna
  (post-apo), soumrak, toxický opar, noc.
- **Menu Render**: kvalita (nízká/střední/vysoká/ultra) a přepínače SSAO, bloom, FXAA; ukládá se do nastavení editoru.
- **Shadery** jsou soubory v `engine/shaders/` (zabudované při buildu). *Render → Shader editor* je otevře
  se zvýrazněním GLSL; *Apply* (Ctrl+S) je uloží a hned přeloží. Chyba se označí na řádku a běží dál
  předchozí shadery. *Play level* předá upravené shadery hře (`fps_game level --shaders DIR`).

## Paměť a textury

- **Komprese textur:** při prvním načtení se každá textura „uvaří“: mipmapy (u barev v lineárním světle)
  a GPU komprese BC1 (barvy), BC3 (s alfou), BC5 (normal mapy, shader dopočítá Z). Paměť textury klesne
  na 1/4 až 1/8. Výsledek se uloží do cache (`~/.cache/fps_engine/textures`, na Windows
  `%LOCALAPPDATA%\fps_engine\cache\textures`), takže další načtení přeskočí dekódování PNG/JPEG i kompresi.
- **Streamování:** objekty dál než vzdálenost streamování mají na GPU jen malou kopii (max. 256 px).
  Plné rozlišení se načte z cache na pracovním vlákně, když se přiblížíš; po 5 s v dálce se zase uvolní.
  CPU kopie pixelů se po nahrání na GPU zahazují.
- **Editor:** textura má v *Texture editoru* sekci *Memory* (komprese zap./vyp., max. velikost). Render →
  *Texture size* omezí velikost všech textur. Dok **Memory** ukazuje GPU (textury, meshe, render targety)
  i CPU (meshe, pixely, celý proces). Kvalita Low/Medium omezuje textury na 1024/2048 px.
- **Průhlednost:** glTF `alphaMode` MASK (listí, ploty, mříže: díry i ve stínech), BLEND (sklo, kreslí se
  po neprůhledné scéně odzadu dopředu) a `doubleSided`. Totéž pro vlastní materiály v Material editoru
  (Alpha, Alpha cutoff, Opacity, Double-sided; řádky `alpha_mode`, `alpha_cutoff`, `opacity`, `double_sided`).
- **Cull distance a kolize:** model v Details > Rendering má *Cull distance* (dál od kamery se nekreslí,
  0 = vždy) a *Physics: Collision* (vypnout pro trávu, listí, drobnou suť). V levelu řádky
  `  cull_distance 60` a `  collision 0` pod řádkem `model`. Kopie jednoho modelu sdílí i kolizní mesh.
- **Instancing:** viditelné kopie stejného modelu (stejné materiály) se kreslí jedním draw callem na díl
  meshe. Vybraný objekt a animované postavy jdou zvlášť. `FPS_ENGINE_STATS=1 fps_game` vypisuje FPS,
  draw cally a počet instancí; `FPS_ENGINE_NO_INSTANCING=1` ho vypne pro porovnání. V editoru příkaz `stat`.
- **Stíny:** vzdálené kaskády se překreslují jen každý 2. / 4. snímek (dřív, když kamera opustí jejich
  okraj nebo se pohne slunce), kreslí LOD a vynechávají malé vrhače. *Details > Rendering > Shadows: Cast
  shadows* (v levelu `  cast_shadows 0`) vypne stín modelu, např. trávě; u vegetace poslední pole
  `foliage_type`. Alpha-test textury (MASK) mají mipmapy zachovávající pokrytí, tráva v dálce nemizí.
- **Velké levely:** modely jsou v prostorové mřížce (32m buňky), culling testuje nejdřív buňky.
  Occlusion culling přeskočí, co je schované za bližší geometrií (zmenšená hloubka z předchozího snímku,
  čtená bez čekání na GPU). Nízké detaily s cull distance a bez kolize (tráva, plevel) ke konci
  dosahu řídnou a stín vrhají jen zblízka. Presety Low/Medium zkracují LOD vzdálenosti.
- **Build:** bez `-DCMAKE_BUILD_TYPE` se překládá Release; debug build je pro velké levely 10–50× pomalejší.
- **Profilování:** dok *Frame* v editoru ukazuje CPU a GPU čas (timer queries) každého průchodu, draw
  cally, trojúhelníky a instancing; přepínače instancingu a cache stínů pro porovnání. Ve hře
  `FPS_ENGINE_STATS=1` vypisuje totéž do konzole.
- **Sdílené meshe:** kopie jednoho modelu (např. 18 dílů plotu) sdílí vrcholy v RAM i v historii undo.
- Testovací level: načtení 21,0 s → 2,3 s (s teplou cache), paměť hry 4,4 GB → 0,54 GB (llvmpipe, kde je
  i „VRAM“ v RAM).

## Modely z Blenderu

File > Export > glTF 2.0 (`.glb` nebo `.gltf`), Y nahoru (výchozí). V editoru Ctrl+I.
Model je zároveň statická kolize (Jolt MeshShape), takže po něm jde chodit. Barva se bere z Base Color materiálu.

## Stav

Hotovo: hráč (Jolt CharacterVirtual), glTF modely s texturami, entity s Lua skripty, AI nepřátel,
zbraně se střelbou a zdravím nepřátel, animované postavy (skeletal animace, GPU skinning), script editor,
PBR materiály a textury s editory, editor pro všechny parametry, zvuk (miniaudio: 3D zvuky,
`.sound` assety, Sound editor, ambientní zóny, Lua `game.play_sound`).
Další: nové modely a textury, nůž/samopal/pistole, hierarchie scény, animation editor, environment editor.
