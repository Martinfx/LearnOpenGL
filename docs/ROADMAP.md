# Roadmapa enginu

Stav k 2026-10-31 (větev `engine`, commit `a0f515b`; doplněno o průzkum základů enginu, viz [`ENGINE_RESEARCH.md`](ENGINE_RESEARCH.md)). Živá verze s komentáři:
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
| Audio | MetaSounds, Sound Cues, atenuace, reverb zóny / AudioMixer | OpenAL Soft (`3rdparty/openal-soft`, HRTF, EFX reverb) pro přehrávání, miniaudio jen dekodér: 3D zvuky, async dekódování, streamování, skupiny mixéru, `.sound` assety, Sound editor, zóny | Zbývá: ducking, reverb zóny, okluze zvuku (kroky podle povrchu a počasí hotové); původně plán: zvuk na vlastním vlákně, 3D zvuky, streamování hudby, mixér (kanály, ducking), zvuk zbraní a kroků podle povrchu, ambientní a reverb zóny, Sound editor | P0 | M1 |
| VFX | Niagara / VFX Graph, Shuriken | Částicový systém (`.particle`, flipbook, additive/lit), Particle editor s náhledem, díry po kulkách podle povrchu, nábojnice s fyzikou odrazů | CPU/GPU částice (prach, kouř, jiskry, krev, oheň), decaly, VFX editor s živým náhledem, anomálie jako efekt | P1 | M1 / M3 |
| UI a HUD | UMG, Common UI / UI Toolkit, uGUI | `Ui2D` (text přes stb_truetype, obdélníky, obrázky), načítací obrazovka, minimapa a PDA mapa, zaměřovač | Hlavní menu, pauza, nastavení (grafika, ovládání, zvuk), HUD (zdraví, munice, výdrž), inventář, dialogy, credits (povinné kvůli CC-BY), UI editor/layout v souborech | P0 | M1 |
| Skriptování a gameplay | Blueprints, C++ gameplay framework, Gameplay Ability System / C#, MonoBehaviour | Lua 5.4 + sol2, `on_start`/`on_update`, vlastnosti z editoru, Script editor s kontrolou syntaxe a doplňováním, zbraně v `.weapon` | Události a zprávy mezi entitami, triggery, časovače, uložení/načtení hry, inventář a předměty, úkoly, dialogy, frakce, debugger skriptů (breakpointy), hot reload | P0 | M1 / M4 |
| Síť | Replikace, dedicated server / Netcode for GameObjects | Nic | Nepotřebujeme (hra pro jednoho hráče). Jen architektura bez globálních singletonů, aby šlo později přidat co-op | P3 | – |
| Asset pipeline a cooking | Import, DDC, cooking, pak soubory / AssetDatabase, Addressables, AssetBundles | glTF import (cgltf), stb_image, `.texture` nastavení vedle obrázků, Asset browser, Git LFS | Komprese textur (BC1/3/4/5/7) s mipmapami a diskovou cache, deduplikace meshů, cooking do binárních formátů, balení do archivů, sledování změn souborů a reimport, závislosti a hledání nepoužitých assetů | P0 | M0 / M5 |
| Profilování a nástroje | Unreal Insights, stat příkazy, GPU Visualizer / Profiler, Frame Debugger, Memory Profiler | Žádný profiler; jen Render quality menu a presety kvality | CPU zóny + GPU timer queries, panel snímku a paměti (VRAM/RAM) v editoru, konzole s příkazy (`stat fps`, `r.*`), záznam do souboru, debug kreslení (čáry, boxy) z C++ i Lua | P0 | M0 |
| Editor | Level editor, Details, Outliner, Content Browser, PIE, modelovací režim | Qt 6 editor ve stylu Scythe: docky, gizmo (posun/rotace), snapping, undo/redo, 4 pohledy, hierarchie, Asset browser, Play (F5), editory skriptů, materiálů, textur, shaderů, zbraní, prostředí | Play in editor přímo ve viewportu, multi-select a prefaby, modelování v editoru (boxy, extrude, UV), editory: animace, zvuk, VFX, terén/vegetace, navmesh, úkoly/dialogy; validace levelu | P1 | průběžně |
| Build a balení | Packaging, launcher, crash reporter / Build Settings, Player | CMake, CI na Linux/Windows/FreeBSD, testy jádra (CTest) | Balíček hry (exe + pak soubory), instalátor/zip, crash dump a log, verze buildu, automatický smoke test levelu v CI | P2 | M5 |
| Platformy | Windows, konzole, mobily, XR / totéž + WebGL | Windows, Linux, FreeBSD (OpenGL 4.6) | Ovladač (gamepad přes GLFW), přemapování kláves, Steam Deck (Linux), volitelně Vulkan backend přes tenkou RHI vrstvu | P2 / P3 | M5 / – |

## 1b. Doplněk auditu: základy, které profi engine řeší skrytě (2026-10-31)

Podrobný průzkum (Unreal, Unity, Godot, X-Ray, id Tech, Source 2) je v [`ENGINE_RESEARCH.md`](ENGINE_RESEARCH.md).
Funkce výše jsou pokryté; chyběla hlavně „neviditelná“ infrastruktura:

| Oblast | Co máme | Co chybí | Prio | Milník |
| --- | --- | --- | --- | --- |
| Logování a asserty | `fprintf(stderr)`, Output log v editoru | Úrovně a kategorie, soubor `logs/`, kruhový buffer, `ENGINE_CHECK/ENSURE/VERIFY` | P0 | M0.5 |
| Pády | nic | Crash handler (DbgHelp minidump / `backtrace` + `sigaltstack`), report s logem, verzí, GPU a levelem; obnova editoru | P0 | M0.5 |
| Konzole a cvars | pevné příkazy v editoru | Registr cvars s příznaky (config/dev/cheat), konzole ve hře `~`, příkazová řádka, Lua; presety kvality jako sady cvars | P0 | M0.5 |
| Konfigurace a cesty | nic | Uživatelské složky (`%APPDATA%`, XDG) pro nastavení, uložené hry, logy, cache; hierarchie výchozí → uživatel → CLI | P0 | M0.5 |
| Identita entit | jméno a pořadí v souboru | Trvalé GUID entit a assetů (save, streaming, odkazy, prefaby, A-Life) | P0 | M0.5 |
| Testy a QA | 25 unit testů v CTest | Smoke test všech levelů v CI (xvfb + llvmpipe), golden image testy, ASan/UBSan/TSan | P0 / P1 | M0.5 / M3 |
| Debug nástroje | Frame a Memory doky | Debug kreslení z C++/Lua, cheaty (`noclip`, `god`, `give`, `teleport`, `timescale`), GL debug labely a callback | P0 | M0.5 |
| Stavy hry | `game/main.cpp` jede rovnou do levelu | Boot → Menu → Loading → Playing ↔ Paused, přechody mezi lokacemi | P1 | M1 |
| Data a formáty | číslo verze v levelu/materiálu | Pravidlo migrace + test starých souborů; verze obsahu v uložené hře; atomický zápis | P1 | M1 |
| Hot reload a registry | ruční reload shaderů a materiálů | Sledování souborů a reload všech assetů; registry závislostí, validace, nepoužité assety | P1 | M2 |
| Profiler | časy průchodů + GPU timery | Timeline vláken (Tracy v `3rdparty/`) | P1 | M3 |
| Okno a platforma | `glfwSwapInterval(0)` natvrdo | VSync/limit FPS, fullscreen/borderless, monitor, DPI, render scale, `NvOptimusEnablement` | P1 | M1 |
| Editor a data | instance modelů | Prefaby s přepisy, propojení entit (entity I/O), datové třídy s dědičností (`.weapon` base), autosave levelu | P1 / P2 | M1 / M2 / M4 |
| Lokalizace a přístupnost | fonty s češtinou | Tabulky řetězců (cs, en, plurály), titulky, FOV, velikost textu | P2 | M5 |
| Výroba | CI na 3 OS | Verze z gitu, symboly ke každému vydání, VFS + `mods/`, licence 3rdparty, demo/benchmark | P2 | M4 / M5 |

## 2. Milníky

Pořadí je dané závislostmi: bez měření a komprese nemá smysl stavět větší svět, bez zvuku,
navigace a menu není hra hratelná, bez streamování neuneseme celou zónu.

```
M0 Výkon a měření ──► M0.5 Základy ──► M1 Hratelný řez ──► M2 Otevřená zóna ──► M4 Zóna žije
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

### M0.5 – Základy enginu (~3 týdny, hned po M0, může se prolínat se začátkem M1)

Infrastruktura z auditu 1b. Každá další funkce na ní staví, později by se dodělávala do hotového kódu.

- [ ] **Logování** (0,5 týdne): úrovně a kategorie, soubor v `logs/`, kruhový buffer, výstup do Output logu editoru, herní konzole a Lua `game.log`.
- [ ] **Asserty a crash handler** (1 týden): `ENGINE_CHECK/ENSURE/VERIFY`; při pádu stack trace (Windows minidump, Linux/FreeBSD `backtrace` + `sigaltstack`), posledních 500 řádků logu, verze buildu, GPU/driver, level; editor nabídne obnovu.
- [ ] **Cvars a konzole ve hře** (1 týden): registr s typem, popisem a příznaky, konzole `~` ve hře, `+cvar hodnota` z příkazové řádky, přístup z Lua, presety kvality jako sady cvars; editorová konzole na stejném registru.
- [ ] **Konfigurace a uživatelské složky** (0,5 týdne): `%APPDATA%` / XDG pro nastavení, uložené hry, logy a cache; výchozí → uživatelské → CLI.
- [ ] **GUID entit a assetů** (1 týden): trvalé 64bitové ID v levelu (verze 6 s migrací), zachované při kopírování/přesunu; podklad pro uložení, streaming, odkazy a prefaby.
- [ ] **Smoke test levelů v CI** (0,5 týdne, přesunuto z M5): každý level 300 snímků pod `xvfb-run`, bez GL chyb a assertů; noční job s ASan+UBSan a TSan.
- [ ] **Debug kreslení a cheaty** (0,5 týdne): čáry/boxy/koule/text z C++ i Lua s dobou trvání; `noclip`, `god`, `give`, `teleport`, `timescale`, volná kamera.
- [ ] **GL ladění** (0,5 týdne): `glDebugMessageCallback` do logu, `glObjectLabel` + `glPushDebugGroup` po průchodech (RenderDoc), `glProgramBinary` cache.

Hotovo, když: každý pád zanechá report s logem, nastavení přežije restart, CI spustí všechny levely a entity mají trvalé ID.

### M1 – Hratelný vertikální řez (~8–10 týdnů)

Jedna lokace zóny od menu po smrt/uložení, se zvukem a chytřejšími nepřáteli.

- [x] **Audio** (2 týdny): ~~OpenAL Soft~~ miniaudio do `3rdparty/` (public domain / MIT-0 místo LGPL, jeden header, backendy pro Windows/Linux/FreeBSD, vestavěné dekodéry), mixování na vlákně zařízení, dekódování na job vlákně, 3D zdroje, streamovaná hudba, skupiny mixéru (sfx, music, ambient, ui); zvuky zbraní (výstřel, přebití, tasení, dopad, zásah), kroků, dopadu a zranění; entita "sound" (3D smyčka nebo ambientní zóna s fade) v editoru; Lua `game.play_sound`. Sound editor (variace, náhled i ze vzdálenosti, hlasitost, náhodný pitch, dosah, loop/stream, skupina). Skutečné nahrávky (CC0/CC-BY, `assets/sounds/CREDITS.md`).
- [x] **OpenAL Soft** (2026-10-31): přehrávání přepsáno na OpenAL Soft 1.24.3 (`3rdparty/openal-soft`, LGPL, sdílená knihovna, přednost má systémový balík), HRTF, EFX reverb presety, loopback zařízení pro testy a stroje bez zvukovky; miniaudio zůstává jen jako dekodér WAV/OGG/MP3/FLAC.
- [x] **Sound editor 2** (2026-10-31): průběh vlny, graf útlumu v dB s tažitelnými min/max, 3D náhled (zdroj kolem posluchače, orbit, dozvuk); stereo soubory se ve 3D mixují do mona. Kroky, dopady a nábojnice podle povrchu (`surfaces.cfg`), déšť a vítr v Environment.
- [ ] Audio dál: hlasitosti skupin v menu hry, ducking, reverb zóny, okluze zvuku geometrií.
- [ ] **Lokální světla** (1,5 týdne): bodová a reflektorová světla, clustered forward, stíny pro pár nejbližších; světla jako objekty v editoru s gizmem dosahu; blikající zářivky.
- [x] **Navigace** (2 týdny, zbývají krycí body; zobrazení a nastavení v editoru hotové: Details > Level > Navigation, View > Navigation mesh `P`): Recast/Detour do `3rdparty/`, stavba navmeshe na pozadí z kolizí levelu, zobrazení a nastavení v editoru, `self:move_to()` v Lua, krycí body.
- [ ] **UI hry** (1,5 týdne): hlavní menu, pauza, nastavení (grafika, myš, klávesy, hlasitost), HUD (zdraví, munice), credits s CC-BY autory, obrazovka smrti.
- [ ] **Uložení hry** (1 týden): serializace stavu entit, hráče a skriptů (Lua tabulka `save`), quick save/load. Po entitách s GUID jako rozdíl proti levelu, atomický zápis (dočasný soubor + `rename`), kontrolní součet, verze obsahu, sloty, autosave.
- [ ] **Stavy hry a přechody** (1 týden): Boot → Menu → Loading → Playing ↔ Paused; pauza zastaví simulaci, ne UI; přechod mezi lokacemi s přenosem hráče a inventáře.
- [ ] **Okno a ovládání** (1 týden): VSync / limit FPS, fullscreen / borderless, výběr monitoru, render scale, `NvOptimusEnablement`; přemapování kláves z configu, citlivost a inverze myši, gamepad přes GLFW (přesunuto z M5).
- [ ] **Propojení entit** (1 týden): výstupy a vstupy jako v Hammeru (`OnTrigger` → `Open`), propojení klikem v editoru, volitelně z Lua.
- [ ] **Předměty a inventář, základ** (1 týden): pickupy (munice, lékárnička), jednoduchý inventář zbraní.
- [x] **Decaly a částice, základ** (1,5 týdne): díry po kulkách a krev z existujícího atlasu, prach a jiskry při zásahu, kouř z hlavně; nastavení v `.weapon` a fyzikálním materiálu. *Hotovo: `.particle` efekty, ParticleSystem na JobSystemu, DecalPool, nábojnice s odrazy, Particle editor s živým náhledem. 2026-10-31: odražené kulky (šance podle povrchu a úhlu, svištění, jiskry, poloviční poškození, nastavení v Assets > Surfaces), menší díry a oprava tečkovaného šumu (barva průhledných texelů při kompresi a mip mapách).*
- [ ] **Fyzika v levelu** (1 týden): dynamická tělesa (sudy, bedny), fyzikální materiály povrchů (zvuk kroků, decal, částice), triggery v editoru.

Hotovo, když: hra se spustí do menu, jde projít lokaci, nepřátelé obcházejí překážky, vše zní, hra jde uložit a načíst.

### M2 – Otevřená zóna (~8 týdnů)

- [ ] **Terén** (3 týdny): heightmapa po dlaždicích, sculpt a malování až 8 vrstev materiálu v editoru, kolize v Jolt (HeightFieldShape), LOD terénu.
  Hotovo: jedna heightmapa, režim Landscape (raise/lower/smooth/flatten, malování 4 vrstev), kolize HeightFieldShape. Zbývá: dlaždice, 8 vrstev, LOD.
- [ ] **Vegetace** (2 týdny): štětec na stromy, keře a trávu (instancing z M0), hustota podle vrstev terénu, vítr ve vertex shaderu, cull distance a fade.
  Hotovo: režim Foliage (štětec, typy s hustotou, měřítkem, cull distance, kolizí, náklonem, stíny), instancing po 32m buňkách, kolize stromů. LOD úrovně foliage (`foliage_lod`) a řídnutí do dálky (far density). Zbývá: hustota podle vrstev, vítr, fade.
- [ ] **Streamování světa** (3 týdny): rozdělení levelu na buňky (např. 128 × 128 m), načítání a uvolňování podle vzdálenosti na pracovních vláknech, HLOD/impostory budov, editor zobrazí buňky a umí je zamknout/načíst; rozpočet paměti.
- [ ] **Voda** (1 týden): řeka a louže (odrazy SSR nebo probe, průhlednost, mlha pod hladinou).
- [ ] **Střídání dne a noci** (1,5 týdne, přání martina 2026-10-31): slunce a měsíc po skutečné dráze podle zeměpisné šířky a data, nastavitelná délka dne; obloha s atmosférickým rozptylem (východ a západ slunce), noční obloha s hvězdami a měsícem; stíny, ambient, mlha a expozice podle času. Vše v Environment, v editoru posuvník času a přehrávání, v Lua `game.set_time`.
- [ ] **Počasí** (1 týden): déšť (hotový základ), mlha a bouřka jako přechody mezi presety Environment, blesky; editor časové osy dne.

- [ ] **Rozpočty paměti** (1 týden): limity po kategoriích (textury, meshe, audio, fyzika, Lua) s varováním v doku Memory, snímkový alokátor, pooly pro částice a decaly.
- [ ] **Asset registry a hot reload** (2 týdny): závislosti assetů podle GUID, přejmenování bez rozbití odkazů, validace (měřítko, chybějící textury) v editoru i CI, sledování souborů a reload textur, modelů, zvuků, `.weapon`, `.particle` a Lua za běhu.
- [ ] **Prefaby s přepisy** (1,5 týdne): skupina entit jako asset, kopie v levelu s přepsanými vlastnostmi, změna prefabu se propíše; autosave levelu v editoru.

Hotovo, když: zóna 1 × 1 km jde projít bez načítací obrazovky a paměť drží rozpočet.

### M3 – Kvalita obrazu a animací (~6–8 týdnů, souběžně s M2)

- [ ] **Animace** (3 týdny): stavový automat s přechody, blend space 1D/2D, vrstvy s maskou kostí, IK nohou na terénu a levé ruky na zbrani, události v klipech; **Animation editor** (časová osa, náhled, přechody jako graf).
- [ ] **Ragdolly** (1 týden): Jolt ragdoll ze skeletonu, přechod ze smrti; nastavení kloubů v editoru.
- [ ] **Osvětlení** (2 týdny): reflection probes s ručním umístěním a bake v editoru, irradiance probe grid pro interiéry, volumetrická mlha a světelné kužele.
- [ ] **Obraz** (1 týden): TAA (místo FXAA na High), motion blur volitelně, kvalitnější SSAO (GTAO), SSR.
- [ ] **Profiler a testy obrazu** (1 týden): Tracy (`3rdparty/`) s časovou osou vláken a GPU zónami; golden image testy rendereru v CI s tolerancí.
- [ ] **Materiály** (1–2 týdny): instance materiálu, vertex paint a blend vrstev, detail mapy; **VFX editor** pro částice z M1.

### M4 – Zóna žije: gameplay systémy (~8–10 týdnů)

- [ ] Frakce a vztahy (stalkeři, bandité, vojáci), skupiny NPC, neutrální NPC v táboře.
- [ ] Simulace mimo dohled hráče (zjednodušená A-life: skupiny se přesouvají mezi body zájmu na mapě, i když nejsou načtené).
- [ ] Úkoly a dialogy: data v souborech, editor dialogů jako strom, PDA s úkoly (navazuje na mapu).
- [ ] Obchod, peníze, opotřebení zbraní a zbroje, váha inventáře.
- [ ] Anomálie a artefakty (vlastní design): trigger + VFX + zvuk + poškození, detektor.
- [ ] Debugger Lua (breakpointy, proměnné) a hot reload skriptů za běhu.
- [ ] Datové třídy s dědičností (`base ak74` v `.weapon`, NPC, předměty, frakce), editor ukáže zděděné hodnoty.
- [ ] Demo záznam vstupů a semínek náhody, přehrání jako benchmark trasy zónou (průměr a 1 % low FPS) a pro reprodukci chyb.

### M5 – Vydání (~3–4 týdny)

- [ ] Cooking: binární meshe, komprimované textury, balení do archivů, kontrola chybějících a nepoužitých assetů.
- [ ] Balíček pro Windows/Linux/FreeBSD, verze buildu z `git describe` (log, menu, crash report), symboly ke každému vydání, release s LTO, artefakty z CI.
- [ ] VFS s mount pointy: volné soubory → archiv → složka `mods/` přepisující soubory podle cesty.
- [ ] Lokalizace: tabulky řetězců s klíči (čeština, angličtina, plurály), editor tabulek, kontrola nepřeložených.
- [ ] Přístupnost: titulky, FOV, vypnutí houpání kamery a motion bluru, velikost textu.
- [ ] Souhrn licencí třetích stran (OpenAL Soft LGPL: dynamické linkování a text licence).
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

1. Dokončit M0 (zbývá indirect draw, impostory, SoA data, graf úloh).
2. M0.5 Základy v pořadí: logování → asserty a crash handler → GUID entit → cvars a konzole → konfigurace → smoke test v CI → debug kreslení → GL ladění.
3. Mezitím rychlé opravy z hraní (ruce stejné u všech zbraní). Potom zbytek M1: UI hry a credits → stavy hry → okno a ovládání → uložení hry → lokální světla → fyzika v levelu → propojení entit → audio dál.
4. Střídání dne a noci hned po M1 (začátek M2/M3).
5. Souběžně s M1 může běžet obsah (assety, level zóny) v ostatních vláknech.
