# Roadmapa enginu

Stav k 2026-10-31 (větev `engine`, commit `a0f515b`). Živá verze s komentáři:
https://claude.ai/artifact/VUAGLWrtDXKEaPskTdDCTQ

Cíl: engine pro jednoho hráče, FPS v postapokalyptické zóně (žánr S.T.A.L.K.E.R., vlastní obsah),
s editorem, ve kterém jde udělat celou hru. Vzorem je rozdělení enginu v Unrealu a Unity na
subsystémy, ale bereme jen to, co taková hra opravdu potřebuje. Síťová hra, konzole, mobily
a filmové nástroje (Sequencer/Timeline v plné šíři) sem nepatří.

Pravidla, která platí pro každý bod:

- **Editor první:** každá nová funkce má v téže změně i UI v editoru (data v souborech levelu/assetu).
- **3rdparty beze změn:** nové knihovny (miniaudio, Recast/Detour, …) jdou do `3rdparty/` v původní podobě.
- **Vícevláknově od začátku:** načítání, příprava snímku, audio a stavba navmeshe běží mimo hlavní vlákno, UI editoru se nikdy neblokuje.
- **Měřítko 1 jednotka = 1 m**, člověk 1,8 m (`assets/SCALE.md`).
- **Windows, Linux, FreeBSD:** CI musí být zelené na všech třech.

Priority: **P0** blokuje hratelnou hru · **P1** potřeba pro hru v zóně · **P2** kvalita a pohodlí · **P3** později / jen když bude čas.
Odhady jsou v týdnech soustředěné práce (1 vývojář + Claude), hrubé.

## 1. Audit: Unreal / Unity vs. náš engine

| Oblast | Co nabízí Unreal / Unity | Co máme teď | Co chybí (pro naši hru) | Prio | Milník |
| --- | --- | --- | --- | --- | --- |
| Rendering | Deferred/forward+, Nanite, virtual textures, RHI (DX12/Vulkan/Metal) / URP, HDRP, SRP batcher | Forward HDR renderer v OpenGL 4.6, depth pre-pass, frustum culling, distance LOD (1 úroveň), FXAA, presety kvality, jeden renderer pro hru i editor | Instancing a indirect draw, occlusion culling, alpha mask + oboustranné materiály, TAA, více LOD úrovní, impostory, GPU timery | P0 | M0 |
| Světla a GI | Lumen, lightmapy, reflection probes, volumetrika / Enlighten, APV, light probes | Slunce s CSM (3 kaskády), IBL z procedurální oblohy, SSAO, výšková mlha | Bodová a reflektorová světla (clustered forward), stíny lokálních světel, reflection probes v interiérech, baked GI/probe grid, volumetrická mlha, světla v editoru | P0 / P1 | M1 / M3 |
| Materiály a shadery | Material Editor (graf), instance materiálů / Shader Graph | PBR metal/roughness, `.material` soubory, Material + Texture editor, živý Shader editor s rekompilací | Instance materiálu s parametry, decaly (krev, díry po kulkách – atlas už je), vertex paint/blend vrstev (mech, rez, špína), detail mapy, permutace shaderů, později jednoduchý graf | P1 | M1 / M3 |
| Svět, terén, vegetace | Landscape (sculpt/paint), Foliage tool, PCG / Terrain, Terrain tools | Level z modelů a boxů, hierarchie, skupiny, mřížka, referenční člověk, vegetace jako jednotlivé modely | Terén z heightmapy (sculpt, malování vrstev v editoru), štětec na vegetaci s instancingem, scatter podle pravidel, voda (řeka) | P1 | M2 |
| Streamování světa | World Partition, HLOD, Level streaming / Addressables, additive scenes | Celý level se načte najednou na pozadí s progressbarem, paralelní čtení souborů, GPU preload | Buňky světa a jejich načítání podle vzdálenosti, HLOD/impostory budov, streamování textur, rozpočet paměti, přechody mezi lokacemi zóny | P1 | M2 |
| Fyzika | Chaos (destrukce, vozidla, cloth) / PhysX | Jolt: statické kolize z meshů, character controller (schody, svahy), raycasty pro zbraně a AI | Dynamická tělesa v levelu (sudy, bedny), ragdolly, fyzikální materiály (zvuk/částice podle povrchu), triggery a kolizní vrstvy v editoru, příznak "bez kolize" | P1 | M1 / M3 |
| Animace | Anim Blueprint, Control Rig, IK, retargeting / Mecanim, Animation Rigging | glTF skeletální animace, prolínání klipů, retargetované lidské animace, FP ruce se zbraněmi (Cransh rigy), přehrávání z Lua | Stavový automat a blend space (chůze/běh/míření podle rychlosti a směru), maskování vrstev (horní/dolní tělo), IK nohou a ruky na zbrani, události v klipech (kroky, výměna zásobníku), Animation editor | P1 | M3 |
| AI a navigace | Behavior Trees, EQS, NavMesh, Perception / NavMesh, ML-Agents | Lua AI (hlídka, pronásledování, střelba, hledání), zorné pole + `can_see` přes raycast | Navmesh (Recast/Detour) se stavbou v editoru a na pozadí, hledání cesty, krytí (cover points), sluch, frakce, skupiny, vizuální debug v editoru | P0 | M1 |
| Audio | MetaSounds, Sound Cues, atenuace, reverb zóny / AudioMixer | miniaudio (`3rdparty/miniaudio`): 3D zvuky, async dekódování, streamování, skupiny mixéru, `.sound` assety, Sound editor, zóny | Zbývá: ducking, zvuk kroků podle povrchu, reverb zóny, okluze zvuku; původně plán: zvuk na vlastním vlákně, 3D zvuky, streamování hudby, mixér (kanály, ducking), zvuk zbraní a kroků podle povrchu, ambientní a reverb zóny, Sound editor | P0 | M1 |
| VFX | Niagara / VFX Graph, Shuriken | Záblesk u hlavně, procedurální atlas děr po kulkách a nábojnice (zatím nevykreslené ve hře) | CPU/GPU částice (prach, kouř, jiskry, krev, oheň), decaly, VFX editor s živým náhledem, anomálie jako efekt | P1 | M1 / M3 |
| UI a HUD | UMG, Common UI / UI Toolkit, uGUI | `Ui2D` (text přes stb_truetype, obdélníky, obrázky), načítací obrazovka, minimapa a PDA mapa, zaměřovač | Hlavní menu, pauza, nastavení (grafika, ovládání, zvuk), HUD (zdraví, munice, výdrž), inventář, dialogy, credits (povinné kvůli CC-BY), UI editor/layout v souborech | P0 | M1 |
| Skriptování a gameplay | Blueprints, C++ gameplay framework, Gameplay Ability System / C#, MonoBehaviour | Lua 5.4 + sol2, `on_start`/`on_update`, vlastnosti z editoru, Script editor s kontrolou syntaxe a doplňováním, zbraně v `.weapon` | Události a zprávy mezi entitami, triggery, časovače, uložení/načtení hry, inventář a předměty, úkoly, dialogy, frakce, debugger skriptů (breakpointy), hot reload | P0 | M1 / M4 |
| Síť | Replikace, dedicated server / Netcode for GameObjects | Nic | Nepotřebujeme (hra pro jednoho hráče). Jen architektura bez globálních singletonů, aby šlo později přidat co-op | P3 | – |
| Asset pipeline a cooking | Import, DDC, cooking, pak soubory / AssetDatabase, Addressables, AssetBundles | glTF import (cgltf), stb_image, `.texture` nastavení vedle obrázků, Asset browser, Git LFS | Komprese textur (BC1/3/4/5/7) s mipmapami a diskovou cache, deduplikace meshů, cooking do binárních formátů, balení do archivů, sledování změn souborů a reimport, závislosti a hledání nepoužitých assetů | P0 | M0 / M5 |
| Profilování a nástroje | Unreal Insights, stat příkazy, GPU Visualizer / Profiler, Frame Debugger, Memory Profiler | Žádný profiler; jen Render quality menu a presety kvality | CPU zóny + GPU timer queries, panel snímku a paměti (VRAM/RAM) v editoru, konzole s příkazy (`stat fps`, `r.*`), záznam do souboru, debug kreslení (čáry, boxy) z C++ i Lua | P0 | M0 |
| Editor | Level editor, Details, Outliner, Content Browser, PIE, modelovací režim | Qt 6 editor ve stylu Scythe: docky, gizmo (posun/rotace), snapping, undo/redo, 4 pohledy, hierarchie, Asset browser, Play (F5), editory skriptů, materiálů, textur, shaderů, zbraní, prostředí | Play in editor přímo ve viewportu, multi-select a prefaby, modelování v editoru (boxy, extrude, UV), editory: animace, zvuk, VFX, terén/vegetace, navmesh, úkoly/dialogy; validace levelu | P1 | průběžně |
| Build a balení | Packaging, launcher, crash reporter / Build Settings, Player | CMake, CI na Linux/Windows/FreeBSD, testy jádra (CTest) | Balíček hry (exe + pak soubory), instalátor/zip, crash dump a log, verze buildu, automatický smoke test levelu v CI | P2 | M5 |
| Platformy | Windows, konzole, mobily, XR / totéž + WebGL | Windows, Linux, FreeBSD (OpenGL 4.6) | Ovladač (gamepad přes GLFW), přemapování kláves, Steam Deck (Linux), volitelně Vulkan backend přes tenkou RHI vrstvu | P2 / P3 | M5 / – |

## 2. Milníky

Pořadí je dané závislostmi: bez měření a komprese nemá smysl stavět větší svět, bez zvuku,
navigace a menu není hra hratelná, bez streamování neuneseme celou zónu.

```
M0 Výkon a měření ──► M1 Hratelný řez ──► M2 Otevřená zóna ──► M4 Zóna žije
                              │                      │                 ▲
                              └──► M3 Kvalita obrazu a animací ────────┘
                                                                        └──► M5 Vydání
```

### M0 – Výkon, paměť a měření (probíhá, ~3–4 týdny)

Podrobně ve výkonové roadmapě vlákna "Vytvořit 3D FPS engine"; tady jen shrnutí, aby se nic nedělalo dvakrát.

- [x] Oprava sekání (textury mimo záběr se mazaly a nahrávaly znovu)
- [x] Alpha mask + oboustranné materiály (listí, mříže, ploty) + průhledné sklo (Blend, řazené odzadu)
- [x] Komprese textur BC1/BC3/BC5 s mipmapami, disková cache, paralelně; komprese a max. velikost v Texture editoru, Render → Texture size (BC7 zatím ne)
- [x] Uvolnění CPU kopií po nahrání, streamování textur podle vzdálenosti, deduplikace meshů (testovací level: načtení 21,0 s → 2,3 s, paměť 4,4 GB → 0,54 GB)
- [x] Panel paměti (VRAM/RAM) v editoru (dok Memory)
- [x] Panel snímku v editoru (dok Frame); CPU časy + GPU timer queries po průchodech, ve hře `FPS_ENGINE_STATS=1`
- [x] Příznak bez kolize a cull distance u modelů (+ jeden Jolt mesh sdílený všemi kopiemi modelu)
- [x] GPU instancing (kopie modelu jedním draw callem; hospital_street: 3391 → 1339 draw callů)
- [ ] `glMultiDrawElementsIndirect`, setřídění podle materiálu
- [x] Cache stínů vzdálených kaskád (2./4. snímek), LOD a vyřazení malých vrhačů ve vzdálených kaskádách, přepínač „Cast shadows“ u modelů a vegetace (hospital_street: stíny 236 → 93 ms GPU, snímek 621 → 510 ms na llvmpipe)
- [x] Alpha mipmapy zachovávající pokrytí pro MASK (tráva a listí nemizí v dálce)
- [x] Prostorová mřížka modelů (32m buňky se sjednocenými hranicemi, culling po buňkách), řídnutí trávy podle vzdálenosti, stíny detailů jen zblízka, lodScale v presetech (Skrunda-1: 7,9 → 4,4 M trojúhelníků)
- [x] Release jako výchozí build type (build bez optimalizací je 10–50× pomalejší)
- [x] Occlusion culling: max-depth kopie depth pre-passu (160 px), asynchronní readback přes PBO + fence, CPU test buněk a modelů; přepínač v doku Frame, `FPS_ENGINE_NO_OCCLUSION=1`
- [x] Víc LOD úrovní na model (`lod` víckrát s rostoucí vzdáleností; v editoru Details > Far LODs)
- [ ] Impostory pro vzdálené stromy
- [ ] Data entit a modelů v souvislých polích (ECS-like: transformace, hranice a flagy v SoA), paralelní update přes JobSystem
- [ ] Snímek jako graf úloh v JobSystemu (animace, culling, skripty paralelně), pak render vlákno

Hotovo, když: zóna (`zone.level`) běží na střední kartě nad 60 FPS na High, editor ukazuje čas snímku a paměť, žádné záseky při chůzi.

### M1 – Hratelný vertikální řez (~8–10 týdnů)

Jedna lokace zóny od menu po smrt/uložení, se zvukem a chytřejšími nepřáteli.

- [x] **Audio** (2 týdny): ~~OpenAL Soft~~ miniaudio do `3rdparty/` (public domain / MIT-0 místo LGPL, jeden header, backendy pro Windows/Linux/FreeBSD, vestavěné dekodéry), mixování na vlákně zařízení, dekódování na job vlákně, 3D zdroje, streamovaná hudba, skupiny mixéru (sfx, music, ambient, ui); zvuky zbraní (výstřel, přebití, tasení, dopad, zásah), kroků, dopadu a zranění; entita "sound" (3D smyčka nebo ambientní zóna s fade) v editoru; Lua `game.play_sound`. Sound editor (variace, náhled i ze vzdálenosti, hlasitost, náhodný pitch, dosah, loop/stream, skupina). Placeholder zvuky syntetizované (`assets/tools/make_sounds.py`).
- [ ] Audio dál: hlasitosti skupin v menu hry, ducking, kroky podle povrchu (materiál), reverb zóny, okluze zvuku geometrií.
- [ ] **Lokální světla** (1,5 týdne): bodová a reflektorová světla, clustered forward, stíny pro pár nejbližších; světla jako objekty v editoru s gizmem dosahu; blikající zářivky.
- [ ] **Navigace** (2 týdny): Recast/Detour do `3rdparty/`, stavba navmeshe na pozadí z kolizí levelu, zobrazení a nastavení v editoru, `self:move_to()` v Lua, krycí body.
- [ ] **UI hry** (1,5 týdne): hlavní menu, pauza, nastavení (grafika, myš, klávesy, hlasitost), HUD (zdraví, munice), credits s CC-BY autory, obrazovka smrti.
- [ ] **Uložení hry** (1 týden): serializace stavu entit, hráče a skriptů (Lua tabulka `save`), quick save/load.
- [ ] **Předměty a inventář, základ** (1 týden): pickupy (munice, lékárnička), jednoduchý inventář zbraní.
- [ ] **Decaly a částice, základ** (1,5 týdne): díry po kulkách a krev z existujícího atlasu, prach a jiskry při zásahu, kouř z hlavně; nastavení v `.weapon` a fyzikálním materiálu.
- [ ] **Fyzika v levelu** (1 týden): dynamická tělesa (sudy, bedny), fyzikální materiály povrchů (zvuk kroků, decal, částice), triggery v editoru.

Hotovo, když: hra se spustí do menu, jde projít lokaci, nepřátelé obcházejí překážky, vše zní, hra jde uložit a načíst.

### M2 – Otevřená zóna (~8 týdnů)

- [ ] **Terén** (3 týdny): heightmapa po dlaždicích, sculpt a malování až 8 vrstev materiálu v editoru, kolize v Jolt (HeightFieldShape), LOD terénu.
  Hotovo: jedna heightmapa, režim Landscape (raise/lower/smooth/flatten, malování 4 vrstev), kolize HeightFieldShape. Zbývá: dlaždice, 8 vrstev, LOD.
- [ ] **Vegetace** (2 týdny): štětec na stromy, keře a trávu (instancing z M0), hustota podle vrstev terénu, vítr ve vertex shaderu, cull distance a fade.
  Hotovo: režim Foliage (štětec, typy s hustotou, měřítkem, cull distance, kolizí, náklonem, stíny), instancing po 32m buňkách, kolize stromů. LOD úrovně foliage (`foliage_lod`) a řídnutí do dálky (far density). Zbývá: hustota podle vrstev, vítr, fade.
- [ ] **Streamování světa** (3 týdny): rozdělení levelu na buňky (např. 128 × 128 m), načítání a uvolňování podle vzdálenosti na pracovních vláknech, HLOD/impostory budov, editor zobrazí buňky a umí je zamknout/načíst; rozpočet paměti.
- [ ] **Voda** (1 týden): řeka a louže (odrazy SSR nebo probe, průhlednost, mlha pod hladinou).
- [ ] **Počasí a denní doba** (1 týden): animované slunce, déšť, mlha a bouřka jako přechody mezi presety Environment; editor časové osy dne.

Hotovo, když: zóna 1 × 1 km jde projít bez načítací obrazovky a paměť drží rozpočet.

### M3 – Kvalita obrazu a animací (~6–8 týdnů, souběžně s M2)

- [ ] **Animace** (3 týdny): stavový automat s přechody, blend space 1D/2D, vrstvy s maskou kostí, IK nohou na terénu a levé ruky na zbrani, události v klipech; **Animation editor** (časová osa, náhled, přechody jako graf).
- [ ] **Ragdolly** (1 týden): Jolt ragdoll ze skeletonu, přechod ze smrti; nastavení kloubů v editoru.
- [ ] **Osvětlení** (2 týdny): reflection probes s ručním umístěním a bake v editoru, irradiance probe grid pro interiéry, volumetrická mlha a světelné kužele.
- [ ] **Obraz** (1 týden): TAA (místo FXAA na High), motion blur volitelně, kvalitnější SSAO (GTAO), SSR.
- [ ] **Materiály** (1–2 týdny): instance materiálu, vertex paint a blend vrstev, detail mapy; **VFX editor** pro částice z M1.

### M4 – Zóna žije: gameplay systémy (~8–10 týdnů)

- [ ] Frakce a vztahy (stalkeři, bandité, vojáci), skupiny NPC, neutrální NPC v táboře.
- [ ] Simulace mimo dohled hráče (zjednodušená A-life: skupiny se přesouvají mezi body zájmu na mapě, i když nejsou načtené).
- [ ] Úkoly a dialogy: data v souborech, editor dialogů jako strom, PDA s úkoly (navazuje na mapu).
- [ ] Obchod, peníze, opotřebení zbraní a zbroje, váha inventáře.
- [ ] Anomálie a artefakty (vlastní design): trigger + VFX + zvuk + poškození, detektor.
- [ ] Debugger Lua (breakpointy, proměnné) a hot reload skriptů za běhu.

### M5 – Vydání (~3–4 týdny)

- [ ] Cooking: binární meshe, komprimované textury, balení do archivů, kontrola chybějících a nepoužitých assetů.
- [ ] Balíček pro Windows/Linux/FreeBSD, crash dump a log, verze buildu, automatický smoke test levelu v CI.
- [ ] Gamepad, přemapování ovládání, lokalizace textů (čeština, angličtina).
- [ ] Credits a licence všech assetů ve hře.

## 3. Co záměrně neděláme

| Funkce z UE/Unity | Proč ne |
| --- | --- |
| Síťová hra, replikace | Hra je pro jednoho hráče. Jen držíme architekturu bez globálního stavu. |
| Nanite / virtuální geometrie, Lumen | Na OpenGL a v jednom člověku nereálné; LOD, impostory a probes stačí. |
| Konzole, mobily, XR, WebGL | Cílové platformy jsou Windows, Linux, FreeBSD. |
| Vizuální skriptování (Blueprints) | Lua + Script editor stačí; gameplay data jsou v editoru jako vlastnosti. |
| Plný Sequencer/Timeline | Případně jednoduché skriptované scény přes Lua a animace kamery. |
| Vulkan/DX12 hned | Až bude OpenGL limitovat; před tím jen tenká vrstva nad GL voláními v Rendereru. |

## 4. Závislosti mezi oblastmi

- Instancing (M0) je podmínkou pro vegetaci (M2) a hustá města.
- Komprese a streamování textur (M0) jsou podmínkou pro streamování světa (M2).
- Panel snímku a paměti (M0) je potřeba pro každé další rozhodnutí o výkonu.
- Navmesh (M1) je podmínkou pro frakce, krytí a A-life (M4); terén (M2) vyžaduje přestavbu navmeshe po dlaždicích.
- Audio (M1) a fyzikální materiály (M1) jsou podmínkou pro anomálie (M4).
- Stavový automat animací (M3) je potřeba pro NPC, která mají chodit, mířit a krýt se zároveň (M4).
- Uložení hry (M1) musí od začátku počítat se streamovanými buňkami (M2) a úkoly (M4): ukládá se po entitách s trvalým ID.

## 5. Nejbližší kroky

1. Dokončit M0 ve vlákně "Vytvořit 3D FPS engine" (komprese textur, paměť, instancing, profiler).
2. Hned potom M1 v pořadí: audio → navmesh → UI hry a credits → uložení hry → lokální světla → decaly/částice → fyzika v levelu.
3. Souběžně s M1 může běžet obsah (assety, level zóny) v ostatních vláknech.
