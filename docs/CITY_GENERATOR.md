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

## 3. Editor

- **Layout:** seed, střed (tlačítko *Camera*), otočení, kořenová skupina, šířky sloupců a řad, třídy ulic na
  čarách mřížky, druh každého bloku.
- **Rules:** profily tří tříd ulic (vozovka, chodník, lampy, stromořadí, kabely napříč), vybavení, pravidla bloků
  činžáků a sídlišť, zarůstání a okraj města. Každá hodnota má nápovědu.
- **Kit:** soubor kitu, materiály země, tabulka budov (role, délka, hloubka, fasáda, patra, výška, váha; *Add
  building* a *Measure selected* změří model) a tabulka rekvizit a rostlin podle rolí.
- **Náhled:** plán (osy ulic, pozemky, půdorysy, průjezdy, cesty) se kreslí do viewportu při každé změně, počítá se
  na pracovním vlákně. Pod tím statistika a varování (např. blok je na sídliště malý).
- **Generate:** modely se načtou paralelně na job systému (každý soubor jednou, kopie sdílejí mesh), do levelu se
  vloží jako jeden krok Undo. Předchozí město (všechno ve skupině *City*) se nahradí, ruční objekty mimo ni zůstanou.
  *Remove town* skupinu smaže.

Soubory: nastavení `assets/city/*.city`, kit `assets/city/postsoviet.citykit` (text, cesty relativně).
Příkazová řádka (stejný generátor): `city_gen <settings.city> <out.level> [--base level] [--svg plan.svg] [--plan-only]`.

## 4. Omezení a další kroky

- Kit zatím tvoří domy zarostlé ulice (11 činžáků), dva panelové domy, tři věžáky (jednoduché modely do dálky),
  garáže a vegetace. Víc druhů domů (rohové domy, delší paneláky, obchod, škola) = víc variability.
- Tráva a plevel jsou jednotlivé modely; hustý porost jako v zarostlé ulici by měl jít přes foliage terénu
  (vrstvy a hustota podle plánu generátoru).
- Mřížka je pravoúhlá (celá se dá otočit). Křivé ulice a náměstí by potřebovaly graf ulic místo mřížky.
- Generátorem se nedělají hotové mapy; nový level z něj vznikne až po schválení.
