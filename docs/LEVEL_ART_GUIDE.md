# Jak navrhovat a dekorovat lokace Zóny

Rešerše z 2026-11-02 (martin: „inspiruj se na internetu, jak to má vypadat a jak se mají navrhovat lokace“).
Shrnuje, co dělají profesionální level designéři a environment artisté, jak skutečně vypadá opuštěné
sovětské město po desítkách let, a z toho plynoucí pravidla pro naše levely. Na konci je audit
Zóny, Ulice u nemocnice a Skrundy-1 a co se podle toho změnilo.

## 1. Postup práce

1. **Příběh místa jako první.** Kde to je, kdo tu žil, co se stalo, kdo tu je teď. Každá lokace má
   napsaný odstavec dřív než první krabice. (80.lv, Paradise City; GMTK)
2. **Blockout celé mapy a ladění metrik a cest před detailem.** Tým STALKER 2 nejdřív postavil celou mapu
   z hrubých tvarů a „pečlivě testoval a ladil její metriky a trasy“, teprve pak dekoroval. Ověřit
   tloušťky zdí, výšku stropů, šířku dveří proti postavě hráče. (Rudyi, novyny.live; 80.lv, Into the Radius 2)
3. **Ručně dělané lokace.** GSC zkoušela procedurální generování a zahodila ho, protože „nesplňuje naše
   nároky na kvalitu“. Pro nás: generátory jsou pomocník (rozmístění trávy, suti, nápisů podle pravidel),
   ale rozložení lokace, cesty a zajímavá místa se navrhují ručně a s důvodem. (PC Gamer)
4. **Reference z místa.** Tým STALKER 2 jezdil opakovaně do Zóny, i do sklepů závodu Jupiter. My: fotky
   Pripjati, Skrundy-1, Černobylu, urbex videa, „stovky fotek“ stárnutí materiálů. (Rudyi; 80.lv)
5. **Každá sekce má téma a cílovou emoci,** pak se ověří hraním (intenzita v čase). Klid na otevřené
   zarostlé silnici, strach v podchodu s ostrými traverzami. (GMTK o Mass Effect 3; Game Informer o The Last of Us)

## 2. Rozložení lokace (Lynch, Upton)

Kevin Lynch (The Image of the City, 1960) popsal, z čeho si lidé skládají mapu města; Brian Upton
(Rainbow Six, GDC 2007) to převedl na hry:

- **Cesty** (silnice, koleje, prošlapané pěšiny): po nich se hráč pohybuje. „I malá mezera hráče přitahuje,
  i malá překážka ho odkloní.“ Silnice v Zóně jsou nebezpečné, protože jsou vidět na kilometry a
  vedou do přepadení (Edge o STALKERovi), takže pěšiny vedle nich mají smysl.
- **Hrany** (ploty, násypy, řeka, zdi): hranice, podél kterých se jde. „Hrany jsou bezpečnější, kryjí 180°
  překvapení.“ Překročení hrany odhalí výhled, přepadení nebo pohled zpět.
- **Uzly** (křižovatka, tábor, vchod do budovy): „věci se dějí v uzlech“. Tam patří příběh a souboje.
- **Čtvrti** (seřadiště, sídliště, elektrárna, mrtvé město): každá má vlastní atmosféru danou tématem a
  dekorací, ne geometrií.
- **Orientační body** („weenies“): chladicí věže, vodárna, radar, komín. Hráč se podle nich trianguluje
  a jde k nim. Z každého startu musí být vidět aspoň jeden. „Vzdálenost by měla odpovídat velikosti cíle.“
- **Chyby, kterým se vyhnout** (Upton): pořád stejná chodba, „bezcestný les“ (rovnoměrně rozházené stromy
  bez cest), cesta odnikud nikam, budova bez důvodu, slabý roh bez krytu. Edge kritizuje u STALKERa
  „opakující se budovy“ a „nepřesvědčivé stromy“.
- **Krytí**: na otevřené ploše řídké a vzácné (traverzy, ploty, vraky), v uzlech husté. Dlouhé výhledy =
  agorafobie, krátké = klaustrofobie; střídat. (Upton; Edge)
- **Logika prostoru jako nápověda**: zlato je v pánově komnatě, ne u sloužících (Thief). Hráč čte prostor
  reálnou logikou, takže vchody, cesty a odpadky musí odpovídat tomu, kudy lidé chodili. (GMTK)

## 3. Vyprávění prostředím

- Tři vrstvy, které si musí odpovídat: mikro scénky (mrtvola, spacák, vzkaz), architektura a rozložení
  (kdo prostor používal), stavba světa. Prostor má být „věrohodný“, ne nutně dokonale logický. (GMTK)
- Každý záběr má vypadat jako „scéna ze života, který se náhle zastavil“. (80.lv, Into the Radius 2)
- Věci mají důvod: prošlapaná pěšina od tábora ke studni, odpadky tam, kde se sedělo, saze nad oknem bytu,
  který hořel, zatékání pod děravou střechou, stalkerské značky na cestě. (naše pravidlo, platí od Skrundy)
- Hráčovy činy mění místo: vyrabovaný obchod, vyklizený tábor. (GMTK o Deus Ex)
- Rozbitý dům (`tenement(..., ruin=(od, do, patro))`, v ulici `RUINS` v `make_overgrown_street.py`): horní patra
  zřícená mezi dvěma zlomky délky, zlom se nahoru nepravidelně rozšiřuje a zeď končí zubatými cihlami, ne
  schodem; stropy visí přes okraj, trčí z nich pruty, střecha má holé krokve, okna u zlomu jsou vyhořelá se sazemi
  a suť leží v hromadě u fasády i na ulici. Takové domy dávají ulici siluetu (vidět jimi oblohu a vzdálená
  sídliště) a jsou orientační body; víc než dva tři na ulici z nich udělá kulisu války místo opuštěného města.
- Příroda leze i na domy: břečťan od chodníku (ivy_*, nejčastěji nízký kolem přízemí, místy až do 5. patra), břízky
  a javory na balkonech, v okapu římsy a na plochých střechách, tráva na otevřených patrech rozbitých domů. Místa
  pro semenáčky vrací generátor domu (`m.spots`: balcony, cornice, roof, ledge), level je osazuje vlastním rng.

## 4. Jak příroda bere opuštěné město

- **Pořadí:** do měsíce vysoká tráva a plevel (pampelišky, ambrozie), první rok praskající asfalt, kde
  stojí voda: nejdřív mech, pak plevel; do deseti let mladé stromy na parkovištích; po desítkách let
  popínavé rostliny po zdech a mech a plevel na střechách. (Futura-Sciences, populárně)
- **Praskliny asfaltu a obrubníky:** asfalt a beton se od sebe oddělí mrazem a provozem; ve spáře je voda,
  prach a sůl. Rostou tam jednoleté, nízké, rozprostřené rostliny s kůlovým kořenem, které snesou
  šlapání: rdesno ptačí, šrucha, pryšec, rosička, milička. Nízké koberce, ne vysoká tráva. (Del Tredici, Green Blacktop)
- **Pripjať po 25+ letech** (Laćan a kol. 2015, 40 ploch ve 4 prostředích): porost tvoří hlavně semenáčky,
  až 8 980 semenáčků javoru na hektar; nejhustší zmlazení podél ulic a v hájích; topol se zmlazuje
  všude, kde stojí vzrostlé stromy. Výhled: mozaika dubu a javoru podél ulic, v hájích a na trávnících,
  topol a osika na dlažbě. Les „bere vršky budov“, zeleň postupuje na vchody, balkony a okna, parky
  zdivočely. (agris.fao.org; David Lock)
- **Pravidla pro nás:**
  - mladé stromy hlavně u vzrostlých (semenáčky do 10 m od kmene), podél ulic a v rozích dvorů,
    jednotlivě i na chodnících a v prasklinách;
  - stromy ve skupinách (háje s podrostem keřů na okraji), ne rovnoměrně rozházené;
  - u zdí kopřivy a plevel, u vchodů a pod balkony keře;
  - na asfaltu nízké koberce u obrubníků, list a hlína nafoukané ke spárám;
  - holé, prošlapané pěšiny tam, kudy se chodí, a vysoká tráva tam, kudy ne.

## 5. Materiály, terén, světlo

- Míchání vrstev terénu šumem a výškou textury, měřítko textury podle vzdálenosti a makro maska proti
  opakování. Kameny a suť rozházené s náhodnou velikostí a natočením. (80.lv, Paradise City) Máme
  v `pbr.frag` od 2026-11-01.
- Opotřebení ve vrstvách: čisté, poškozené, staré ztmavlé. Rez a špína přes vlastní masky. (80.lv, Into the Radius 2)
- Stékání a šmouhy vody na všech plochách pod hranami. (DNEG, The Last of Us 2)
- Světlo: teplé slunce, výšková mlha, barevné korekce; cíl je „útulná apokalypsa“, ne šedivá kaše. Mlha
  v dálce, ale ne hnědá zeď. (80.lv; náš nález: oranžové slunce v levelu + denní cyklus = sépie)
- Barevný scénář: každá sekce zjednodušená na základní barvy a tvary. (Game Informer o The Last of Us)

## 6. Kontrolní seznam lokace

- [ ] Odstavec s příběhem místa; každé zajímavé místo má důvod.
- [ ] Ze startu je vidět orientační bod; hlavní cíl je vidět nebo naznačený.
- [ ] Cesty, hrany, uzly a čtvrti jsou čitelné; žádný „bezcestný les“ ani cesta odnikud.
- [ ] Prošlapané pěšiny spojují místa, kam lidé chodili (tábor, vchody, voda, silnice).
- [ ] Stromy ve skupinách s podrostem; semenáčky u vzrostlých stromů a podél ulic.
- [ ] Spáry asfaltu a obrubníky: nízké rostliny, list a hlína; u zdí kopřivy; u vchodů keře.
- [ ] Nápisy, skvrny a odpadky s důvodem (kdo, proč, kdy).
- [ ] Krytí řídké na otevřených plochách, husté v uzlech; střídání dlouhých a krátkých výhledů.
- [ ] Světlo: neutrální slunce v levelu (denní cyklus si večer dobarví sám), mlha nezakrývá orientační body.
- [ ] Ověřeno z pohledu hráče (1,7 m), ne jen shora.

## 7. Audit našich levelů (2026-11-02)

| Pravidlo | Zóna | Ulice u nemocnice | Skrunda-1 |
|---|---|---|---|
| Příběh, důvody | ano (hlavička levelu, NPC) | ano (úkol: lékárna v nemocnici) | ano (Eddie, poslední branci) |
| Orientační bod ze startu | chladicí věže | nemocnice | vodárna |
| Prošlapané pěšiny | **chybí** (jen silnice a koleje) | **chybí** k vchodům | ano (OSM chodníky, cesta k HQ) |
| Stromy ve skupinách | **rovnoměrně rozházené** | málo stromů | les na okraji, ve městě jednotlivě |
| Semenáčky, zmlazení | **chybí** | **chybí** | **chybí** |
| Spáry asfaltu | od dnes tráva u obrubníků | od dnes | tráva v prasklinách |
| Plevel u zdí, keře | od dnes | od dnes | ano |
| Nápisy se smyslem | od dnes (73) | **chybí** | ano |

Co se podle auditu dělá (commit po commitu, v `add_terrain.py` pro Zónu a Ulici, v `make_skrunda_level.py`
pro Skrundu):

1. Prošlapané pěšiny mezi místy, kam se chodí (vrstva `path`, bez trávy uprostřed, vyšší tráva po stranách).
2. Zmlazení podle Pripjati: semenáčky a mladé stromy u vzrostlých stromů, podél ulic, v rozích dvorů a
   občas v dlažbě; podrost keřů na okrajích hájů.
3. Nápisy a skvrny i na Ulici u nemocnice.

## 8. Proč to působí amatérsky a co se s tím dělá (2026-11-02)

Seřazeno podle dopadu, z pohledu hráče:

1. **Věci nesedí v zemi.** Silnice jsou krabice s ostrou hranou do trávy, koleje leží na desce, budovy vyrůstají
   ze země bez přechodu. Profi hry mají na každém spoji přechod. *Uděláno:* decaly spojů
   (`assets/models/zone/source/zonegen/joints.py`, rozmisťuje `assets/levels/source/add_joints.py`): hlína, písek
   a listí u obrubníků, rozbitá zarostlá krajnice silnic, špína a vlhko u paty zdí (jen kde je zeď rovná, test
   paprsky), štěrk vysypaný z kolejového lože.
2. **Assety nejsou z jedné rodiny.** Různá hustota textur, sytost a jas. *Uděláno:* `assets/levels/source/material_audit.py`
   (texel density proti mediánu levelu, jas a sytost albeda); rez a vlnitý plech méně syté (0,6), červený lak
   vybledlý, asfalt Skrundy z 114 na 256 px/m. Odchylek ze 127 na 47; zbytek jsou malé rekvizity, gumy a listí.
3. **Kompozice.** *Uděláno:* háje s podrostem keřů a semenáčky na okraji místo rozházených stromů,
   semenáčky u vzrostlých stromů a podél ulic (Pripjať), úzké prošlapané pěšiny mezi tábory, postavami, cíli
   a silnicí (Dijkstra na mřížce, vyhlazené, sešlapaná suchá tráva s holou zemí uprostřed). Háje drží 28 m od
   táborů a cílů.
4. **Střední vrstva detailu budov** (okapy, svody, kabely, cedule, otlučené hrany): další krok.
5. **Světlo a varianty stromů**: sépie opravena; zbývá víc variant stromů a kontaktní stín.

## 9. Zarostlá ulice je New York (2026-11-03)

martin: „nedávej tam ruský názvy, ale normálně jako v Americe, třeba v New Yorku“ a na kartě zvolil celou ulici jako New York.
Pravidla pro ulici (Mulberry Street, příčná Hester Street):
- Názvy postav, úkolů, ulic, obchodů a nápisy jsou americké, anglicky, bez azbuky (úkol „Eddie“, graffiti EDDIE WAS HERE).
- Domy jsou cihlové činžáky (walk-up) s plochou dehtovou střechou a atikou. Mají černá železná požární schodiště přes dvě okenní osy, s plošinami, schodišti a vytaženým spouštěcím žebříkem. Dále plechovou římsu na konzolách, kamenné parapety a překlady a dřevěnou vodní nádrž na ocelových nohách (`tenement(style='nyc')`).
- Vozovka je asfalt rozpraskaný na bloky asi 3 m, s prasklinami podél pruhů, tmavými záplatami po výkopech a výtluky s blátem. Tráva v prasklinách roste jen tam, kde je ulice nejzelenější, protože zelená čára na čistém asfaltu vypadá jako namalovaná.
- Cedule ulic jsou zelené s bílým písmem, nápisy obchodů anglicky (DELICATESSEN, BAKERY, PHARMACY…).

## 10. Skutečná architektura Lower Manhattanu (rešerše 2026-11-03)

martin: „spoj všechno do Mulberry Street, ať to vypadá realisticky… dělej to podle skutečné architektury“. Co je ověřené ze zdrojů (Z) a co je odhad (O):

**Parcela a dům**
- Typická newyorská parcela je 25 × 100 stop (7,6 × 30,5 m) (Z: Old Law Tenement, Tenement, Commissioners' Plan). **Ulice se proto skládá z úzkých samostatných domů po 7,6 m**, ne z dlouhých bloků. Každý dům má vlastní výšku, cihlu, římsu a rytmus oken. Občas stojí dvojdům na dvou parcelách (15,2 m). To je hlavní rozdíl proti našim 24–48 m dlouhým domům a důvod, proč ulice vypadá vymyšleně.
- Činžák (tenement) má 5–6 pater (Z). Old Law (1879) „dumbbell“ dům má boky zúžené o 3 stopy (0,9 m), takže mezi sousedy vzniká úzká světlíková šachta (Z). Na podlaží jsou 4 byty po 3–4 pokojích, řazené za sebou jako „railroad“ (Z).
- Zastavěnost parcely je nejvýš 65 % (1879) a 70 % (1901). Zákon z roku 1901 navíc požaduje zadní dvůr aspoň 12 stop (3,7 m) (Z). **Dům je tedy hluboký asi 18–21 m a za ním je dvůr. Uprostřed bloku se dvory dvou řad potkávají.** Manhattan v bloku nemá uličky, má ploty, prádelní šňůry a zadní domy.
- Požární schodiště je ze zákona (1867) na každém bytě (Z). Tvoří ho plošina v každém patře, schody nebo žebříky mezi nimi a spodní spouštěcí žebřík na pantu či kolejnici (Z). Je na uliční fasádě přes dvě okenní osy (O, podle fotek).
- Ozdoby: pískovcové a terakotové prvky, barevné cihly, styl Queen Anne a později Beaux-Arts, plechová římsa, letopočet a jméno na římse, např. „Anna Esposito 1926“ na č. 121 (Z: Forgotten NY).
- Výška podlaží je asi 3,0–3,2 m (O). Parter s obchodem je vysoký 3,6–4 m a má výlohu, roletu, markýzu a štítovou ceduli (O). Sklep je napůl pod zemí, se schody z chodníku a zábradlím. V chodníku jsou ocelové poklopy do sklepa (O).

**Ulice a blok**
- Ve mřížce z roku 1811 mají běžné ulice 60 stop (18 m), široké ulice a avenue 100 stop (30 m). Blok ze severu na jih je asi 200 stop (61 m). Mulberry leží jižně od Houston Street, mimo mřížku, a je užší a nepravidelná (Z).
- Mulberry Street vede mezi Baxter a Mott Street. Kříží ji Bayard, Canal, Hester, Grand, Broome, Kenmare, Spring a Prince (Z). Je srdcem Little Italy, s restauracemi, pekárnami a lahůdkami. Na jihu přechází do Chinatownu (Z).
- Vozovka úzké ulice měří asi 9–10 m a chodníky asi 3–4 m (O, NYC DOT čísla nenašel).
- Lampa je ocelový osmiboký stožár s „cobra head“ svítidlem. Dekorativní je „Bishop's Crook“ (Z: NYC Street Design Manual).
- Vybavení ulice (O, z fotek): požární hydranty, stojany na kola, drátěné koše na rozích, hromady černých pytlů na chodníku, novinové boxy, telefonní budka, poštovní schránka, lešení s chodníkovou stříškou („sidewalk shed“) z dřevotřísky na zelených trubkách, rolety výloh pomalované graffiti, klimatizace v oknech, vodní nádrže na střechách vyšších domů, parkující auta podél obou obrubníků a semafory na rozích.

## 11. Pravidla herního a level designu (martin: „řiď se podle design game pravidel“)

1. **Kritická cesta a cíl na očích:** hráč na začátku vidí cíl nebo landmark („weenie“, Disney). Jde k němu, i když ho cesta vede jinudy.
2. **Framing a centering** (Gaynor, GDC 2013): důležitou věc rámují dveře, průrazy, trosky a konec ulice. Hráč na ni hledí, když se událost děje. Mimo hlavní cíl platí pravidlo třetin, ne střed obrazu.
3. **Vodicí linie:** obrubníky, kabely, řady aut a pruh vyšlapané cesty vedou oko ke cíli, často podvědomě (rozbor TLoU II).
4. **Rytmus otevřeno–úzko:** široký prostor (orientace, boj) se střídá s úzkým (napětí, průchod domem). Uzly jsou křižovatky, kde se hráč rozhlíží.
5. **Víc cest ke stejnému cíli** (wide-linear): ulice, dvory přes domy, požární schodiště a střechy. Smyčky vracejí hráče do známého prostoru.
6. **Gating:** zával nebo barikáda zavře přímou cestu a ukáže, kudy dál (otevřené dveře, žebřík, světlo).
7. **Kryty a průhledy:** žádný průhled delší než asi 100 m bez krytu. Auta, kontejnery a lešení dělí ulici na úseky. Kryt stojí 10–20 m od sebe.
8. **Affordance:** kam hráč smí, to vypadá prostupně (otevřené dveře, světlo, vyšlapaná cesta, spouštěcí žebřík). Kam ne, to je jasně zavřené (zabedněno, zával, plot). Žádné neviditelné zdi tam, kde by se dalo jít; clip je jen za přirozenou hranicí.
9. **Vertikalita:** sklepy, patra, požární schodiště a střechy. Výš je výhled a odměna.
10. **Hustota detailu u očí hráče:** nejvíc detailu je do 10 m od cesty a ve výšce 0–3 m. Výš a dál stačí silueta.
11. **Měřítko:** reálné rozměry se kvůli hratelnosti mírně stlačují (rozbor TLoU II), ale proporce (parcela, patro, okno, dveře) zůstávají skutečné.

## 12. Plán: jedna Mulberry Street (2026-11-03, rozšířeno na mřížku 4 × 4)

Plánek: `docs/images/mulberry_plan.png`. Rozložení je v `assets/levels/source/mulberry_layout.py`, stavba levelu v `make_overgrown_street.py`, domy z `zonegen/walkup.py`. Souřadnice jsou v metrech, sever je −z.
- **Ulice ze severu na jih:** Baxter (x = −74), Mulberry (x = 0), Mott (x = +74) a Elizabeth (x = +148), každá 16 m mezi fasádami. **Ulice z východu na západ:** Canal na jihu (široká 30 m, otevřený start), Hester (z = −17), Grand (z = −103) a Broome (z = −189), každá 14 m. Mulberry pokračuje za Broome ke kostelu.
- **Bloky:** 9 vnitřních bloků. Do Mulberry, Mott a Elizabeth jsou obrácené hluboké řady (dům 18 m), do příčných ulic mělké (14 m). Domy stojí na parcelách 6,1 / 7,6 / 9,1 / 15,2 m (20, 25, 30 a 50 stop) a mají 4–6 pater, obchody v přízemí, římsy, kamenné schody, sklepní dvířka a požární schodiště s padacím žebříkem. Uprostřed bloku jsou dvory s ploty, šňůrami na prádlo a harampádím. Asi 20 domů jde projít: vchod, chodba, schodiště až na střechu a zadní dveře do dvora. Za každým takovým domem (a za proluku nebo vyhořelý dům) je plot uprostřed bloku vyvrácený, takže dvorem jde projít na druhou ulici. Na vybraných místech stojí vyhořelé domy, proluky se záborem, betonové skelety (vykuchané lofty) a zřícený dům.
- **Hranice:** po obvodu je uzavřená řada domů (přirozená zeď). Konce ulic zavírá barikáda z autobusů a aut. Clip je jen za nimi.
- **Kritická cesta:** start na Canal Street → pohled na sever do Mulberry s věží kostela na konci (weenie, 53 m) → Hester (uzel, tábor, úkol Eddie) → Mulberry u Grand zavírá zřícený dům s autobusem → přes dům a dvory, nebo oklikou po Mott Street → Grand → Broome → kostel. Vedlejší úkol „Balík“ vede na roh Grand a Mott ke stánku s novinami. Psí smečky jsou ve dvoře západního bloku a na Mott.
- **Pouliční vybavení podle NYC:** cobra lampy na šedém ocelovém sloupu s litinovou patkou, semafory na rozích, hydranty, pytle s odpadky, kontejnery, zaparkované vraky a taxíky u obou obrubníků, výmoly a kaluže. Z `zonegen/streetkit.py` (2026-11-04): dva vstupy do metra na Canal Street (schody 3,6 m dolů mezi zelenými zábradlími, zelené skleněné koule u ústí, zamčená mříž dole, na podestě lékárnička nebo náboje), tři autobusové zastávky s vytlučeným sklem, stromy v mřížích u obrubníku (litinové ohrádky, některé zválcované), u rohů modré poštovní schránky, řady automatů na noviny, červené hlásiče požáru a telefonní sloupky, u obchodů parkovací automaty. Vstup do metra musí ležet na mřížce terénu 0,5 m, terén se pod ním propadá (`SUBWAYS` v generátoru). Mulberry je Little Italy: přes ulici stojí šest slavnostních světelných oblouků (jeden nakloněný, jeden spadlý přes silnici, `FEAST_ARCHES`) a před obchody jsou předzahrádky restaurací se stolky, převrácenými židlemi a truhlíky se suchými rostlinami (úseky výloh posílá `walkup.py` do `overgrown_street_spots.json`).
- **Výkon:** domy se skládají z knihovny asi 30 variant parcel, které se instancují. Interiéry má jen LOD 0, LOD 1 nastupuje na 75 m. Ulice se navzájem zakrývají, takže pomáhá occlusion culling. Ve hře je to 4,4–8,2 milionu trojúhelníků a 300–480 draw callů na snímek. Cíl je 60 FPS na GTX 1060.

## Zdroje

- Game Maker's Toolkit, How Level Design Can Tell a Story:
  https://amara.org/subtitles/Lv64YfjMFhOC/en/1/download/How%20Level%20Design%20Can%20Tell%20a%20Story%20%20Game%20Makers%20Toolkit.en.txt
- PC Gamer, STALKER 2 locations are almost entirely hand-crafted:
  https://pcgamer.com/games/fps/despite-being-one-of-the-biggest-unreal-engine-5-games-ever-stalker-2-s-locations-are-almost-entirely-hand-crafted-it-took-a-lot-of-time-took-a-lot-of-effort-but-we-re-happy-with-the-result
- Denys Rudyi o lokacích STALKER 2: https://novyny.live/en/tehnologii/rozrobnik-rozpoviv-iak-stvoriuvalisia-lokatsiyi-v-stalker-2-219358.html
- 80.lv, Into the Radius 2 Abandoned Dacha: https://80.lv/articles/how-to-create-a-nostalgic-and-abandoned-environment-for-vr-survival-shooter
- 80.lv, Post-apocalyptic city in Substance 3D Painter and UE4: https://80.lv/articles/creating-a-post-apocalyptic-city-in-substance-3d-painter-unreal-engine-4
- Edge/GamesRadar, Stalker's brutal wasteland: https://www.gamesradar.com/stalker-shadow-of-chernobyls-brutal-wasteland-has-grown-more-thrilling-with-age
- Laćan, McBride, De Witt (2015), Urban forest condition and succession in Pripyat: https://agris.fao.org/search/en/records/65de2e5c63b8185d9ca77757
- David Lock, A city reclaimed by nature (Pripyat): https://www.davidlock.com/post/a-city-reclaimed-by-nature-what-has-pripyat-shown-us
- Peter Del Tredici, Green Blacktop (2011): https://peterdeltredici.com/s/PDF-41f-Green-Blacktop-2011-New-Geographies-3.pdf
- Peter Del Tredici, Spontaneous Urban Vegetation (2010): https://peterdeltredici.com/s/PDF-41d-Spontaneous-Urban-Vegetation-2010-Nature-Culture.pdf
- VFX Voice, How DNEG built Seattle and decayed it: https://vfxvoice.com/how-dneg-built-seattle-and-decayed-it-25-years-for-the-last-of-us-season-2/
- Game Informer, Nature's ruinous revenge (The Last of Us): https://gameinformer.com/b/features/archive/2012/02/17/nature-s-ruinous-revenge-the-environments-of-the-last-of-us.aspx
- Lynch legibility: https://ecampusontario.pressbooks.pub/studioskills/?p=172
- Brian Upton, Landscaping game worlds (GDC 2007): https://gamedeveloper.com/game-platforms/post-gdc-i-rainbow-six-i-s-upton-talks-landscaping-game-worlds
- Futura-Sciences, If humanity disappeared, how long would New York last: https://www.futura-sciences.com/en/if-humanity-disappeared-tomorrow-how-long-would-new-york-last_27788/
- Wikipedia, Old Law Tenement: https://en.wikipedia.org/wiki/Old_Law_Tenement
- Wikipedia, Tenement: https://en.wikipedia.org/wiki/Tenement
- Wikipedia, Mulberry Street (Manhattan): https://en.wikipedia.org/wiki/Mulberry_Street_(Manhattan)
- Wikipedia, Commissioners' Plan of 1811: https://en.wikipedia.org/wiki/Commissioners%27_Plan_of_1811
- Wikipedia, Fire escape: https://en.wikipedia.org/wiki/Fire_escape
- Forgotten NY, Mulberry Street: https://forgotten-ny.com/?p=93556
- NYC Street Design Manual, lighting: https://www.nycstreetdesign.info/lighting/standard-street-lighting
- Game Developer, Level design study: The Last of Us Part II trailer level: https://www.gamedeveloper.com/design/level-design-study-the-last-of-us-part-ii-gameplay-trailer-level
- Game Developer, Framing and centering: https://www.gamedeveloper.com/design/framing-and-centering-directing-player-attention-in-open-3d-worlds
- GDC 2024, Ania Bulavina, Urban Planning in Games: https://gdcvault.com/play/1034528/Level-Design-Summit-Urban-Planning
