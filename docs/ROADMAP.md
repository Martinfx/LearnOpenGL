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

## 1c. Atmosféra zóny (2026-10-31)

Atmosféra S.T.A.L.K.E.R.u nevzniká z jedné technologie, ale ze souhry: tmavé noci s baterkou,
počasí a mlha, tlumené barvy, prázdné pláně, vrstvený zvuk a NPC, která žijí i bez hráče.
Rozbor technik je v [`ENGINE_RESEARCH.md`, sekce 6](ENGINE_RESEARCH.md#6-atmosféra-zóny-z-čeho-vzniká-a-jak-ji-postavit);
konkrétní položky jsou označené **[atm]** v milnících níže.

| Pilíř | Co máme | Co chybí | Milník |
| --- | --- | --- | --- |
| Tmavé noci a baterka | pevná expozice, slunce, IBL z procedurální oblohy | Auto-expozice s dolním limitem, měsíc a hvězdy, ztlumení sky light v interiérech, baterka (reflektor se stínem a cookie), baterky NPC | M1 / M2 / M3 |
| Počasí a denní doba | presety Environment, déšť a vítr (zvuk + částice) | Klíčové snímky po hodinách a přechody počasí, stíny mraků, okluze deště, mokré povrchy a louže, blesky a hrom se zpožděním, globální vítr | M2 |
| Mlha a světlo v prostoru | výšková mlha se sun scatter | Volumetrická mlha (froxely), paprsky slunce, kužele baterek, barva oparu z oblohy | M3 |
| Barvy | saturace, kontrast, tint, viněta, zrno | 3D LUT po presetech s prolínáním, lift/gamma/gain, kontrola palety albeda | M1 / M2 |
| Zvuk | OpenAL, HRTF, EFX, ambientní zóny, déšť/vítr | Náhodné 3D jednorázové zvuky podle denní doby a oblasti, zvukové oblasti s prolnutím, reverb zóny, okluze, ducking před událostí, detektor/radiace | M1 / M4 |
| Život v zóně | Lua AI, navmesh | Online/offline NPC, graf zóny, tábory s rolemi a rozvrhem, offline střety se zvukem a následky, fauna | M4 |
| Anomálie, ohně, detaily | částice, flipbooky ohně a kouře, decaly | Průchod distorze (anomálie, teplo nad ohněm), táborák jako prefab, rozptyl odpadků a suti štětcem | M1 / M3 / M4 |

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

- [x] **Logování** (0,5 týdne): úrovně a kategorie, soubor v `logs/`, kruhový buffer, výstup do Output logu editoru, herní konzole a Lua `game.log`. *Hotovo 2026-10-31: `engine/Log.h` (`LOG_INFO/WARN/ERROR`, `FPS_ENGINE_LOG=debug`), soubory `game.log`/`editor.log` v uživatelské složce s rotací 5 běhů, ring 2000 řádků, posluchači (Output Log editoru, Qt zprávy), Lua `game.log`/`print`; herní konzole přijde s cvars.*
- [x] **Asserty a crash handler** (1 týden): `ENGINE_CHECK/ENSURE/VERIFY`; při pádu stack trace (Windows minidump, Linux/FreeBSD `backtrace` + `sigaltstack`), posledních 500 řádků logu, verze buildu, GPU/driver, level; editor nabídne obnovu. *Hotovo 2026-10-31: `engine/Assert.h`, `engine/Crash.h` (signály + `sigaltstack` + `backtrace`, Windows SEH + DbgHelp minidump, `std::terminate`), report v `crashes/` s verzí z gitu, GPU, levelem a 500 řádky logu; editor ukládá rozdělaný level každých 90 s na pozadí a po pádu nabídne obnovu; test hook `FPS_ENGINE_CRASH=segv|check`.*
- [x] **Cvars a konzole ve hře** (1 týden): registr s typem, popisem a příznaky, konzole `~` ve hře, `+cvar hodnota` z příkazové řádky, přístup z Lua, presety kvality jako sady cvars; editorová konzole na stejném registru. *Hotovo: `engine/CVars.h`, `game/GameConsole`, Lua `game.cvar` / `game.set_cvar`, editor `cvars [prefix]`; kvalita je `r_quality 0–3`.*
- [x] **Konfigurace a uživatelské složky** (0,5 týdne): `%APPDATA%` / XDG pro nastavení, uložené hry, logy a cache; výchozí → uživatelské → CLI. *Hotovo: `userDirectory()` (logs, config, recovery, saves), `config/game.cfg` se načte po výchozích hodnotách a před `+cvar` z příkazové řádky, ukládá se při ukončení.*
- [x] **GUID entit a assetů** (1 týden): trvalé 64bitové ID v levelu (verze 6 s migrací), zachované při kopírování/přesunu; podklad pro uložení, streaming, odkazy a prefaby. *Objekty hotové 2026-10-31: `ObjectId` u boxů, modelů a entit (řádek `  id` v levelu verze 6; starší levely dostanou ID při načtení a uloží ho editor), kopie a vložení dostanou nové ID, ID v Details, `findObjectById`, Lua `self.id`. GUID assetů (soubory `.meta`) zbývá k registru assetů v M2.*
- [x] **Smoke test levelů v CI** (0,5 týdne, přesunuto z M5): každý level 300 snímků pod `xvfb-run`, bez GL chyb a assertů; noční job s ASan+UBSan a TSan. *Hotovo: `tests/smoke_level.sh` a CTest `level_smoke_*` (štítek `smoke`, 120 snímků, chyba v logu = neúspěch, včetně GL chyb), Linux CI s LFS a xvfb, `.github/workflows/nightly.yml` (ASan+UBSan s levely, TSan s unit testy).*
- [x] **Debug kreslení a cheaty** (0,5 týdne): čáry/boxy/koule/text z C++ i Lua s dobou trvání; `noclip`, `god`, `give`, `teleport`, `timescale`, volná kamera. *Hotovo: `engine/DebugDraw.h` (barevné čáry, šipky, boxy, koule, texty s dobou trvání), Lua `game.debug_*`, `r_debugdraw`, `ai_debug` (stav, zdraví, zorné pole, cesta po navmeshi); cheaty za `cheats 1`: `god`, `noclip`, `freecam`, `timescale`, `give health|ammo|all`, `teleport X Y Z | jméno | kam se dívám`.*
- [x] **GL ladění** (0,5 týdne): `glDebugMessageCallback` do logu, `glObjectLabel` + `glPushDebugGroup` po průchodech (RenderDoc), `glProgramBinary` cache. *Hotovo: zprávy ovladače do logu (kategorie `gl`, opakované se ztlumí), `FPS_ENGINE_GL_DEBUG=1` = debug kontext se synchronními zprávami, popisky programů a render targetů, skupiny podle průchodů, cache binárek v `cache/shaders` uživatelské složky (`FPS_ENGINE_NO_SHADER_CACHE=1` vypne).*

Hotovo, když: každý pád zanechá report s logem, nastavení přežije restart, CI spustí všechny levely a entity mají trvalé ID.

### M1 – Hratelný vertikální řez (~8–10 týdnů)

Jedna lokace zóny od menu po smrt/uložení, se zvukem a chytřejšími nepřáteli.

- [x] **Audio** (2 týdny): ~~OpenAL Soft~~ miniaudio do `3rdparty/` (public domain / MIT-0 místo LGPL, jeden header, backendy pro Windows/Linux/FreeBSD, vestavěné dekodéry), mixování na vlákně zařízení, dekódování na job vlákně, 3D zdroje, streamovaná hudba, skupiny mixéru (sfx, music, ambient, ui); zvuky zbraní (výstřel, přebití, tasení, dopad, zásah), kroků, dopadu a zranění; entita "sound" (3D smyčka nebo ambientní zóna s fade) v editoru; Lua `game.play_sound`. Sound editor (variace, náhled i ze vzdálenosti, hlasitost, náhodný pitch, dosah, loop/stream, skupina). Skutečné nahrávky (CC0/CC-BY, `assets/sounds/CREDITS.md`).
- [x] **OpenAL Soft** (2026-10-31): přehrávání přepsáno na OpenAL Soft 1.24.3 (`3rdparty/openal-soft`, LGPL, sdílená knihovna, přednost má systémový balík), HRTF, EFX reverb presety, loopback zařízení pro testy a stroje bez zvukovky; miniaudio zůstává jen jako dekodér WAV/OGG/MP3/FLAC.
- [x] **Sound editor 2** (2026-10-31): průběh vlny, graf útlumu v dB s tažitelnými min/max, 3D náhled (zdroj kolem posluchače, orbit, dozvuk); stereo soubory se ve 3D mixují do mona. Kroky, dopady a nábojnice podle povrchu (`surfaces.cfg`), déšť a vítr v Environment.
- [x] Audio dál: hlasitosti skupin v menu hry, ducking, reverb zóny, okluze zvuku geometrií. *Hotovo (2026-11-01): hlasitosti skupin byly v Nastavení > Zvuk; ducking skupin v AudioSystem (`duck`), střelba hráče ztlumí ambient a hudbu (`snd_duck`); dva EFX reverb sloty s prolínáním, reverb celého levelu v Environment > Sound a entita Reverb zone (objem, preset, síla, prolnutí na hranici); okluze: 3 paprsky od ucha ke zdroji při startu zvuku a ~10× za sekundu, za zdí zvuk ztichne a ztmavne (low-pass), `snd_occlusion`. Testovací level `io_test` má zóny chodby a haly.*
- [ ] **[atm] Vrstvený ambient** (1 týden): zvukové oblasti v editoru (sada vrstev, prolnutí na hranici), náhodné jednorázové 3D zvuky 50–400 m od hráče (psi, vrány, vzdálená střelba, plech) s pravděpodobností podle denní doby a oblasti, venku/uvnitř, ducking ambientu před událostí; editor sad ve Sound editoru.
- [x] **Lokální světla** (1,5 týdne): bodová a reflektorová světla, clustered forward, stíny pro pár nejbližších; světla jako objekty v editoru s gizmem dosahu; blikající zářivky.
  - Hotovo (2026-11-01): entita typu `light` (engine/Lights.h), nastavení jako vlastnosti entity: bodové/reflektorové, intenzita, dosah, vnitřní/vnější úhel, sklon, stíny, blikání (oheň, zářivka, rozbitá, pulz), zapnuto. Renderer: clustered forward 16×9×24 s logaritmickými řezy, až 512 světel v záběru, `RenderView::lights` pro světla ze hry; stíny pro nejbližší světla v jedné mapě (reflektor 1 vrstva, bodové 6), rozlišení a počet vrstev v `RenderQuality`. Světla svítí i na zbraň v ruce. Editor: Přidat > Světlo (bodové / reflektor), panel Light v Details, gizmo dosahu (kruhy / kužel), náhled blikání. Testovací level `lights_test.level`.
- [x] **[atm] Baterka** (0,5 týdne, po lokálních světlech): reflektor se stínovou mapou a cookie texturou, mírné zpoždění za kamerou, klávesa, volitelně baterie; nastavení v editoru (Player); NPC v noci svítí baterkami.
  - Hotovo (2026-11-01): klávesa F (`in_flashlight`, přemapovatelná), konzole `flashlight [0|1]`. Reflektor u oka vpravo dole se stínem a procedurálním vzorem reflektoru (horký střed, kroužky; vlastnost `cookie` i pro světla v levelu), paprsek se zpožďuje za pohledem. Volitelná baterie: vybíjí se, před vybitím slábne a cuká, mimo provoz se dobíjí; HUD pruh „BATERKA“, stav v uložené hře. Editor: Level > Player (má baterku, stíny, barva, intenzita, dosah, šířka, zpoždění, baterie, dobíjení). NPC: zaškrtávátko Character > „Carries a lit flashlight“ (vlastnost `flashlight`); automatické rozsvícení v noci přijde s cyklem dne a noci.
- [x] **[atm] Táborák a ohně** (0,5 týdne): prefab s blikajícím bodovým světlem (šum), stínem, ohněm/kouřem/jiskrami a praskáním; sedací body pro NPC, volitelně 3D hudba (kytara).
  - Hotovo (2026-11-01): editor Přidat > Táborák (Place i menu Edit) vytvoří skupinu: ohniště `stone_fire_pit`, plameny/jiskry/kouř (`fire.particle`), bodové světlo s blikáním „fire“ a stínem, praskání (`sounds/ambient/campfire.sound`, procedurální CC0 smyčka) a 4 sedací body (entita `sit_point`, natočené k ohni). `enemy.lua` si při hlídce občas sedne k volnému ohni (`rest_chance`, `rest_time`, `anim_sit`), stav se ukládá. Lua: `:get_property`, `:set_property`, `game.find_nearest`. Kytara zatím ne (chybí asset); skutečné prefaby (soubor s více objekty) přijdou později.
- [x] **[atm] Expozice a grading** (1 týden): auto-expozice z histogramu s rychlostí adaptace a min/max EV (noc musí zůstat tmavá), 3D LUT na konci post-processu (export neutrální LUT, úprava v Kritě/GIMPu, prolínání podle presetu), lift/gamma/gain a vyvážení bílé v doku Environment.
  - Hotovo (2026-11-01): auto-expozice na GPU (log jasu vážený ke středu, bez zbraně, zmenšený mipmapami na 1 texel; adaptace v EV zvlášť do tmy a do světla; min/max EV kolem ruční expozice; míchání 0..1 s ruční). Grading po tone mappingu: teplota a zelená/purpurová, lift/gamma/gain, 3D LUT z pruhu N²×N (32³) se silou. Editor Environment: všechny hodnoty, výběr LUT a „Export neutral LUT…“. Ukázkové LUT `textures/luts/neutral_32.png` a `zone_bleak_32.png` (generuje `make_luts.py`). Místo histogramu je průměr logaritmu (stabilní, bez čtení zpět na CPU); prolínání LUT podle presetu/počasí přijde s klíčovými snímky počasí.
- [x] **Navigace** (2 týdny, zbývají krycí body; zobrazení a nastavení v editoru hotové: Details > Level > Navigation, View > Navigation mesh `P`): Recast/Detour do `3rdparty/`, stavba navmeshe na pozadí z kolizí levelu, zobrazení a nastavení v editoru, `self:move_to()` v Lua, krycí body.
- [x] **UI hry** (1,5 týdne): hlavní menu, pauza, nastavení (grafika, myš, klávesy, hlasitost), HUD (zdraví, munice), credits s CC-BY autory, obrazovka smrti. *Hotovo 2026-11-01: `game/GameMenu` (hlavní menu, pauza Esc zastaví simulaci, nastavení generované z cvars s příznakem Config – stránky Grafika/Ovládání/Zvuk, credits ze všech `CREDITS.md` a licencí knihoven, obrazovka smrti), `game/GameHud` (zdraví, zbraň, munice, přebíjení, záblesk zásahu); `--no-menu` jde rovnou do hry. Přemapování kláves zbývá k „Okno a ovládání“, načtení hry k „Uložení hry“.*
- [x] **Uložení hry** (1 týden): serializace stavu entit, hráče a skriptů (Lua tabulka `save`), quick save/load. Po entitách s GUID jako rozdíl proti levelu, atomický zápis (dočasný soubor + `rename`), kontrolní součet, verze obsahu, sloty, autosave. *Hotovo 2026-11-01: `game/GameSave` (textový soubor v `saves/` uživatelské složky, dočasný soubor + `rename`, kontrolní součet FNV-1a, verze), hráč, zbraně, dávka, zabití a entity podle `ObjectId` s Lua `on_save`/`on_load` (enemy.lua ukládá stav AI); pozice rychlá (F5/F9), automatická (po změně lokace) a 1–3; menu Uložit/Načíst, smrt → Načíst poslední uložení, konzole `save`/`load`.*
- [x] **Stavy hry a přechody** (1 týden): Boot → Menu → Loading → Playing ↔ Paused; pauza zastaví simulaci, ne UI; přechod mezi lokacemi s přenosem hráče a inventáře. *Hotovo 2026-11-01: level je samostatná session v `game/main.cpp` (`playLevel`), okno, renderer, UI, konzole a menu ji přežijí; menu „Začít lokaci znovu“ a „Vybrat lokaci“, konzole `map` a `changelevel`, Lua `game.change_level(file)` přenese zdraví, zbraň a zásobníky. Přenos inventáře doplní „Předměty a inventář“.*
- [x] **Okno a ovládání** (1 týden): VSync / limit FPS, fullscreen / borderless, výběr monitoru, render scale, `NvOptimusEnablement`; přemapování kláves z configu, citlivost a inverze myši, gamepad přes GLFW (přesunuto z M5). *Hotovo 2026-11-01: cvars `r_fullscreen` (okno / bez rámečku / celá obrazovka), `r_monitor`, `r_scale` (scéna v nižším rozlišení, UI ostré), zapamatovaná velikost okna; vazby `in_*` (Nastavení > Ovládání: klikni a stiskni klávesu, i tlačítka myši), gamepad (páčky, spouště, Start = pauza, `joy_sensitivity`, `joy_deadzone`), export pro NVIDIA Optimus / AMD na Windows.*
- [x] **Propojení entit** (1 týden): výstupy a vstupy jako v Hammeru (`OnTrigger` → `Open`), propojení klikem v editoru, volitelně z Lua; trigger objemy v editoru (z položky Fyzika v levelu). *Hotovo (2026-11-01): každá entita má tabulku Outputs (výstup, cíl, vstup, parametr, zpoždění, jednou) s nabídkou známých jmen i jmen ze skriptu, cíl jde vybrat klikem ve viewportu, vybraná entita ukazuje šipky ke svým cílům a od zdrojů (vidět i přes zdi), přejmenování entity opraví odkazy. Nové entity trigger (objem, reaguje na hráče / NPC, Once) a relay. Výstupy OnStart, OnDamaged, OnDeath, OnTrigger, OnStartTouch, OnEndTouch; vstupy Enable/Disable/Toggle, TurnOn/TurnOff, Trigger, Kill, Damage, SetProperty, ShowMessage, PlaySound; cíle !self, !activator, !player. Lua: `on_input`, `self:fire_output`, `game.fire`, `game.message`; enemy.lua umí Alert/Calm a OnSpotted. Zvuky a částice se vypínají vstupy, stav I/O se ukládá do save. Testovací level `io_test` (trigger rozsvítí halu, spustí poplach a pošle banditu, jeho smrt poplach vypne).*
- [ ] **Předměty a inventář, základ** (1 týden): pickupy (munice, lékárnička), jednoduchý inventář zbraní.
- [x] **Decaly a částice, základ** (1,5 týdne): díry po kulkách a krev z existujícího atlasu, prach a jiskry při zásahu, kouř z hlavně; nastavení v `.weapon` a fyzikálním materiálu. *Hotovo: `.particle` efekty, ParticleSystem na JobSystemu, DecalPool, nábojnice s odrazy, Particle editor s živým náhledem. 2026-10-31: odražené kulky (šance podle povrchu a úhlu, svištění, jiskry, poloviční poškození, nastavení v Assets > Surfaces), menší díry a oprava tečkovaného šumu (barva průhledných texelů při kompresi a mip mapách).*
- [x] **Fyzika v levelu** (1 týden): dynamická tělesa (sudy, bedny), fyzikální materiály povrchů (zvuk kroků, decal, částice), triggery v editoru. *Hotovo (2026-11-01): modely s příznakem Physics prop jsou Jolt tělesa (box / válec / koule podle obrysu, hmotnost v kg), padají, hráč je odstrčí, kulky a nůž do nich strkají, pozice se ukládají do save; v editoru řádky Physics prop / Mass / Collider, výchozí hodnoty z katalogu `clutter.json` při vložení a Edit > Physics props from the catalogue pro celý level; testovací level `physics_test`. Zvuk kroků, decaly a částice podle povrchu už byly hotové (surfaces.cfg). Triggery přecházejí do položky Propojení entit (trigger objem s výstupem OnTrigger).*

Hotovo, když: hra se spustí do menu, jde projít lokaci, nepřátelé obcházejí překážky, vše zní, hra jde uložit a načíst.

### M2 – Otevřená zóna (~8 týdnů)

- [ ] **Terén** (3 týdny): heightmapa po dlaždicích, sculpt a malování až 8 vrstev materiálu v editoru, kolize v Jolt (HeightFieldShape), LOD terénu.
  Hotovo: jedna heightmapa, režim Landscape (raise/lower/smooth/flatten, malování 4 vrstev), kolize HeightFieldShape. Zbývá: dlaždice, 8 vrstev, LOD.
- [ ] **Vegetace** (2 týdny): štětec na stromy, keře a trávu (instancing z M0), hustota podle vrstev terénu, vítr ve vertex shaderu, cull distance a fade.
  Hotovo: režim Foliage (štětec, typy s hustotou, měřítkem, cull distance, kolizí, náklonem, stíny), instancing po 32m buňkách, kolize stromů. LOD úrovně foliage (`foliage_lod`) a řídnutí do dálky (far density). Zbývá: hustota podle vrstev, vítr, fade.
- [ ] **Streamování světa** (3 týdny): rozdělení levelu na buňky (např. 128 × 128 m), načítání a uvolňování podle vzdálenosti na pracovních vláknech, HLOD/impostory budov, editor zobrazí buňky a umí je zamknout/načíst; rozpočet paměti.
- [ ] **Voda** (1 týden): řeka a louže (odrazy SSR nebo probe, průhlednost, mlha pod hladinou).
- [ ] **Střídání dne a noci** (1,5 týdne, přání martina 2026-10-31): slunce a měsíc po skutečné dráze podle zeměpisné šířky a data, nastavitelná délka dne; obloha s atmosférickým rozptylem (východ a západ slunce), noční obloha s hvězdami a měsícem; stíny, ambient, mlha a expozice podle času. Vše v Environment, v editoru posuvník času a přehrávání, v Lua `game.set_time`.
  **[atm]** Navíc: typ počasí jako klíčové snímky po hodinách (barvy oblohy, slunce, mlhy, ambient, oblačnost, LUT) s interpolací; noc opravdu tmavá (noční světlo ~1/1000 dne, namodralé, min EV auto-expozice); IBL přepočítávané po stěnách cubemapy; stíny mraků (posouvaná šumová textura).
- [ ] **Počasí** (1 týden): déšť (hotový základ), mlha a bouřka jako přechody mezi presety Environment, blesky; editor časové osy dne.
- [ ] **[atm] Déšť a bouřka** (1,5 týdne): mapa okluze deště shora (neprší pod střechou, tlumený zvuk uvnitř), cákance, mokré povrchy (tmavší albedo, nižší roughness, louže z masky, kroužky) s vysycháním; blesky (záblesk světla a oblohy, blesk v dálce), hrom se zpožděním podle vzdálenosti (343 m/s), zesílení větru.
- [ ] **[atm] Globální vítr**: jeden vektor pro vegetaci, částice, látky a zvuk, nastavení v Environment.
- [ ] **[atm] Paleta assetů**: kontrola rozsahu albeda (cca 30–240 sRGB) a sytosti v asset validaci editoru.

- [ ] **Rozpočty paměti** (1 týden): limity po kategoriích (textury, meshe, audio, fyzika, Lua) s varováním v doku Memory, snímkový alokátor, pooly pro částice a decaly.
- [ ] **Asset registry a hot reload** (2 týdny): závislosti assetů podle GUID, přejmenování bez rozbití odkazů, validace (měřítko, chybějící textury) v editoru i CI, sledování souborů a reload textur, modelů, zvuků, `.weapon`, `.particle` a Lua za běhu.
- [ ] **Prefaby s přepisy** (1,5 týdne): skupina entit jako asset, kopie v levelu s přepsanými vlastnostmi, změna prefabu se propíše; autosave levelu v editoru.

Hotovo, když: zóna 1 × 1 km jde projít bez načítací obrazovky a paměť drží rozpočet.

### M3 – Kvalita obrazu a animací (~6–8 týdnů, souběžně s M2)

- [ ] **Animace** (3 týdny): stavový automat s přechody, blend space 1D/2D, vrstvy s maskou kostí, IK nohou na terénu a levé ruky na zbrani, události v klipech; **Animation editor** (časová osa, náhled, přechody jako graf).
- [ ] **Ragdolly** (1 týden): Jolt ragdoll ze skeletonu, přechod ze smrti; nastavení kloubů v editoru.
- [ ] **Osvětlení** (2 týdny): reflection probes s ručním umístěním a bake v editoru, irradiance probe grid pro interiéry, volumetrická mlha a světelné kužele.
- [ ] **[atm] Volumetrická mlha** (1,5 týdne): froxel mřížka ve compute shaderu, rozptyl ze slunce se CSM stíny a z lokálních světel (kužele baterek), časové vyhlazení, paprsky slunce; barva oparu z oblohy podle směru. Nastavení v Environment a presetech kvality.
- [ ] **[atm] Tmavé interiéry** (0,5 týdne, s probes): objemy „interiér“ ztlumí světlo oblohy a změní ambient, reverb a déšť; baterka pak v budovách dává smysl.
- [ ] **[atm] Distorze vzduchu** (0,5 týdne): průchod, do kterého částice a meshe zapisují posun UV, post-process posune obraz s maskou hloubky; tetelení nad ohněm, anomálie, výfuky.
- [ ] **Obraz** (1 týden): TAA (místo FXAA na High), motion blur volitelně, kvalitnější SSAO (GTAO), SSR.
- [ ] **Profiler a testy obrazu** (1 týden): Tracy (`3rdparty/`) s časovou osou vláken a GPU zónami; golden image testy rendereru v CI s tolerancí.
- [ ] **Materiály** (1–2 týdny): instance materiálu, vertex paint a blend vrstev, detail mapy; **VFX editor** pro částice z M1.

### M4 – Zóna žije: gameplay systémy (~8–10 týdnů)

- [ ] Frakce a vztahy (stalkeři, bandité, vojáci), skupiny NPC, neutrální NPC v táboře.
- [ ] Simulace mimo dohled hráče (zjednodušená A-life: skupiny se přesouvají mezi body zájmu na mapě, i když nejsou načtené).
  **[atm]** Konkrétně: NPC online do ~150 m (s hysterezí), dál offline záznamy; graf zóny přes nenačtené buňky; tábory nabízejí role (hlídka, spánek, oheň, obchod) s rozvrhem den/noc; offline střety vyhodnocené statisticky s následky (těla, loot) a slyšitelnou střelbou ze správného směru; uložení s GUID. Editor: body zájmu a role v levelu, ladicí pohled na simulaci na mapě.
- [ ] **[atm] Fauna**: smečky psů s doupaty, vrány, které vyletí při přiblížení (jednoduché boidy).
- [x] **[atm] Radiace a detektor**: praskání jako Poissonův proces podle dávky, pípání detektoru podle vzdálenosti anomálie.
- [ ] **[atm] Anomální bouře** (vlastní design): událost řízená počasím s varováním (zvuk, obloha, ducking), nutnost úkrytu.
- [ ] Úkoly a dialogy: data v souborech, editor dialogů jako strom, PDA s úkoly (navazuje na mapu).
- [ ] Obchod, peníze, opotřebení zbraní a zbroje, váha inventáře.
- [ ] Anomálie a artefakty (vlastní design): trigger + VFX + zvuk + poškození, detektor. **[atm]** Distorze z M3, hučení, reakce na hozený předmět.
  - [x] Anomálie (2026-10-31): Drtič, Výboj, Výheň, Žíravina, Zlom, Radiace; šrouby (G), detektor, geiger, dávka a HUD; zkreslení obrazu v post-processu (zatím zjednodušené, bez zápisu z částic); editor + `anomaly_test.level` + 8 ve Skrundě. Artefakty zbývají.
- [ ] **[atm] Detaily zóny**: rozptyl odpadků, suti a malých meshů štětcem (Foliage režim), decaly nápisů a skvrn, rádia s filtrovaným zvukem, prach v kuželech světla.
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
3. Mezitím rychlé opravy z hraní (ruce stejné u všech zbraní). Potom zbytek M1: UI hry a credits → stavy hry → okno a ovládání → uložení hry → lokální světla → **baterka, táborák, expozice a LUT** → fyzika v levelu → propojení entit → audio dál a vrstvený ambient.
4. Střídání dne a noci hned po M1 (začátek M2/M3), s ním déšť a bouřka, pak volumetrická mlha a tmavé interiéry (M3) a A-Life (M4).
5. Souběžně s M1 může běžet obsah (assety, level zóny) v ostatních vláknech.
