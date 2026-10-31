# Co musí řešit profesionální engine – průzkum a mezery

Stav k 2026-10-31 (větev `engine`, commit `88d94f7`). Navazuje na audit v [`ROADMAP.md`](ROADMAP.md),
který porovnává *funkce* (renderer, terén, audio …) s Unrealem a Unity. Tenhle dokument se dívá
hlavně na **to, co hráč ani autor levelu nevidí, ale bez čeho se profi engine neobejde**:
logy, pády, konfigurace, ID entit, verze dat, testy, balení, lokalizace, platformní detaily.
Právě tohle malé enginy obvykle podcení a pak to doplácejí, když hra roste.

Zdroje: veřejná dokumentace Unreal Engine 5, Unity 6, Godot 4, Source/Source 2 (Valve Developer
Community), id Tech (zdrojáky Quake/Doom 3, GDC přednášky), X-Ray (open-source OpenXRay, modding
komunita S.T.A.L.K.E.R.), knihy *Game Engine Architecture* (J. Gregory) a GDC přednášky o
produkčních enginech. Porovnání s naším kódem je z přímého čtení repozitáře.

## 1. Mapa subsystémů profi enginu

Každý velký engine má zhruba stejné vrstvy. Tučně jsou ty, které v naší roadmapě dosud chyběly
nebo byly jen zmíněné.

| Vrstva | Co do ní patří | Jak to řeší velcí |
| --- | --- | --- |
| Platforma | okno, vstup, čas, vlákna, souborový systém, cesty k uživatelským datům, **detekce GPU a driverů** | UE `FPlatformMisc`, SDL v Source 2, Godot `OS`/`DisplayServer` |
| Jádro | **logování, asserty, crash handler**, paměť, kontejnery, job systém, **konzole a cvars**, **konfigurace (ini)**, reflexe/serializace | UE `UE_LOG`, `check/ensure`, CrashReportClient, `IConsoleManager`, `.ini` hierarchie; id Tech `cvar_t`; X-Ray `.ltx` s dědičností |
| Data | **stabilní ID (GUID)**, asset registry se závislostmi, **verzování a migrace formátů**, derived data cache, cooking, pak soubory, **VFS s mount pointy** | UE AssetRegistry + DDC + `.pak/.utoc`; Unity `.meta` GUID; Source VPK; X-Ray `.db` archivy a `fsgame.ltx` |
| Svět | scéna, entity, prefaby, streamování, **správa stavů hry** (menu/načítání/hra/pauza), přechody mezi levely | UE GameMode/GameInstance, World Partition; Godot SceneTree; X-Ray online/offline A-Life |
| Simulace | fyzika, animace, AI, gameplay, skripty, **pevný krok a interpolace**, **deterministické přehrávání (demo)** | id Tech/Source dema, UE Replay system |
| Prezentace | renderer, audio, UI, VFX, **lokalizace**, **přístupnost**, titulky | UE Localization Dashboard, Unity Localization; přístupnost dle Xbox Accessibility Guidelines |
| Nástroje | editor, **validace dat**, profilery, **in-game debug menu a cheaty**, debug kreslení | UE Insights, Data Validation, cheat manager; Source `developer 1`, `noclip`, `ent_*` |
| Výroba | build systém, CI, **testy (unit, golden image, smoke, výkon)**, **sanitizery**, balení, **symboly a crash reporty**, verze buildů | UE Gauntlet/Automation, Horde; Unity Test Framework; Valve vlastní crash server |

## 2. Co z toho už máme (silné stránky)

- **Renderer** na úrovni moderního forward enginu: HDR, CSM s cache, SSAO, IBL, bloom, ACES, FXAA, Hi-Z occlusion, instancing, víc LOD, komprese textur BC s diskovou cache, streamování textur.
- **Job systém** a asynchronní načítání, editor neblokuje UI.
- **Fyzika** Jolt (statické kolize, character controller, height field), **navmesh** Recast/Detour se stavbou na pozadí.
- **Audio** OpenAL Soft s HRTF a EFX, Sound editor, povrchy (`surfaces.cfg`).
- **Editor** s docky, gizmem, undo, 4 pohledy, editory materiálů, textur, shaderů, zvuků, částic, zbraní, terénu a vegetace; konzole s několika příkazy; panely Frame a Memory s GPU timery.
- **CI** na třech OS s CTest (25 testů jádra), formáty souborů mají číslo verze (level v5, materiál v1).
- **Fonty** pečou Latin-1 i Latin Extended-A, takže čeština (č, ř, ž, ů) se vykreslí.

## 3. Mezery, seřazené podle priority

Priority stejné jako v roadmapě: **P0** blokuje hratelnou hru / brzdí vývoj už teď,
**P1** potřeba pro hru v zóně, **P2** kvalita a pohodlí, **P3** později.

### P0 – základy, které se s růstem hry zdraží každým týdnem

1. **Logování** (0,5 týdne). Teď jen `fprintf(stderr)` na 6 místech. Chybí úrovně (trace/info/warn/error), kategorie (render, audio, physics, script), zápis do souboru `logs/engine_YYYYMMDD.log`, kruhový buffer v paměti pro crash report a společný výstup do Output logu editoru i herní konzole. Lua `game.log` napojit na stejný systém. *Bez toho každá chyba u Martina (FreeBSD SIGILL, prázdný viewport) znamená hádání.*
2. **Crash handler a asserty** (1 týden). Žádné `signal()`/`SetUnhandledExceptionFilter`, žádné `assert`. Profi engine při pádu uloží stack trace (Windows: DbgHelp + minidump; Linux/FreeBSD: `backtrace()`/libunwind + `sigaltstack`), posledních ~500 řádků logu, verzi buildu, GPU a driver a otevřený level. Makra `ENGINE_CHECK` (fatální), `ENGINE_ENSURE` (zaloguje jednou a pokračuje, jako UE `ensure`), `ENGINE_VERIFY`. Editor po pádu nabídne obnovu z autosave.
3. **Stabilní ID entit (GUID)** (1 týden). Entity v levelu mají jen jméno a pořadí v souboru. Uložení hry, streamované buňky, odkazy mezi entitami (trigger → dveře), prefaby i A-Life potřebují 64/128bitové trvalé ID, které přežije přejmenování a přesun. Roadmapa to zmiňuje v závislostech, ale nemá na to položku. Musí přijít **před** uložením hry a streamováním.
4. **Konzole a cvars v enginu** (1 týden). Konzole je teď jen v editoru s pevným seznamem příkazů. Profi standard (id Tech, Source, UE): registr proměnných `r.ssao`, `s.volume`, `ai.debug` s typem, popisem, výchozí hodnotou a příznaky (uložit do configu, jen pro vývoj, cheat), dostupný ve hře (klávesa `~`), v editoru, z příkazové řádky (`+r.ssao 0`) a z Lua. Presety kvality pak jsou jen sady cvars.
5. **Konfigurace a uživatelská data** (0,5 týdne). Nastavení grafiky, zvuku a kláves se musí ukládat do souboru ve správném místě (Windows `%APPDATA%`, Linux/FreeBSD `$XDG_CONFIG_HOME` / `$XDG_DATA_HOME`), stejně tak uložené hry, logy a cache. Hierarchie: výchozí v balíčku → uživatelské → příkazová řádka.
6. **Testy výš než unit** (1 týden, pak průběžně). Máme unit testy jádra. Chybí: (a) **smoke test levelu** v CI (načíst každý level, 300 snímků pod `xvfb-run` s llvmpipe, žádná GL chyba, žádný assert) – hooky `FPS_ENGINE_FRAMES` a `FPS_ENGINE_SCREENSHOT` už existují, v roadmapě je to až v M5; (b) **golden image testy** rendereru (snímek porovnat s referencí s tolerancí); (c) **sanitizery** – noční job s ASan+UBSan a TSan pro job systém. Pád z hardened libc++ na FreeBSD ukázal, že by takové chyby chytily dřív.
7. **Debug kreslení a in-game debug nástroje** (0,5 týdne). V roadmapě zmíněné v auditu, ale bez položky v milníku. Čáry, boxy, koule, text ve 3D z C++ i Lua s dobou trvání; cheaty `noclip`, `god`, `give`, `teleport`, volná kamera a časová lupa (`timescale`). Zrychlí ladění AI, navmeshe, fyziky i levelů.

### P1 – potřeba, než hra naroste do zóny

8. **Správa stavů hry a přechodů** (1 týden). Stavový automat Boot → Menu → Loading → Playing ↔ Paused → Loading jiné lokace; pauza zastaví simulaci, ale ne UI a audio menu; přechod mezi lokacemi zóny s přenosem hráče a inventáře (X-Ray: level changer). Navazuje na UI hry a uložení.
9. **Verzování a migrace formátů** (0,5 týdne + průběžně). Číslo verze máme, ale chybí pravidlo: každá změna formátu = nová verze + migrační funkce + test, který načte starý soubor. Uložené hry navíc potřebují verzi obsahu, aby po updatu hry nespadly.
10. **Bezpečné uložení hry** (součást M1 Uložení). Zápis do dočasného souboru a atomický `rename`, kontrolní součet, sloty, autosave a quicksave, náhled a čas hraní; ukládá se po entitách s GUID a jen rozdíl proti levelu (jako X-Ray `.sav` a Source save delta).
11. **Hot reload všech assetů** (1 týden). Teď ručně jen shadery a materiály. Profi editory sledují soubory (Qt `QFileSystemWatcher`, ve hře vlastní watcher) a za běhu přenahrají textury, modely, zvuky, `.weapon`, `.particle`, Lua skripty. Velké zrychlení práce s assety z Blenderu.
12. **Asset registry a závislosti** (1,5 týdne). Kdo používá který materiál/texturu, přejmenování bez rozbití odkazů (díky GUID z bodu 3), hledání chybějících a nepoužitých souborů, validace (měřítko 1 m, chybějící textury, rozměry mimo 2^n) jako tlačítko v editoru i v CI. Dnes to roadmapa řadí až do M5 cookingu, ale nástroje potřebujeme dřív.
13. **Profiler s časovou osou vláken** (0,5 týdne). Dok Frame ukazuje časy průchodů. Na job systém a budoucí render vlákno potřebujeme timeline: **Tracy** (BSD licence, jde do `3rdparty/` beze změn, podporuje GL timer queries i Lua). Zóny `ENGINE_ZONE("Culling")`, vypnuté v release.
14. **Vstup: akce, přemapování, gamepad** (1 týden). `Input.h` už má akce, ale bez mapování z konfigurace, bez citlivosti/inverze myši, bez gamepadu (GLFW gamepad API umí). Roadmapa to má v M5, ale nastavení ovládání patří do menu v M1 – přesunout aspoň přemapování.
15. **Okno a platforma** (0,5 týdne). VSync / limit snímků (ve hře je `glfwSwapInterval(0)` natvrdo), fullscreen / okno bez rámečku, výběr monitoru, DPI, alt-tab bez ztráty kontextu, export `NvOptimusEnablement`/`AmdPowerXpressRequestHighPerformance` (na noteboocích jinak hra běží na integrované GPU), škálování rozlišení (render scale; později FSR 1 jako shader).
16. **GL ladění a cache programů** (0,5 týdne). `glDebugMessageCallback` do logu v debug buildu, `glObjectLabel` a `glPushDebugGroup` po průchodech (RenderDoc a Nsight pak ukazují jména), `glProgramBinary` cache pro rychlejší start. Plus testovací matice driverů: NVIDIA, AMD (Mesa radeonsi na FreeBSD/Linux i Windows), Intel.
17. **Události a propojení entit** (1 týden). Source/Hammer „entity I/O“ (výstup `OnTrigger` → vstup `Open` dveří) a Godot signály: v editoru se klikem propojí trigger s cílem, bez psaní skriptu. Roadmapa má „události a zprávy“ obecně; doporučuju konkrétně tento model, protože se dobře edituje (pravidlo *Editor první*).
18. **Prefaby s přepisy** (1,5 týdne). Unity prefab varianty / UE Blueprint třídy / Godot zděděné scény: kontejner, sud s fyzikou, světlo se zvukem jako jeden asset vložený vícekrát, úprava se propíše do všech kopií. Bez toho se modulární level z dílů těžko udržuje.
19. **Datové třídy s dědičností** (0,5 týdne). X-Ray `.ltx` sekce s rodičem (`[ak74u]:ak74`) jsou důvod, proč je S.T.A.L.K.E.R. tak dobře moddovatelný. Pro `.weapon`, NPC, předměty a frakce: `base ak74` + jen rozdílné hodnoty, editor ukazuje zděděné hodnoty šedě.

### P2 – kvalita, pohodlí a vydání

20. **Lokalizace** (1 týden). Tabulky řetězců s klíči (`ui.menu.new_game`), čeština + angličtina, formátování s parametry a plurály (čeština má 3 tvary), editor tabulek, hledání nepřeložených. Texty v Lua a dialozích přes klíče od začátku, ať se to nemusí předělávat.
21. **Přístupnost** (0,5 týdne). Titulky k dialogům a důležitým zvukům, FOV, vypnutí motion bluru a houpání kamery, velikost textu, barvy zaměřovače, přemapování všech kláves. Levné, pokud se plánuje s menu.
22. **Demo záznam a benchmark** (1 týden). id Tech/Source dema: nahrát vstupy a náhodná semínka, přehrát. Slouží jako opakovatelný benchmark (stejná trasa zónou, průměr/1 % low FPS) a k reprodukci chyb od hráčů. Vyžaduje pevný krok simulace (máme `FixedStepClock`) a semínka náhody na jednom místě.
23. **Rozpočty paměti a alokátory** (1 týden). Dok Memory ukazuje celkovou spotřebu. Profi enginy mají rozpočty po kategoriích (textury, meshe, audio, fyzika, Lua) s varováním při překročení, snímkový (frame) alokátor pro dočasná data a pooly pro částice a decaly. Potřeba nejpozději se streamováním světa.
24. **VFS a mody** (1 týden, součást cookingu). Mount pointy: ve vývoji volné soubory, ve vydání archiv, nad tím složka `mods/`, která přepisuje soubory podle cesty (model X-Ray `gamedata/`). Komunita S.T.A.L.K.E.R. žije z modů; jde to skoro zadarmo, když se VFS udělá dřív než balení.
25. **Výroba vydání** (součást M5). Verze buildu z gitu (`git describe`) v logu, menu a crash reportu; uložení symbolů (PDB, debug info) ke každému vydanému buildu; release build s LTO; artefakty z CI ke stažení; souhrn licencí třetích stran (OpenAL Soft je LGPL – dynamické linkování a text licence v balíčku) a CC-BY autorů v credits.
26. **Autosave a zálohy v editoru** (0,5 týdne). Autosave levelu každých N minut do `.autosave`, obnova po pádu, rotující zálohy. Profi editory (UE, Unity) to mají, protože editor padá i jim.

### P3 – vědomě později

27. **RHI vrstva / Vulkan** – už v roadmapě jako „až bude OpenGL limitovat“.
28. **Telemetrie** (anonymní, opt-in: FPS, pády) – až budou hráči.
29. **Síť / co-op** – roadmapa ji vylučuje; GUID entit (bod 3) a oddělení vstupu od simulace (bod 22) nechávají dveře otevřené.

## 4. Poučení z jednotlivých enginů

| Engine | Co si vzít | Na co si dát pozor |
| --- | --- | --- |
| **X-Ray (S.T.A.L.K.E.R.)** | A-Life s online/offline přepínáním NPC podle vzdálenosti; `.ltx` konfigurace s dědičností; `gamedata/` přepisující archivy (mody); level changery mezi lokacemi | Proslulá nestabilita a neinformativní pády – proto crash handler a logy hned. Velké singletony a globální stav ztěžovaly opravy. |
| **id Tech** | Konzole + cvars jako páteř ladění i nastavení; dema pro benchmark; minimalismus a rychlý start | Megatextury: vysoká složitost pro malý tým – nekopírovat. |
| **Source / Source 2** | Entity I/O v Hammeru; VPK archivy s volnými soubory navrch; `developer`/`noclip`/`ent_fire` příkazy; Source 2 modelování přímo v Hammeru (už máme v plánu) | Kompilace map (VBSP/VVIS/VRAD) zdržovala iteraci – my peču na pozadí, tak to držet. |
| **Unreal** | `check`/`ensure` asserty; Insights; Data Validation; World Partition s „one file per actor“ (méně konfliktů v gitu); Gameplay Tags | Obrovská složitost (UObject, Blueprints); vybírat jen principy. |
| **Unity** | GUID v `.meta` u každého assetu; prefab varianty; ScriptableObject datové assety; Test Framework | Pozdě řešené verzování serializace a drahý domain reload. |
| **Godot** | Scény jako prefaby s dědičností, signály, textové formáty dobře čitelné v gitu | Výkon velkých otevřených světů – streamování si stavět sami (to plánujeme). |

## 5. Doporučené změny roadmapy

Promítnuto do [`ROADMAP.md`](ROADMAP.md) (sekce 1b a položky v milnících):

- **Nový blok „M0.5 Základy enginu“** (~3 týdny), hned po zbytku M0 a souběžně se začátkem M1: logování, crash handler + asserty, cvars a herní konzole, konfigurace v uživatelských složkách, GUID entit, smoke test levelů v CI + sanitizery, debug kreslení a cheaty, GL debug labely.
- **M1** rozšířit o: stavy hry a přechody mezi lokacemi, přemapování ovládání a nastavení okna (VSync, limit FPS, fullscreen), bezpečné ukládání (atomický zápis, verze, sloty), propojení entit (entity I/O).
- **M2** rozšířit o: rozpočty paměti po kategoriích, asset registry se závislostmi a validací, prefaby s přepisy.
- **M3** rozšířit o: golden image testy rendereru, profiler Tracy pro render vlákno.
- **M4** rozšířit o: datové třídy s dědičností (zbraně, NPC, předměty), demo záznam/benchmark.
- **M5** rozšířit o: lokalizaci od tabulek po editor, přístupnost, VFS + složka `mods/`, symboly a verze buildu, souhrn licencí.
- Smoke test levelu v CI **přesunout z M5 do M0.5** – hooky už existují a chytí regrese hned.

Pořadí nejbližších kroků: logování → crash handler → GUID → cvars/konzole → smoke test v CI;
pak pokračovat v M1 (UI hry, uložení, lokální světla, fyzika v levelu).

## 6. Atmosféra zóny: z čeho vzniká a jak ji postavit

S.T.A.L.K.E.R. nepůsobí z technologie, ale ze souhry světla, počasí, zvuku, prázdnoty a života
mimo hráče. Níže je každý pilíř rozložený na techniky, které engine potřebuje, s tím, co už máme.
Obsah a design jsou vlastní (žádné kopírování lokací, frakcí ani pojmů z původní hry).

### 6.1 Světlo: opravdu tmavé noci a baterka

- **Expozice s limity.** Dnes je expozice pevná (`Environment::exposure`). Profi enginy mají automatickou expozici (histogram jasu → cílová EV s pomalou adaptací oka), ale **s dolním limitem**, jinak noc „vytáhne“ na šedý den. Pro tmavé noci: min EV nastavit tak, aby bez baterky byla vidět jen silueta horizontu a obloha.
- **Měsíc a hvězdy.** Noční slunce = slabé namodralé směrové světlo (asi 1/1000 dne) se stíny, hvězdy a měsíc v `sky.frag`, IBL z noční oblohy skoro černé.
- **Interiéry musí být tmavé.** Největší chyba malých enginů: světlo oblohy (IBL) svítí i do budov. Řeší se objemy „interiér“, které ztlumí sky light, nebo irradiance probes (M3). Bez toho baterka v budově nic neznamená.
- **Baterka.** Reflektor se stínovou mapou, maskou (cookie textura s nerovnoměrným kruhem a skvrnami), mírné zpoždění za kamerou (pocit ruky), kužel viditelný ve volumetrické mlze, zapínání klávesou a volitelně baterie. Baterky NPC v noci prozradí jejich polohu.
- **Ohně a lampy** jako bodová světla s blikáním (šum, ne sinus) a stíny; jediné teplé barvy ve studené scéně.

### 6.2 Počasí a denní doba

- **Klíčové snímky po hodinách.** X-Ray drží pro každý typ počasí sadu stavů po hodinách (barvy oblohy, slunce, mlhy, ambient, oblačnost, déšť) a mezi nimi interpoluje. Pro nás: preset Environment = 24 (nebo 8) klíčů, editor s časovou osou dne, přechod mezi typy počasí (jasno → zataženo → bouřka) během desítek sekund. Herní čas běží rychleji než reálný (nastavitelné).
- **Obloha a IBL průběžně.** Procedurální obloha se s časem mění; IBL přepočítávat po částech (jedna stěna cubemapy za snímek), ne celé najednou.
- **Stíny mraků.** Posouvaná šumová textura promítnutá ze slunce na terén a budovy. Levné a mimořádně „živé“.
- **Déšť.** Částice kolem kamery (máme `rain.particle`) + **mapa okluze deště** (hloubka z pohledu shora), aby nepršelo pod střechou ani v budovách; dopady a cákance na zemi; **mokré povrchy** (tmavší albedo, nižší roughness, louže podle masky/výšky, kroužky jako animovaná normála), postupné vysychání po dešti. Zvuk deště venku vs. tlumený uvnitř podle stejné mapy.
- **Bouřka a blesky.** Záblesk = krátké silné směrové světlo + rozsvícená obloha (pár snímků, nepravidelně), blesk jako sprite/mesh v dálce, **hrom se zpožděním podle vzdálenosti** (343 m/s) a s dozvukem. Vítr zesílí.
- **Vítr** jako jeden globální vektor: vlnění trávy a stromů ve vertex shaderu (M2 Vegetace), částice, plachty, zvuk.
- **Mlha a opar.** Výšková mlha už je; doplnit barvu mlhy z oblohy podle směru (aerial perspective) a **volumetrickou mlhu**: froxel mřížka (např. 160 × 90 × 64) ve compute shaderu, rozptyl ze slunce se CSM stíny a z lokálních světel, časové vyhlazení. Dává sluneční paprsky lesem, kužele baterek a světla v mlze.

### 6.3 Barvy: desaturovaná paleta a grading

- **3D LUT** (32³) na konci post-processu: neutrální LUT se uloží spolu se screenshotem, upraví v Krita/GIMP/Resolve a načte zpět. Každý preset počasí má vlastní LUT a při přechodu se prolínají. Dnes máme saturaci, kontrast, tint, vinětu a zrno – LUT je nadstavba nad nimi.
- **Lift/gamma/gain a vyvážení bílé** v Environment doku pro rychlé ladění bez LUT.
- **Disciplína palety v assetech:** albedo v rozsahu cca 30–240 sRGB, žádné syté barvy kromě cílených akcentů (výstražné cedule, ohně, anomálie). Kontrola v asset validaci editoru. Desaturuje se gradingem, ne v texturách, aby šlo měnit náladu.

### 6.4 Zvuk: polovina atmosféry

- **Vrstvy ambientu.** (1) Podkladové smyčky podle prostředí a počasí (vítr podle síly, venku/uvnitř, noc/den), (2) **náhodné jednorázové zvuky v prostoru** kolem hráče: vytí psů, vrány, vzdálená střelba, skřípání plechu, praskání větví – každý s pravděpodobností podle denní doby a oblasti, umístěný 50–400 m daleko ve 3D, (3) bodové zdroje v levelu (rádia s filtrovaným zvukem, generátory, kapající voda), (4) situační hudba (kytara u ohně jako 3D zdroj v táboře, ne hudba v pozadí).
- **Zvukové oblasti** v editoru (objem s vlastní sadou vrstev a prolnutím na hranici), **reverb zóny** (interiér, tunel, otevřená pláň) a **okluze** zdí – položky „Audio dál“ v M1.
- **Řízené ticho.** Před událostí (anomálie, bouřka) se ambient stáhne (ducking), aby náhlý zvuk zapůsobil.
- **Zvuky ze simulace.** Vzdálená střelba nemá být jen náhodná: když se offline skupiny NPC střetnou (6.5), hráč ji slyší ze správného směru.
- **Detektor/radiace:** praskání jako Poissonův proces s frekvencí podle dávky; pípání detektoru anomálií podle vzdálenosti.

### 6.5 Život v zóně (A-Life)

- **Online/offline podle vzdálenosti.** NPC blíž než ~150 m jsou plné entity (fyzika, animace, navmesh, Lua AI); dál jen záznamy v simulaci. Přepínání s hysterezí, aby NPC neblikala.
- **Graf zóny** (body zájmu a cesty mezi nimi, i přes nenačtené buňky) pro offline přesuny; navmesh jen online.
- **Tábory a „práce“.** Body zájmu nabízejí role (hlídka, spánek, sezení u ohně, obchod) s rozvrhem den/noc; skupiny si vybírají cíle podle frakce a potřeb.
- **Offline střety** se vyhodnotí statisticky (síla skupin, náhoda) a zanechají následky: mrtvá těla a loot, když tam hráč dorazí, a zvuk střelby (6.4).
- **Fauna:** smečky psů s doupaty, vrány na stromech, které vyletí při přiblížení.
- Vše se ukládá s GUID (M0.5) a musí přežít streamování buněk (M2).

### 6.6 Anomálie, ohně a detaily

- **Zkreslení vzduchu:** průchod distorze – částice/meshe zapisují posun UV do bufferu, post-process pak posune obraz scény (s maskou, aby se nezkreslovaly předměty před anomálií). Stejný průchod dá tetelení nad ohněm a výfuky.
- **Anomálie (vlastní design):** trigger + distorze + částice + hučení + typ poškození + reakce na hozený předmět; vyhledává se detektorem.
- **Táborák jako prefab:** blikající bodové světlo se stínem, oheň/kouř/jiskry (máme flipbooky), praskání, sedací body pro NPC, volitelně hudba.
- **Detaily obsahu:** decaly nápisů a skvrn, rozptyl odpadků a suti štětcem (Foliage režim na malé meshe), vraky, prach v paprscích světla (částice zapnuté jen v kuželu).
