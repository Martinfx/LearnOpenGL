# Generátor města

Nástroj v editoru (**Edit > City generator**, dock vpravo) a v enginu (`engine/CityGen.h`), který z navržené mřížky
ulic a z modulárního kitu postaví celé post-sovětské město: ulice s chodníky a obrubníky, bloky činžáků s průjezdy
do dvorů, panelová sídliště, věžáky, garáže, parky, ruiny, pustiny, pouliční lampy s kabely, vraky, stromořadí a
zarůstání. Výsledek jsou obyčejné boxy, modely a skupiny v levelu, takže se dají po vygenerování ručně upravit.

Pravidla `docs/LEVEL_ART_GUIDE.md` platí dál: generátor je pomocník pro rozvržení a „správně změřenou“ výplň,
cesty, zajímavá místa a příběh lokace se navrhují ručně.

## 1. Jak to dělají profesionální nástroje (stručná rešerše)

- **CityEngine (Parish a Müller, SIGGRAPH 2001; Müller a kol., SIGGRAPH 2006).** Síť ulic, z ní bloky, bloky se dělí
  na parcely a na parcelách roste budova podle pravidel tvarové gramatiky CGA (hmota -> patra -> pole fasády ->
  okna). Klíč: každý krok je pravidlo s parametry, ne náhodné rozházení. Zdroje: history.siggraph.org/?p=116923,
  „Procedural Modeling of Buildings“ (history.siggraph.org/?p=113547).
- **Houdini ve hrách (Marvel's Spider-Man, Insomniac, GDC 2019).** Manhattan postavený procedurálně z dat a pravidel,
  pak ruční dokončení umělci; nástroj musí nechat výsledek ručně upravit a umět ho přegenerovat po částech.
  Zdroje: gdcvault.com/play/1025765, vimeo.com/326448655 (David Santiago, „Marvel's Spider-Man, meet Houdini“).
- **Modulární kity (Skyrim/Fallout, Joel Burgess, GDC 2013).** Malá sada dílů se stálou mřížkou a rozměry, díly
  na sebe přesně navazují; kvalita je v měřítku a v tom, že se díly neopakují vedle sebe.
  Zdroj: slideshare.net/JoelBurgess/gdc2013-kit-buildingfinal, gamedeveloper.com/design/skyrim-s-modular-approach-to-level-design.
- **Wave Function Collapse (Townscaper, Oskar Stålberg).** Lokální pravidla sousedství dílů řešená jako omezení.
  Výborné pro drobné modulární prvky (střechy, rohy), pro uliční síť a bloky se nehodí: neumí globální kompozici
  (osy, průhledy, hierarchii ulic). Proto ho tu nepoužíváme na rozvržení.

Z toho plyne návrh: **navržená mřížka + pravidla pro každý druh bloku + modulární kit + náhoda jen mezi
povolenými možnostmi.**

## 2. Co generátor dělá a podle jakých pravidel

1. **Ulice.** Mřížka sloupců a řad bloků (šířky v metrech). Každá čára mřížky je ulice třídy *main / side / alley /
   none*. Profil třídy je přesný: hlavní ulice 9 m vozovka + 2 × 3,5 m chodník = 16 m mezi fasádami (jako zarostlá
   ulice), vedlejší 7 + 2 × 2,5 m, ulička 4 + 2 × 1,5 m. Chodníky a pozemky jsou 15 cm nad vozovkou. 1 jednotka = 1 m.
2. **Bloky činžáků (perimeter).** Uliční čára je uzavřená od rohu k rohu. Rohy jsou „větrník“: dům na rohu jde
   podél jedné ulice a jeho štít lícuje s druhou. Fronta se plní **přesně** (chyba 0,00 m, hlídá to test): domy
   z kitu (délky po 3 m) se vybírají prohledáváním s pravidly a zbytek délky si vezmou průjezdy do dvora
   (4–9 m, asi jeden na 70 m), rozložené rovnoměrně. Pravidla: nikdy stejný dům vedle sebe ani A-B-A, sousedé se
   liší nejvýš o 3 patra (plynule stupňovaná silueta), rohový dům je z nejvyšších (akcent). Průjezd má pilíře a
   rezavá vrata (zavřená, pootevřená nebo jedno křídlo chybí). Ve velkém dvoře řada garáží, která nechá volnou
   cestu od průjezdu.
3. **Sídliště (microdistrict).** Panelové domy v řadách rovnoběžně s delší stranou bloku, mezera mezi řadami
   = výška vyššího domu (nejméně 24 m, oslunění), vyšší řady na sever, odstup od ulice 12 m, vše vycentrované.
   Od vchodů vedou vyšlapané cesty k nejbližší ulici, pod balkony keře, v rozích háje, v pásu u ulice stromořadí.
4. **Věžáky, garáže, park, ruina, pustina, prázdný pozemek.** Věžáky jako orientační body (podle reference
   s vysokými paneláky), garáže v řadách s průjezdnou uličkou, park se dvěma vyšlapanými úhlopříčkami a háji,
   ruina s haldami suti a spadlými panely podél ulice a mladým lesem za nimi, pustina s plevelem a nálety.
5. **Uliční vybavení podle míry.** Obrubníky v celých 6 m dílech (zbytek rozdělený na oba konce), lampy po 30 m
   střídavě na obou stranách ulice (v souřadnici ulice, takže sedí přes celé město), kabely lampa–lampa a napříč
   ulicí mezi fasádami, stromořadí jednoho druhu na celou ulici, vraky zaparkované u obrubníku podél ulice.
6. **Příroda a rozpad (LEVEL_ART_GUIDE, kap. 4).** Ze stromořadí zbylo 60 % stromů (zbytek pahýly, suché stromy nebo
   semenáček v jámě), u přeživších semenáčky, ve spáře obrubníku tráva a trsy, u zdí kopřivy, v průjezdech keře,
   háje v rozích dvorů, kolem vraků tráva. Střed ulic a vyšlapané cesty zůstávají volné. Kolem města pustina
   s mladým lesem, do které pokračují silnice. Míra zarůstání je jeden posuvník (Overgrowth 0–1).

**Stabilita:** každý blok má vlastní seed odvozený ze seedu města, takže změna jednoho bloku nepřehází ostatní
(„Re-roll selected blocks“ přegeneruje jen vybrané). Generátor má vlastní generátor náhodných čísel, takže stejné
nastavení dá stejné město na Windows, Linuxu i FreeBSD.

### Organický režim (Layout → Street layout: Organic)

Druhý režim vedle mřížky, podle přehledu algoritmů, který poslal martin. Každý krok je popsaný pravidlem:

1. **Silniční síť z tenzorového pole** (Chen, Esch, Wonka, Müller, Zhang: *Interactive Procedural Street
   Modeling*, 2008; `engine/CityRoads.h`). Pole je směs mřížek (jedna na čtvrť, natočené až o *District turn*),
   radiálního pole kolem centra (okružní a paprskové ulice, *Ring roads*) a pomalého šumu (*Curvature*, *Bend
   scale*). Hlavní silnice se trasují jako proudnice pole (RK2, krok 3 m) v odstupu *Main road spacing*, pak
   vedlejší ulice mezi nimi (*Side street spacing*, rozmístění podle Jobard–Lefer). Krátké konce se protáhnou
   k nejbližší ulici, slepé konce se odříznou, z průsečíků vznikne rovinný graf a z jeho stěn bloky (každá hrana
   bloku ví, jaká ulice podél ní vede). Kolem města volitelně obvodová silnice.
2. **Zónování a parcely.** Blok se zmenší o polovinu šířky ulic (podle třídy každé hrany) a podle vzdálenosti od
   centra dostane druh: v centru činžákové bloky, ve vnitřním městě činžáky / sídliště / věžáky, venku rodinné
   domy, garáže a pustina; podíl ruin a parků je nastavitelný. Úzké nebo malé bloky jsou zeleň. Činžákový blok se
   dělí na pásy podél ulic (mitre inset) a ty na parcely s průjezdy; rodinné domy rekurzivním dělením bloku na
   parcely s frontou k ulici (dům stažený od uliční čáry, ovocné stromy na zahradě).
3. **Domy podle gramatiky fasád** (zjednodušená CGA, Müller a kol. 2006; `engine/CityGrammar.h`). Půdorys parcely
   → stěna pro každou hranu podle role (ulice / dvůr / štít k sousedovi) → patra → osy oken → dlaždice (okno,
   lodžie, balkon, dveře, výloha). Okna jsou rozbitá, zatlučená nebo prázdná, pod parapety stékající špína, ruiny
   mají patra uražená po sloupcích. Střecha valbová nebo plochá s atikou. Výsledek je .glb (a _lod1.glb) ve složce
   *House models*; stejný dům se zapíše jen jednou (soubor je pojmenovaný hashem parametrů). Styly (činžák,
   panelák, chruščovka, rodinný dům – patra, výšky podlaží, rozteč os, okna, materiály, lodžie, balkony, výlohy)
   jsou tabulka v kitu.
4. **Terén a hustá tráva.** Pod městem vznikne terén (krok *Terrain spacing*): vozovka, chodníky, dvory, cesty
   a zastavěná plocha jsou namalované jako vrstvy, mimo město se zvedají kopce (*Hills*). Tráva a plevel rostou
   přes foliage terénu (instancované, tisíce kusů na blok) s hustotou podle druhu země – tabulka *Ground cover*
   v kitu, celkově *Grass density*. Zvláštní druh země *verge* je spára podél obrubníků a pás u paty zdí: tam
   tráva a plevel rostou nejhustěji, jako v opravdu opuštěném městě.
5. **Náměstí** (*Town square in the centre*): nejbližší dost velký blok u centra je náměstí – cíl mapy. Dlažba
   s trávou ve spárách po 6 m, pomník (obelisk s hvězdou na stupňovitém podstavci) na dlouhé ose, před ním suchá
   kašna, když je místo, kolem pomníku lavičky, po obvodu lípy (část uschlá), lampy v rozích, u nejrušnější ulice
   kiosk, sem tam náletový stromek. Na mapě PDA má značku *Площадь*. Modely jsou v
   `assets/models/zone/source/zonegen/square.py` (role `monument`, `fountain`, `bench`, `kiosk` v kitu).
6. **Optimalizace** zůstává na enginu: LOD každého domu, instancing sdílených meshů, frustum culling, Hi-Z
   occlusion. Textury domů se sdílí mezi soubory (jedna kopie v paměti pro celé město).

Město 640 × 640 m: ~200 úseků silnic, 80 bloků, 330 parcel, 370 domů; plán za 0,1 s, domy se zapíšou za ~3 s
(paralelně), level se načte za ~10 s.

### Betonové skelety (styl role `skeleton` / `skeleton_tower`)

Část panelových domů, chruščovek a tenementů s plochou střechou (Town plan → Zoning →
**Concrete skeletons**, klíč `skeleton_share`, v ruinových blocích dvojnásobně) zůstane jen jako
holá železobetonová kostra na stejném půdorysu. Vlastní gramatika (`SkeletonBuilder` v CityGrammar.cpp):

- **Rám:** mřížka sloupů po `bay` metrech přes orientovaný obdélník půdorysu, buňky uvnitř půdorysu.
- **Patra:** stropní deska na buňku (náhodně propadlé, nahoře víc), sloupy v uzlech, obvodové průvlaky.
- **Obvod:** parapetní panely (podíl `balconies`), výplňové stěny s prázdným oknem (podíl `loggias`)
  nebo nic; přízemí otevřené.
- **Zřícení:** jedna strana spadla – patra ubývají směrem k ní, z ulomených sloupů a hran desek trčí
  rezavá výztuž, na hromadě leží šikmé desky, opřený nosník a kusy betonu (jen LOD0).
- **LOD1:** sloupy, desky a jednostranné stěny (~450 trojúhelníků místo ~2,5 tisíce).
- Kolem stavby generátor nasype suť z kitu (rubble/slab) a semenáčky; půda pod skeletem je `Lot`,
  takže skrz přízemí roste tráva. Popínavky po obvodu jako u ostatních domů, bez stromků na střeše.
- Styly v kitu: „Concrete skeleton“, „Burnt frame“ (role skeleton), „Skeleton tower“ (role
  skeleton_tower, 14–22 pater – část věžáků v blocích Towers). Materiál `frame_concrete` je šedý beton.

### Názvy ulic (New York)

Town plan → Zoning → **Street names on the map** (`street_names`) pojmenuje ulice po newyorsku:
ulice podél hlavního směru města jsou avenue (od západu 12th Ave … Broadway … 1st Ave, York Ave),
příčné jsou číslované (W 14th St … od jihu k severu, počátek podle seedu). Úseky na jedné přímce sdílejí
název, každý název má na mapě PDA jednu značku (na nejdelším úseku). Název náměstí: **Square name**
(`square_name`, výchozí „Union Square“). Všechny názvy v generátoru jsou anglické/americké.

### Rozpraskané silnice (Nature and decay → Road decay)

`road_decay` 0..1 (editor: Nature and decay → **Road decay**). Na každé ulici: podélné praskliny ve
stopách kol a uprostřed, příčné mrazové praskliny, síťové praskliny a staré asfaltové záplaty
(plošné decaly z kitu, role `road_crack`, `road_crack_cross`, `road_net`; modely z
zonegen/road_decals.py, instancované, bez stínů a kolizí), výtluky (decal `pothole` + prohlubeň v
terénu, kde se při dešti drží voda, tráva na okraji), u obrubníku rozevřená spára s trávou a na
chodnících spáry mezi deskami s trávou, propadlé a nadzvednuté desky.

### Tovární haly (blok Industrial, styl role `hall`)

Town plan → Zoning → **Factory halls** (`industry_share`): vnější bloky s 1–3 opuštěnými halami.
Gramatika `HallBuilder`: ocelové rámy po `bay` m (I-sloupy, příhradové vazníky, vaznice), cihlové pilíře
a sokl, ocelová okenní mříž mezi pilíři (zbylé tabule = podíl `loggias`), sedlová střecha z plechů
(část propadlá – dovnitř padá světlo) se světlíkem u hřebene, štít s vraty bez křídel a štít s vysokým
oknem, ochoz na konzolách se zábradlím a schodištěm (šance `balconies`, jedno pole spadlé), rezavé
potrubí podél stěny (jedno prasklé), prkna, plechy a cihly na podlaze. Výška okapu = `ground`.
Uvnitř generátor zasadí kapradiny (kit `fern`), plevel, semenáčky a z vazníků visící popínavky
(kit `hanging`: břečťan otočený vzhůru nohama). LOD1 ~1,2 tisíce trojúhelníků (LOD0 ~8 tisíc).

### Podloubí a zarostlé balkony

Styl má parametr **Arcade** (`arcade`, šance): uliční fasáda domu se 3+ patry dostane podloubí –
sloupy v osách 2,2 m před fasádou (nad chodníkem), deska s průvlakem ve výšce přízemí, za ním výlohy;
občas sloup chybí. Generátor pod podloubím nesází stromy ani lampy a na desku dá plevel, keře a semenáčky.
Balkony (z gramatiky, `balconySpots()`): v truhlících malé keře/plevel, přes zábradlí visí popínavky
(kit `hanging`). V kitu: Housing project 9/16, Five-storey walk-up, Beige/Dark brick/Brick tenement.

### Požární schodiště a štíty obchodů (New York)

Styl má parametr **Fire esc.** (`fire_escape`, šance): uliční fasáda domu se 4+ patry a 4+ osami bez
podloubí dostane ocelové požární schodiště – plošiny s zábradlím v každém patře od 2. NP, střídavá
ramena, spouštěcí žebřík nad chodníkem (LOD1 jen plošiny). V kitu: Brick/Stucco/Beige/Dark brick tenement.
Každý dům s obchodním parterem (i pod podloubím) má nad výlohami vybledlé štíty – 16 návrhů
(DELI, PHARMACY, LAUNDROMAT, PIZZA, PAWN SHOP, DINER, …) v atlasu `zone/textures/shop_signs_albedo.png`
z `zonegen/shop_signs.py` (materiál `shop_signs`, alfa blend, olupující se barva, rez, chybějící písmena).
Výběr štítků má vlastní RNG, takže ostatní domy se nemění.

### Dominanta (Landmark tower)

Town plan → Zoning → **Landmark tower** (`landmark`, `landmark_floors`, `landmark_name`): v bloku
severně od centra (směrem od jižního vjezdu přes náměstí) stojí jediná velmi vysoká holá kostra
(styl role `skeleton_tower`, výchozí 34 pater, ~100 m) se zřícenou stranou; blok kolem ní je pustý
(bříza, plevel). Na mapě má štítek (výchozí „Liberty Tower“).

## 3. Editor

- **Layout:** seed, střed (tlačítko *Camera*), otočení, kořenová skupina, šířky sloupců a řad, třídy ulic na
  čarách mřížky, druh každého bloku.
- **Town plan:** organický režim – silniční síť (velikost, hlavní směr, čtvrti a jejich natočení, okružní ulice,
  zakřivení, rozteče hlavních a vedlejších ulic, obvodová silnice), zónování (centrum, vnitřní město, podíl
  rodinných domů, ruin a parků) a terén (krok, kopce, hustota trávy, složka generovaných domů).
- **Rules:** profily tří tříd ulic (vozovka, chodník, lampy, stromořadí, kabely napříč), vybavení, pravidla bloků
  činžáků a sídlišť, zarůstání a okraj města. Každá hodnota má nápovědu.
- **Kit:** soubor kitu, materiály země, tabulka budov (role, délka, hloubka, fasáda, patra, výška, váha; *Add
  building* a *Measure selected* změří model), tabulka rekvizit a rostlin podle rolí, sady textur, tabulka stylů
  fasád (materiály z nabídky) a tabulka pokryvu země (model, hustota, podíl na každém druhu země, LOD, vítr).
- **Náhled:** plán (osy ulic, pozemky, půdorysy, průjezdy, cesty) se kreslí do viewportu při každé změně, počítá se
  na pracovním vlákně. Pod tím statistika a varování (např. blok je na sídliště malý).
- **Generate:** modely se načtou paralelně na job systému (každý soubor jednou, kopie sdílejí mesh), do levelu se
  vloží jako jeden krok Undo. Předchozí město (všechno ve skupině *City*) se nahradí, ruční objekty mimo ni zůstanou.
  *Remove town* skupinu smaže.

Soubory: nastavení `assets/city/*.city`, kit `assets/city/postsoviet.citykit` (text, cesty relativně).
Příkazová řádka (stejný generátor): `city_gen <settings.city> <out.level> [--base level] [--svg plan.svg] [--plan-only]`.

## 4. Omezení a další kroky

- Kit tvoří domy zarostlé ulice (11 činžáků), dva panelové domy, tři podrobné věžáky (lodžie, vchody, strojovna
  výtahu) a garáže; organický režim k tomu staví domy gramatikou. Víc stylů = víc variability.
- Náměstí a parky jako cíl sítě (ne jen zbylé bloky) a řeky / železnice jako hranice pole zatím nejsou.
- Mapa **Mrtvé město** (`assets/levels/zone_city.level`, martin schválil 2. 11. 2026) je vygenerovaná z
  `assets/city/zone_city.city`; domy jsou v `assets/models/city/zone_city/`. Start na jižním příjezdu, cíl centrum;
  start, název, popis a značka jižního příjezdu jsou ruční a přegenerování je zachová
  (`city_gen assets/city/zone_city.city zone_city.level --base <kopie zone_city.level>`).
- Další mapy z generátoru jen po schválení.
