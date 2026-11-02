# Realistické osvětlení v reálném čase – jak to dělají nejlepší enginy

Stav k 2026-11-02 (větev `engine`). Martinovo zadání: „real-time, reálná atmosféra, reálné shadery,
reálné osvětlení – jak to dělají nejlepší enginy“. Cíl: ponurá postsovětská ulice pod zataženou
bouřkovou oblohou, opar v dálce, mokré povrchy a zarostlá vegetace (`overgrown_street.level`).

Zdroje: veřejná dokumentace a přednášky (SIGGRAPH *Advances in Real-Time Rendering*, GDC):
UE5 Lumen (Wright, Narkowicz, Kelly 2022), Frostbite *Physically Based Sky, Atmosphere and Cloud
Rendering* (Hillaire 2016, 2020), *Moving Frostbite to PBR* (Lagarde, de Rousiers 2014),
Decima / Horizon Zero Dawn *Real-time volumetric cloudscapes* (Schneider 2015, 2017),
id Tech 6/7 *DOOM* (Courrèges; Sousa, Geffroy 2016), RE Engine (CAPCOM 2019–2023),
CryEngine SVOGI, NVIDIA RTXGI/DDGI (Majercik et al. 2019), AMD GI-1.0 a Brixelizer,
Activision *Practical Real-Time Strategies for Accurate Indirect Occlusion* (GTAO, Jimenez 2016),
Call of Duty *Precomputed lighting* (Iwanicki 2013–2023), Ubisoft *Assassin’s Creed* a *Far Cry*
(precomputed GI, sky visibility), GSC *S.T.A.L.K.E.R. 2* (UE5, Lumen + virtuální stíny).
Srovnání s naším kódem je z přímého čtení repozitáře.

## 1. Co dělá obraz „reálným“ (pořadí podle dopadu)

Studie produkčních enginů se shodují na pořadí, ve kterém lidské oko pozná falešné světlo:

1. **Nepřímé světlo a zastínění (GI + AO).** Bez něj jsou stíny ploché, tmavé nebo naopak
   svítící; kouty nemají kontakt, předměty „plavou“. Pod zataženou oblohou je to *téměř celé*
   osvětlení – slunce je slabé a světlo přichází z oblohy a odrazů od stěn a země.
2. **Atmosféra a vzdálenost.** Opar, který barví vzdálené věci barvou oblohy (vzdušná
   perspektiva), světelné paprsky, nízká oblačnost. Dává měřítko a hloubku.
3. **Správná energie materiálů (PBR).** Lineární světlo, fyzikální jednotky, energeticky
   zachovávající BRDF, zrcadlová okluze, mokré povrchy.
4. **Stíny s přirozenou měkkostí.** Ostré u paty předmětu, měkké dál od něj; pod mraky měkké vše.
5. **Kamera.** Expozice, tónové mapování, bloom, mírná zrnitost – ne „filtr“, ale film.

## 2. Jak to řeší jednotlivé enginy

| Oblast | UE5 (Lumen) / STALKER 2 | Frostbite (Battlefield, Star Wars) | Decima (Horizon) | id Tech 7 (DOOM Eternal) | RE Engine | CryEngine |
|---|---|---|---|---|---|---|
| Nepřímé difúzní | Lumen: surface cache + SDF / HW ray tracing, screen probes s temporálním filtrem | Předpočítané GI (Enlighten) + světelné sondy, nově ray-traced GI | Předpočítané světelné sondy + denní cyklus (sady pro více časů) | Pečené lightmapy + mřížka sond pro dynamické | Pečené sondy + SSGI, nověji RTGI | SVOGI (voxelový cone tracing) |
| Obloha | Sky Atmosphere (Hillaire LUT: transmitance, multiple scattering, sky-view, aerial perspective) | Hillaire 2016/2020 – autoři téhož modelu | Raymarchované objemové mraky + fyzikální obloha | Ručně kreslená skybox + světelné objemy | Fyzikální obloha | Fyzikální obloha + 3D mraky |
| Mraky | Volumetric Cloud (raymarch, Perlin-Worley šum) | Volumetrické mraky (Hillaire) | Vynálezce moderních objemových mraků (Schneider) | 2D vrstvy | Objemové | Objemové |
| Mlha | Exponential Height Fog + Volumetric Fog (froxely 160×90×64), stíny ve froxelech | Froxelová objemová mlha (Hillaire 2015 – zdroj UE techniky) | Froxely | Froxely + ruční „light shafts“ | Froxely | Froxely |
| AO | Lumen short-range AO / GTAO, distance field AO | HBAO/GTAO + specular occlusion | SSAO + baked AO | Baked AO + SSAO | GTAO | SSDO |
| Stíny slunce | Virtual Shadow Maps (16k virtual), contact shadows, PCSS „soft“ | Kaskády + PCSS/contact shadows | Kaskády + screen-space shadows | Kaskády | Kaskády + contact | Kaskády + POM shadows |
| Odrazy | Lumen reflections + SSR | SSR + lokální cubemapy (parallax corrected) | SSR + sondy | SSR + cubemapy | SSR + cubemapy | SSR |
| Tónování | ACES (filmic), od 5.x volitelně neutral / „Filmic Tonemapper“ | Vlastní filmic, HDR výstup | Vlastní | Vlastní filmic | ACES | Filmic |
| Vegetace | Two-sided foliage + subsurface profile, foliage AO | Translucence s tloušťkou (thickness map) | Translucence + wrap diffuse | — | Translucence | SSS |

**STALKER 2** (to, co Martin bere jako měřítko): UE5 s Lumenem (GI i odrazy), virtuální stíny,
Sky Atmosphere + Volumetric Cloud, výšková objemová mlha s paprsky, ACES, silná barevná korekce
(desaturovaná zelená, studené stíny). Hodně „reality“ dělá *atmosféra a GI*, ne samotné textury.

**Shoda napříč enginy:**
- Difúzní GI se dělá **sondami** (pečené nebo dynamicky aktualizované) – lightmapy jen pro
  statiku. Dynamické GI v reálném čase (Lumen, DDGI) = sondy, které se průběžně „dosvěcují“
  paprsky. Bez RT hardwaru se paprsky nahrazují *signed distance fields* (Lumen SW) nebo
  *screen-space* trasováním (SSGI) s pečenými sondami jako zálohou.
- **Obloha = jeden fyzikální model** (Rayleigh + Mie + ozon, Hillaire LUT), ze kterého se bere
  barva oblohy, barva slunce (podle výšky), světlo oblohy pro sondy i *vzdušná perspektiva* –
  vše je spolu konzistentní, proto to „sedí“.
- **Mlha = froxely** s fázovou funkcí Henyey-Greenstein a stíny ze slunce; daleko analytická
  výšková mlha. Mlha bere barvu z oblohy, ne z ručně zadané barvy.
- **AO je víc druhů naráz**: velké měřítko (sondy / DFAO / sky visibility), střední (GTAO),
  malé (textura) + **zrcadlová okluze**, aby kouty neodrážely oblohu.
- **Stíny**: měkkost podle vzdálenosti od vrhače (PCSS) a *contact shadows* (krátký paprsek
  v hloubkovém bufferu) pro malé detaily, na které kaskády nestačí.

## 3. Co už máme (a jak to obstojí)

| Prvek | Náš stav | Poznámka |
|---|---|---|
| PBR | GGX + Smith + Schlick, metal/rough, IBL z prefiltrovaného env mapu, BRDF LUT | Chybí kompenzace víceodrazové energie a zrcadlová okluze |
| Nepřímé světlo | Pečené sondy (irradiance mřížka + cubemapy, `<level>.probes`), jinak env mapa oblohy | Statické; venku žádný odraz od stěn/země ulice; dynamika jen přes intenzitu |
| AO | SSAO (pevný poloměr) | Malé měřítko; „kaňon“ ulice nemá zastínění oblohy |
| Obloha | Procedurální gradient zenit/horizont + 2D mraky v `skyColor` | Barva slunce a mlhy se nastavuje ručně – nekonzistence |
| Mlha | Froxelová objemová mlha (paprsky, lokální světla, šum, anizotropie) + analytická výšková | Dobrý základ; barva mlhy ruční, ne z oblohy |
| Stíny | 4 kaskády, 3×3 PCF, stíny mraků, lokální stíny | Bez měkkosti podle vzdálenosti, bez contact shadows |
| Odrazy | SSR + sondy | OK |
| Kamera | ACES, auto expozice, bloom, LUT, white balance, lift/gamma/gain | OK, chybí neutrální/AgX volba |
| Počasí | Déšť (mokro, kaluže s vlnkami), blesky, bouře | OK |

## 4. Plán implementace (pořadí podle poměru dopad / cena)

Vše v enginu, nastavitelné v editoru (Environment), s výchozími hodnotami, které nemění
staré levely víc, než je nutné. Výkonový cíl: střední GPU 60 fps na ulici při kvalitě High.

1. **Fyzikální atmosféra (Hillaire LUT)** – transmitance + multiple scattering + sky-view LUT
   (malé textury, přepočet jen při změně slunce/počasí). Z ní: barva oblohy, barva a síla
   slunce podle výšky, světlo oblohy do env mapy a *vzdušná perspektiva* (opar barvy oblohy
   podle vzdálenosti). Zatažení (`cloud_cover`) tlumí přímé slunce a zvedá difúzní šedé světlo.
   Ruční barvy zenit/horizont zůstanou jako „tint“ (kompatibilita).
2. **Zastínění oblohy v ulici (sky visibility / bent normal AO)** – pod zataženou oblohou je
   to hlavní světlo. Levně: *GTAO* s větším dosahem + odvozená bent normal pro dotaz do env
   mapy; plus „horizon mapa“ z hloubkové mapy shora (heightfield sky occlusion jako Far Cry /
   AC) pro velké měřítko – fasády ulice zakryjí oblohu dole v kaňonu.
3. **Zrcadlová okluze + víceodrazová energie** (Lagarde 2014, Fdez-Agüera 2019) – kouty
   přestanou svítit odrazem oblohy, drsné kovy (rez) nebudou tmavé.
4. **Měkké stíny (PCSS na kaskádě 0–1) + contact shadows** – ostré u paty, měkké dál, drobné
   detaily (tráva, dlažba, rámy oken) dostanou kontaktní stín.
5. **Nepřímý odraz od povrchů v reálném čase (SSGI, 1 odraz) nad sondami** – obrazovkové
   trasování po hloubce s temporálním filtrem (jako UE5 SSGI / RE Engine), záloha = sondy.
   Dá barevný odraz od stěn a země („zelené“ světlo pod korunami, teplé od cihel).
6. **Mlha z atmosféry** – barva froxelové i analytické mlhy z modelu (bod 1), nízká oblačnost
   / přízemní opar podle výšky, bouřkové paprsky pod mraky.
7. **Tónové mapování: volba ACES / AgX / neutral** (Khronos PBR Neutral) – AgX lépe drží
   saturované jasy (zapadající slunce, oheň) bez „přepálené žluté“.
8. *(Později, až bude měření z Martinovy GPU)* objemové mraky (Schneider/Hillaire) a
   dynamické sondy (DDGI-like dosvícení pečených sond paprsky v SDF nebo hloubkových kostkách).

Vegetace: translucenci listů a trávy (průsvit proti slunci) přidává vlákno levelu do
`pbr.frag`; engine ji zachová a napojí na nastavení prostředí.

### Stav (2026-11-02)

| Krok | Commit | Nastavení (level `env` / editor) |
|---|---|---|
| 1. Fyzikální atmosféra | 31b4c7a | `atmosphere`, `atmosphere_haze`, `atmosphere_brightness` (Environment > Sky) |
| 2. Zastínění oblohy | 905d9f1 | `sky_occlusion` (Environment > Sky) |
| 3. Zrcadlová okluze + víceodrazová energie | 905d9f1 | vždy zapnuto |
| 4. Měkké stíny (PCSS) | 6745a90 | `sun_size` (Environment > Sun), kvalita „Soft shadows (PCSS)“ |
| 5. Odražené světlo (SSGI) | 1b7595f | `bounce_light` (Environment > Sky), kvalita „Bounce light (SSGI)“ |
| 6. Přízemní opar, barva oparu podle směru | b054eb9 | `ground_haze`, `ground_haze_height`, `ground_haze_thickness` (Environment > Fog) |
| 7. Tónové mapování ACES / AgX / neutral | viz git log | `tonemap` 0/1/2 (Environment > Camera and image: „Tone mapping“) |

Contact shadows (krátký paprsek v hloubce) zatím nejsou: v hlavním průchodu už není volná
texturová jednotka; přijdou s odloženým (deferred) stínovým průchodem nebo spolu s bodem 8.

## 5. Ověření

Každý krok: snímek před/po z hráčovy kamery na ulici (stejná pozice a čas), golden testy,
měření `FPS_ENGINE_STATS` (časy průchodů) a nové hodnoty prostředí předané vláknu levelu.
