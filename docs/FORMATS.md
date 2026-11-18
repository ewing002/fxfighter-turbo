# FX Fighter Turbo – fájlformátumok

Visszafejtve a `FIGHT.EXE`-ből (Watcom C, BRender 1.2.2, 16.16 fixpontos build) és az adatfájlokból.
A ✅ jelölésű formátumokat a port már olvassa, a ◐ részben ismert, a ❌ még nincs feldolgozva.

## FIGHT.PAC ✅
```
u32 count
count × { u32 offset; char name[80]; }   // little-endian; az utolsó bejegyzés neve üres = fájlvég
```
655 fájl. A nevek `fight\...` alakúak, és kis- vagy nagybetűsek is lehetnek.

## *.cmp – RLE képek ✅
```
u32 unpacked_size
c < 0x80  : a következő bájt ismétlése (c + 2)-szer
c >= 0x80 : (256 - c) literál bájt másolása
```
Kivétel: a `bgrounds/picp*.cmp` fájlok nem tömörítettek, hanem BRender pixelmapok (lásd lent).

| Fájl | Méret | Tartalom |
|---|---|---|
| `fighters/f%d.cmp`, `f%db.cmp` | 256×256, 8 bit | harcostextúra (a `b` a második ruha / P2-színek) |
| `bgrounds/pic%x.cmp` | 640×200 | aréna-panoráma (360°) |
| `bgrounds/picp%x.cmp` | 256×1 RGBX | az aréna palettája |
| `floors/floor_[hlm]%x.cmp` | 256×256 | aréna-textúra (h/m/l = részletességi szint); a `floor_h%x.dat` a ringen kívül a környező építményeket is tartalmazza (oszlopok, falak, ládák, tornyok) |
| `fighters/fm%d.cmp`, `chr%d.cmp`, `fi%d.cmp` | – | ◐ menüportrék és ikonok (fm=256×256, chr=512 B, fi≈580 B) |

## BRender chunk-fájlok (.dat, .mat, .pix) ✅
Big-endian `u32 id, u32 length, data`. Az id-k a `core/fw/datafile.h` szerint
(BRender 1.3.2, MIT licenc, github.com/foone/BRender-v1.3.2).

Ezek fordulnak elő:
- `18 FILE_INFO`: típus (0xFACE = modell, 5 = anyag, 2 = pixmap)
- `54 OLD_MODEL_2`: u16 flags, név
- `23 VERTICES`: u32 n, n×3 float
- `24 VERTEX_UV`: u32 n, n×2 float
- `53 FACES`: u32 n, n×{u16 v0,v1,v2, u16 smoothing, u8 flags}
- `22 MATERIAL_INDEX`: u32 n, n×ASCIZ név. **A length mező hibás, nem szabad rá támaszkodni.**
- `26 FACE_MATERIAL`: u32 n, u32 elemméret (2), n×u16 (1-alapú index, 0 = nincs anyag)
- `4 MATERIAL_OLDEST`: RGB(3) opacity(1) ka kd ks power(4×float) flags(u16) map_xform(6×float) index_base(u8) index_range(u8) név
- `28 COLOUR_MAP_REF`: ASCIZ
- `3 OLD_PIXELMAP`: u8 típus, u16 row_bytes, u16 w, u16 h, u16 ox, u16 oy, ASCIZ név
- `33 PIXELS`: u32 darab, u32 elemméret, adat

A `TX` kezdetű anyagok textúrázottak, a textúrát a kód rendeli hozzájuk (harcosnál `f%d.cmp`, padlónál `floor_h%x.cmp`).

**Textúracsempék:** minden 256×256-os textúra 4×4 darab 64×64-es csempe. **A .dat fájl i-edik modellje az i-edik csempét használja**
(sorfolytonosan, balról jobbra, fentről le), és az UV-koordinátái (0..1) a csempén belül értendők. Ez a harcosokra
(16 testrész = 16 csempe, a Skull a 14-es csempe) és a padlókra (A..P részek) is igaz.
Egyes UV-k szemetet tartalmaznak (−1.7e38), ezeket le kell vágni 0..1-re.

**Palettabankok:** a nem `TX` anyagok (`BANKn`, `FLOORn`, `FFLOORn`, …) színe a palettából jön: `index_base = 10 + 16·(n−1)`,
`index_range = 15`. Ez egy sötétből világosba futó, 16 lépcsős színátmenet, a szín `paletta[index_base + árnyalat]`.
A material.mat RGB-értéke csak a true-color módhoz tartozó közelítés.

### Harcosmodell: `fighters/fm%d.dat`
16 testrész, ábécésorrendben. **Ez a sorrend egyben a csontindex is**:
```
0 LeftFemur  1 LeftFoot   2 LeftHand   3 LeftHumoru  4 LeftRadius  5 LeftTibia
6 Neck       7 Pelvis     8 RightFemur 9 RightFoot  10 RightHand  11 RightHumor
12 RightRadiu 13 RightTibia 14 Skull   15 Thorax
```
A koordináta-rendszerben Z mutat felfelé és +Y előre. A medence van az origóban.
Ha mindkét játékos ugyanazt a harcost választja, a P2 az `fm%db.dat` + `f%db.cmp` párt kapja (másik ruha).
A testrészek egy közös gyökér-actor lapos gyerekei (a `0x445dc8`-as függvény építi fel), nincs csonthierarchia.

## Animáció: `fighters/f%d.ani` ✅
(Az `.an4` ugyanilyen formátumú, de az EXE nem tölti be; feltehetően egy régebbi, alacsonyabb képkockaszámú változat.)
```
u32 nbones (=16), u32 nclips, u32 total_frames
nclips × {
  char name[16]            // NUL után szemét is lehet
  u32  nframes
  nframes × {
    s32 root_x, root_height, root_forward      // × 406 / 65536
    16 × { u8 ax, az, ay;  s8 tx, tz, ty }
  }
}
```
Csontmátrix (FIGHT.EXE `0x445f16`, BRender sorvektoros konvenció):
```
M = RotateZ((255-az)<<8) · PostRotateX((255-ax)<<8) · PostRotateY((255-ay)<<8)
T = (tx, ty, tz) × 406 / 65536        // figyelem: a fájlban a sorrend tx, tz, ty
```
A szög br_angle (65536 = 360°). A Skull (14) kap még egy fejfordítás-eltolást a játékállapotból.
A `root_forward` változása mozgatja a harcost előre (`BrMatrix34PostTranslate(0, Δ/2, 0)`).

## Mozdulatok: `moves/f%d.hdr` ✅ / `moves/f%d.ste` ✅
Betöltő: `0x433120`. A P1 táblái a `0x4d6390`-es (mozdulatok) és a `0x4ccd90`-es (állapotok) címen vannak, a P2-éi +0x4800 / +0x4b00 eltolással.

### `.hdr`: `u16 ?` + N × 36 bájt
| Offszet | Mező |
|---|---|
| 0x00 | név (20 bájt, pl. "STAND", "PUNCH", "HIT FACE L") |
| 0x14 | támadó testrészcsoport: index a `LIMB_SETS` táblába (`0x4aa510`) |
| 0x15 | testhelyzet: alsó 5 bit = azonosító (4 = áll, 8 = dobás alatt…), 0xC0 = támadástípus |
| 0x16 | osztály (3 = földön) |
| 0x17 | animáció: ≥200 → a saját `f%d.ani` (érték−200)-adik klipje, <200 → a közös `f0.ani` klipje, 255 → nem vált |
| 0x18 | hang |
| 0x19 | AI-jelzők: bit 0–1 = fajta (1 támadás, 2 blokk, 3 megfordulás), 0x0C = ringszél-vizsgálat, 0x40 = magas, 0x80 = az AI sosem választja |
| 0x1A–0x1E | AI-pontszám az 1–5. helyzet-slotban |
| 0x1F | helyzetmaszk (melyik ellenfél-helyzetben erősebb, l. lent) |
| 0x20 | ennyi képkocka kell a mozdulatnak az ellenfél ütése előtt |
| 0x22 | u16: az első állapotrekord indexe a `.ste`-ben |

### `.ste`: 12 bájtos rekordok (little-endian)
```
u8 op; u8 fr; u16 val; u16 mask; u8 cond; u8 minfr; u8 ctx; u8 pad; s16 target
```
Egy mozdulat rekordjai a `0x22`-es indextől a lezáró rekordig tartanak (`mask == 0 && ctx == 0`).
- **Bemeneti átmenet** (`0x4427c4`): akkor sül el, ha `((in ^ előző) | in << 8) & mask == val`, és `minfr ≤ képkocka`,
  `cond` (az ellenfél testhelyzete: 0 = mindegy; `0x10` bit = "nem egyenlő") és `ctx` (`ctx>>4 == ctx & kontextus`) is stimmel. Ekkor → `target`.
- **Esemény** (`0x441c0b`): amikor a képkocka eléri az `fr`-t: `op` 1–99 = találatvizsgálat `op` sebzéssel (`cond>>6` = irány),
  100–199 = saját sebzés, 0xD0–0xD7 = mozgásmód, 0xD8 = megfordulás, 0xDE = hang, 0xE0 = képkocka tartása, … A `cond == 8` rekord dobást jelent.
- **Lezáró rekord**: ha `fr ≤ képkocka` vagy véget ért az animáció → `target` (`cond == 0x0A`: 180°-os fordulás).
- A blokkoló állapotok `op`-ja 0xFF (felső/középső blokk) vagy 0xFD (teljes); 0xFE = sérthetetlen.
- A `val=0x00FF` átmenetek csak az AI-nak szólnak (emberi bemenettel elérhetetlenek).

### Gépi ellenfél (`0x425892`, a portban `ai_think`)
Az AI nem nyom gombot: az aktuális mozdulat átmenetei közül választ, és a célmozdulatot közvetlenül indítja (`0x43e828`).
- Nem lép, ha valamelyik harcos dobás alatt van (testhelyzet 8). Ha háttal áll, az első 3-as fajtájú (megforduló) célt indítja.
- Távolság (`0x4a5698`): < 0,8 közel (1–5. slot), < 1,0 közép (1–3. slot), egyébként távol (csak az 1. slot).
- Pontozás (`0x425166`) minden átmenetre (`val` és `mask` ≠ 0xFF00, `cond`/`ctx`/`minfr` stimmel):
  `pont = cél[0x19+slot] × SZORZÓ[táv][slot] × SZORZÓ[3 + ellenfél_alacsony][slot] × súly − ellenfél_mozdulata[0x19+slot]`.
  `SZORZÓ` = `0x4a56a8` (5×6 int). A súly (`0x4250cc`) a helyzetbitek (`0x424f80`: guggol, levegőben, földön, kábult, blokkol, magasan/alacsonyan támad)
  és a cél 0x1F-es maszkjának metszetéből jön: bitenként `0x4a5778[bit] + szint`. Minden slot csak `rand & 15 ≤ 0x4a5720[szint]` eséllyel számít.
  A `mask == 0`, `ctx ≠ 0` átmenet kötelező (999999 pont).
- Védekezés (`0x4256cd`): ha az ellenfél támad (fajta 1), az AI kiszámolja a következő találatának képkockáját.
  `rand & 15 ≤ 0x4a5728[szint]` eséllyel és 2,1-nél kisebb távolságon egy 2-es fajtájú (blokk) célt választ 5000 ponttal,
  ha lehet, a támadás magasságához illőt.
- Csak akkor vált, ha a saját aktuális mozdulat pontja abban a slotban kisebb. Egy tickben legfeljebb 3 lépés.
  Semleges mozdulatban (fajta 0) minden lépés előtt `8 − szint` ticket vár.
- Nehézségi szint: 0–7 (`0x4a587c`, alapértéke 4).

**Bemeneti bitek**: 0x01 előre, 0x02 hátra, 0x04 le, 0x08 fel, 0x10 ütés, 0x20 rúgás.
**Kontextusbitek**: bit0 = az ellenfél ütőtávon belül, bit1 = háttal áll az ellenfélnek, bit2 = az ellenfél áll háttal.

### Találat (`0x4491f7`, `0x448cbb`)
A testrészek **kockák az ízületi pontjuk körül** (`FUN_004488ab`): a középpont a testrész világmátrixának eltolása, a fél élhossz
`fi%d[0x88 + testrész] / 256` (jellemzően 35/256 ≈ 0,14, a medence 50, a mellkas 150). Nem a háló bounding boxa!
A támadó csoport kockáit a `0x448a69` vonja össze: x-ben és y-ban unió, z-ben viszont az alsó ÉS a felső határ is a kisebbik
értéket kapja (az eredeti hibája, a port megtartja). Ezt veti össze az áldozat [Skull, LHumerus, RHumerus, LTibia, RTibia, Pelvis, LFemur, RFemur, Neck]
kockáival, sorrendben (a lista mind a négy védekezési állapotban ugyanaz). A zóna a talált kocka középpontjának Z-je: < 0,4 alacsony, < 1,0 közép, egyébként magas.
Söprés: a doboz a mozdulat előző vizsgálatának (össze nem vont) dobozával is összevonódik (`a8268` / `aa4c0`). A tároló csak a második
vizsgálattól íródik, és a StartMove nullázza, ezért a mozdulat második vizsgálata egy nulla dobozzal vonódik össze (szintén az eredeti szerint).
A vizsgálat előtt a képkocka az esemény `fr`-jére áll, utána a mozdulat eseményei egy 0-s vagy ≥ 0xF1-es rekordig nem futnak (`cb540`).
A 0xD4-es opkód után a következő támadó esemény "folyamatos": a vizsgálat minden tickben megismétlődik, amíg talál vagy véget ér a mozdulat.
Sebzés = `op × fi[0x40] × fi[0x44] × támadás[támadó] × védekezés[áldozat]`, csonkolva (`0x441865`; a két `fi` tag 1,0).
A két szorzót minden menet előtt a `0x446690` állítja: gépi harcosnál a nehézségi szintből (támadás `0x4a5738`:
1,0 1,11 1,12 1,12 1,15 1,15 1,2 1,2; védekezés `0x4a5758`: 1,0 1,0 0,95 0,9 0,9 0,9 0,85 0,8), két ember között a
támadás a hendikep (`0x4a4030`: 0,7 … 1,4, 0,1-es lépésekkel; a 4. fokozat 1,0), a védekezés 1,0; ember a gép ellen
1,0 / 1,0. Visszalökés: 0,1 / 0,2 / 0,3 az irány (`cond>>6`) szerint.
**Kombó** (`0x448cbb`): ha az áldozat a találat előtt reakciómozdulatban volt (`0x4a4ac0` jelzőtábla, a StartMove a `cb4c8`-ba írja),
a sebzés és a kábulás az n-edik kombótalálatnál `0x4aa470[n]`-szeres (0,2 / 0,15 / 0,1 / 0,05 / 0,02, utána 0,01). Tiszta találatnál n = 0-ra áll.
**Kábulás** (`cb3f8`): tiszta találat `fi[0x58] × sebzés` pontot ad (3,0). Tickenként `5 × fi[0x5c]` pont fogy (2,0),
és tickenként 0 és 64 közé van szorítva. Ha egy találat után 75 fölött van, és az áldozat él, az érték nullázódik,
és (ha nem fekszik) a 0x68-as „FLYING FALL 2 DAZE” mozdulat indul. Utána GETTING UP DAZED → DAZE START → DAZE LOOP 1–3 → DAZE END.
**Kiszabadulás** (0xCC / 0xCD / 0xCE esemény): ha az eddigi bemenetváltások száma (`cb460`) eléri a `fi[0x60] / 2`, `fi[0x64]`,
illetve `fi[0x68]` értéket (12 / 25 / 25), azonnal a rekord céljára vált. A számláló minden olyan tickben nő, amikor a bemenet
változik. A StartMove nullázza, kivéve ha az új mozdulat első rekordja is 0xCC–0xCE. A CPU ilyenkor nem lép,
hanem tickenként `0x4a5790[szint]`-et ad a számlálóhoz. A DAZE LOOP 1 és 2 elején van 0xCC: kitartó nyomkodással kimaradnak a további körök.
Az 1-es és 2-es támadástípus (`stance >> 6`) a teljes harcvonalat elforgatja a támadó körül `−(sebzés << 7)` br_angle-lel.
A reakció indexe: `(támadástípus>>3) | zóna<<5 | irány | (halálos ? 4 : 0)`, ezzel a 128 elemes `REACT_*` táblákból jön az áldozat következő mozdulata.
Dobás: a `cond == 8` esemény az áldozatot a **dobó táblájából** indítja (`StartMove(p+2)`); a 0xCB lezáró rekord
"ugyanaz a játékos, a másik tábla" (`StartMove(p^3)`); a 0xE8 a két harcos távolságát állítja (`target << 9`, 16.16,
+0,1, ha az utolsó dobást a 2. játékos indította: `0x4a8344 == 2`). A dobás indításakor (`0x441c0b`) a dobó nem
szilárd (`0x4a8260[dobó] = 0`), így a taszítás nem tolja hátra; e nélkül a felemelt harcos a dobó előtt lebeg, nem
a feje fölött.
A 0xC9 lezáró rekord akkor sül el, amikor a harcos földet ér.

Életerő: 120. A játék sebessége 48 tick/s (PIT 1193182 / 24850), egy kör 60 másodperc.

## Harcos- és arénaparaméterek: `fighters/fi%d.cmp` ✅
RLE, utána **minden bájtból 1-et le kell vonni**. 584 bájt. Az *n*-edik blokk egyszerre az *n*-edik harcos és az *n*-edik aréna adata.
| Offszet | Mező |
|---|---|
| 0x00 / 0x10 | harcos neve / aréna neve (MAGNON/INFERNO, SHEBA/RHOMB, VENAM/PERES, JAKE/SENTRAL, KIKO/LUSK, SIREN/URSAE, ASHRAF/KARLAK, CYBEN/AXONE, RYGIL/ANARCHIS, LINNA, KWONDO) |
| 0x40, 0x44 | sebzésszorzók (16.16) |
| 0x4c | a padló mélysége a ringen kívül (16.16, pl. −0,6) |
| 0x54 | a ring félmérete (float, 5,04; a gyakorló arénában 2,52) |
| 0x58 | kábulási szorzó (3,0) |
| 0x5c | kábulás csökkenése (0,4; tickenként ×5) |
| 0x60, 0x64, 0x68 | kiszabaduláshoz szükséges bemenetváltások a 0xCC (fele) / 0xCD / 0xCE eseményhez (25) |
| 0x6c, 0x70, 0x74 | ugrási ív: v0, g, skála |
| 0x78 + 4·osztály | testsugár mozdulatosztályonként (0,3 / 0,3 / 0,5 / 0,6) |
| 0x88 + testrész | találati kocka fél élhossza ×256 |

## Harcvonal és ring
A harc egy vonal mentén zajlik (`a8684` mátrix, a két 1D pozíció: `a8280`/`a8284`, kezdetben −1 / +1). A vonal elfordul:
- **oldalazás** (`0x43e424`): mozdulatváltáskor az előző mozdulat oldalirányú gyökér-eltolásából `atan(x / távolság)` szöggel elforgatja a vonalat az ellenfél körül (küszöb: 0,02 rad);
- **lendítő ütések** (lásd fent).

Ugrás és kitörés (`0x440f42`): a 0xD0/D1/D2 ugrás helyben/előre/hátra (sebesség ±2 egység / 32 tick), a 0xD5/D6/D7 kitörés az ellenfél felé (a távolsággal, +1, −1 sebességgel).
Taszítás (`0x43fbca`): ha mindkettő szilárd (`a8260`) és a „testmagasságuk” (a mellkas és a bal alkar kockájának átlagolt középpontja) 0,65-nél kevésbé tér el,
a sugarak összegénél kisebb távolság 0,3-ával tolja szét őket, az érintkezés előtti oldal felé (`a8264`, érintkezés közben nem frissül).
A földön fekvő (osztály 3) helyett a másik mozdul. Ha átjutottak egymáson, a régi távolság felével a közép felé húzza őket.
Ringhatár (`0x4408f9`): a medencétől a harcvonal mentén mért távolság mindkét irányban a `félméret + 1,3` falakig legalább 0,5
(1,8, ha ki van kapcsolva a kiesés és a mozdulat 0xDA-val nem oldotta fel, vagy ha az ellenfél medencéje már kint járt). Dobás alatt nem tol.
Ringből kiesés (`0x43f36e`): tickenként csak a középtől messzebb lévő harcos vizsgálható (`0x445002`, 0,1-es holtsávval követett medence-pozíciókkal).
A „medence kint” jelző (`df3c8`: |x| vagy |y| > félméret − 0,1) a kör végéig megmarad. Fekvő medence (z < 0,15) és a jelző → RING OUT SLIDE HEAD / FEET (9/10);
álló harcos, kilógó és talajközeli (z < 0,2) lábfej → RING OUT BACKWARDS / STEP (7/6). Az első változat (9, illetve 7) akkor jön,
ha a harcos az ellenfél felé néz és annak medencéje nem járt kint, vagy háttal áll és járt kint; egyébként a második. Ezzel elveszíti a kört.
Talajmagasság (`cb498`): ugyanez a függvény minden ticken, kieséstől függetlenül beállítja a harcos alatti talajt: 0, vagy az aréna
alsó szintje (`fi[0x4c]`, fall depth), ha a medence (félméret − 0,1) vagy bármelyik lábfej (félméret) kint van; kikapcsolt kiesésnél 0.
A leszállás (`0x441245`) ehhez igazít (magasság ≥ talaj + 0,3). Így a peremen túl elesett vagy KO-zott harcos lezuhan, nem lebeg.

## Hang ✅
### `sound/*.asf` – 4 bites DPCM (dekóder: `FUN_0044f7b1`, rutinok `0x44f7e6` / `0x44fb1e`)
```
0x00 "ASF\0"   0x10 név[8]   0x18 u32 blokkszám   0x1c u32 minta/blokk (32)
0x24 u16 mintavételi frekvencia (22050 / 44100)   0x28 u32 jelzők (bit0 16 bit, bit1 sztereó; mind 13 = 16 bites mono)
0x2c adat: blokkonként 1 vezérlőbájt + 16 bájt (32 db előjeles 4 bites delta, felső nibble először)
```
Vezérlőbájt: felső 4 bit = shift, 2-es bit = predikció. A minta (`n` = nibble×16, `cl` = shift+2):
- bit2 = 0: `s = (előző·64 + (n << cl)) >> 6`
- bit2 = 1: `s = (2·előző·64 − előtte·64 + (n << cl) + átvitel) >> 6`

Az eredmény 16 bitre csonkolódik, vágás nélkül. A fájl végén van egy plusz, nem használt blokk. Összesen 284 minta, kb. 375 s.

### `sound/*.fxl` – hangbankok (`FUN_0043509b`)
Szöveges lista, soronként egy mintanév, `end`-ig; `;` = megjegyzés; `null` = üres. Az előtag a lejátszási mód
(a bejegyzés `+0x10` mezője): nincs = 0, `*` = 1 és `#` = 3 beszéd – lemezről, egyetlen csatornán szól, és ha az
foglalt, egy 10 elemű sorba áll (`FUN_0041142a`), így a bemondások egymás után hangzanak el (pl. a választás után
„név – versus – név”); `+` = 2 rövid hang egy kis puffergyűrűn át (a portban úgy szól, mint a 0).
- 0-s bank: `generic` (bemondó: 1 "FIGHT", 2 győzelem, 3 vereség, 4 idő, 7 hibátlan, 14/15 P1/P2 nyert, 16 hirtelen halál, 17 visszaszámlálás, 19 teleport);
  tornán `tour` (ugyanez, plusz 20–30 a harcosok neve 1–11 sorrendben, 31 „wins”)
- 1/2-es bank: a harcos neve (`magnon.fxl`, …, 45 bejegyzés)
- arénabank: `b%d.fxl` (4 bejegyzés: lépés, puffanás, landolás, dobbanás)

Lejátszás (`FUN_00435eb4(bank, index)`): a mozdulat 0x18-as mezője a `StartMove`-ban, illetve a 0xDE opkód `target` mezője.
< 0x80: a játékos saját bankja (1-alapú), 0x80–0xEF: az arénabank (0-alapú), ≥ 0xF0: nincs hang.
A találati hangokat a reakciómozdulatok saját hangmezője adja (pl. HIT FACE L = 1 = `mag_l5`).

### `*.slb` – sprite-effektkönyvtárak ✅
Nem hangparaméterek: látványeffektek (`Sprlib.c`, `Sprplay.c`, `Sfxroot.c`, `Effects.c`; a „sfx” itt special effects).
A port: `src/sprfx.c`. Fejléc: `u32 verzió = 1, u32 npix`, aztán három tömb (`u32 darab, u32 elemméret`): parancsok
(0x24 bájt), kibocsátók (0x44 bájt) és effektbejegyzések (0x20 bájt: `u32 azonosító 0..255, u32 első parancs, char név[24]`),
végül `npix` pixelmap: 0x18 bájtos fej (`s16 sorhossz, u8 típus = 3 (8 bites indexelt), u8, u16 bázis x/y, u16 szélesség/magasság,
s16 origó x/y, u32 tömörített méret, u32 kibontott méret`), utána a `0x42e5c9` PackBits-változata: `c = ~(int8)b`; `c > 0`: `c+1`
szó szerinti bájt, `c == -128`: vége, különben a következő bájt `1-c`-szer; minden tárolt érték a pixel + 1. A 0-s index átlátszó.

**Parancsok** (9 dword, értelmező `0x44d868`, az időegység a harc tickje, 48 Hz):
| típus | jelentés |
|---|---|
| 1 | képkocka: `[1]` pixelmap, `[2]` float idő (tick), `[3..8]` 16.16-os 2×3-as mátrix (a négyzet helye és mérete a kamerasíkban) |
| 2 | ugrás `[1]`-re, ha `[2] == -1` |
| 3 | vége: a sprite megszűnik |
| 4 | kibocsátó `[1]` a `[2]` csontcsoport minden csontján, `[3..5]` eltolás, `[6]` részecskeszám |
| 5 | gyereksprite: `[1]` kezdő parancs, `[2]` csontcsoport, `[3..5]` eltolás, `[6]` fajta, `[7]` kamera felé húzás (16.16) |

Csontcsoport: a `0x4aa510` tábla sora (`u8 darab, csontok`; a 0..15 sorok az egyes testrészek, a 16..63 sorok csoportok,
az utolsó mind a 16). `-1` = a szülő csoportja eltolás nélkül, `0x10` = a lövedék (ha van), `-2` = a harcos talppontja (`0x4a8690`).
Gyereksprite-fajták: 0 = rögzítve ott, ahol a csont most van (`0x447f4a`); 1 = követi a csontot (`0x4163d0`); 2–4 = a csontot
követi eltolással, és a csonthoz képest elfordul (`0x43c2f0`: a kamera→csont mátrix Euler-szögének első tagja, 2-nél
`0x7ff8 - szög`, rend 2 / 5 / 4).
**Kibocsátó** (17 dword, `0x43bbfa`, részecske `0x43ba41`, frissítés `0x43bec1`): `[0]` a részecske szkriptje, `[1]` helyszórás,
`[2]` sebességmód (0 = az ellenfél `[3]` csontja felé `[4..6]` eltolással, `[7]` sebességgel; 1 = ugyanez a saját csontra;
2 = `[3..5]` állandó; 3 = `[3..5]` a csont mátrixával, az eredetiben eltolással együtt; 4 = mint a 2, de a talajon, z = 0),
`[8]` sebességszórás, `[9,10]` gravitáció (z, szórással), `[11,12]` pattanás (0 = a talajon megszűnik), `[13,14]` élettartam
(16.16 tick, `-1.0` = végtelen), `[15,16]` nem használt. Szórás: `v + ((rand() - 0x3fff) * 16) % s`. Lépésenként
`pos += v·dt` és `v += g·dt` (a `dt/2`-es tag egész osztás, egész tickre 0).
**Rajzolás** (`0x4480c7`, `BrSprite` modell `0x4483aa`): egységnégyzet, UV 0..0,97, a képkocka mátrixa × forgatás a
kamerasíkban (billboard), a pozíció `[7]`-tel a kamera felé húzva. Z-pufferelt, a 0-s texel átlátszó, világítás nincs.
Legfeljebb 256 sprite (`0x44e890`).

**Könyvtárak** (`0x433350`, tábla `0x4a6b10`): generic és teleport mindig, `Arena%02d` (csak a 2-es és 6-os, vízi aréna
létezik), a két harcosé (`1 << harcos`, Magnon = 1 … Kwondo = 11). Játékosonként `[id]` tábla (`0x4bd9a8`): egy későbbi
könyvtár akkor írja felül az azonosítót, ha üres, vagy a könyvtár az adott játékos harcosáé vagy az arénáé – így a harcos
saját vére lép a generic helyébe.
**Indítók** (`0x44c666` sorba teszi, képkockánként legfeljebb 4-et; `0x44c851` / `0x44cba0` futtatja, dt = 0-val):
- találat (`0x448cbb`): a talált testrészen (`c6f00`) vér `0x17 + irány` (Blood 1–4), ha a reakció sebződés, különben
  blokkszikra `0x13 + irány` (Block 1–4); az irány a támadó eseményének `cond >> 6` bitjei;
- mozdulat-esemény `0xe1`: azonosító = a rekord `tgt`-je, csontcsoport = `cond & 0x3f` (Kwondo töltés, torkolattűz;
  Cyben fúvókák, rotor; a vízi arénák Step / Land / Splash effektjei);
- a meccs eleje (`0x44cd7c` → `0x44cdd5`): teleport (`0x2d`) mindkét harcos talppontján; endurance-ben csak az újonnan
  érkezőnél;
  a teleportáló harcos addig láthatatlan (`0x444ea6`: a testrész-actorok típusa 0, `0x4a8828 = 1`), amíg a kör kezdete
  (`0x4c7270`, `0x4464c2`) óta `0x1fd999` PIT-tick (kb. 1,75 mp valós idő, 48 Hz-en 84 tick) el nem telik (`0x444f54`) –
  ekkor a legszélesebb a fénycsóva;
- lövedékek (`0x43cdb2`, `Sfxproj.c`: saját találatvizsgálat a `0x4491f7`-tel) a 0xd3-as eseményből és a 0xf0–0xf7 mozdulat-
  hangokból indulnának, de **a Turbo egyik mozdulattáblája sem használja őket** – halott kód, a port nem valósítja meg.
Rygil (9) mozdulatainak 43-as azonosítója egyik könyvtárban sincs meg, az eredetiben sem rajzol semmit.
`FX_SPRTRACE=1` kiírja a kért effekteket.

Hanglejátszás (`Windsoun.c`): nincs térhatás. A `0x435eb4(bank, index)` pozíciót nem kap, SetPan csak 0, SetFrequency
nincs, ismétlés nincs. 8 körbeforgó puffer (22 500 Hz-en, kb. 2 %-kal magasabban), a hosszú hangoknak egy külön
puffer (22 050 Hz), a `+` hangoknak 2 (44 100 Hz). Hangerő csak a főhangerő; az egyetlen kivétel a visszaszámláló
sípolása (`0x422399`), amely 0 dB-es főhangerőnél nem változik.

### Zene
- Zene: CD-audio sávok (MCI, `FUN_0043adf8`). Harc közben az `aréna + 2`-es sáv szól (`FUN_0043b077(a8308 + 2)`), a stáblistánál a 2-es, demó módban a 14-es.
  Az 1. sáv a CD adatsávja (MODE1/2352). A kép egyetlen BIN fájl, a hang benne **big-endian** 16 bites sztereó 44,1 kHz.

## Videók: `fight/video/*.brp` ✅
A CD-n vannak (`FIGHT/VIDEO`; a telepített `FIGHT\VIDEO\WIN.BRP` valójában a BRender-logó), a `tools/rip_video.py`
másolja ki őket. Lejátszó: `0x42aad0` (`Video.c`), szétbontó `0x419900` / `0x419bae`. A port: `src/video.c`, ellenőrző
eszköz: `tests/brpcheck.c` (a Python referenciával pixelre és mintára azonos).
```
'BRPP', u32 nstreams, u32 0x4b000
nstreams × { fourcc 'BVID' | 'BASF', u32 index, u32 hossz (ms), u32 max chunk, u32 extra hossz, extra }
  BVID extra: u32 képkockák, u32 szélesség (320), u32 magasság (200 / 180 / 150), u32 mélység (24)
  BASF extra: 'ASF\0', u16 1, u16 2, u32 hangchunkok száma, u32 0x18, char név[8]
chunkok a 0xFFFFFFFF indexig: { u32 stream, u32 időbélyeg (ms), u32 méret, adat }
```
Sorrend: a hang chunk(ok) (az FXINTRO-ban kb. 0,5 s-os darabok közbeszúrva), egy `PAL8` (a játék nem használja), aztán
képkockánként egy `MAD1`. A képkockák az időbélyeg szerint jönnek (15 kép/s; a stáblista klipjei 13, az ARGLOGO 30).
**MAD1** (24 bites ág, `0x42ba80`): műveleti kódok 0xFF-ig, helyben, egy megmaradó képpufferbe; a színek R, G, B.
8 = kulcskép (törlés, 12×12-es egyszínű blokkok); 10 = a kép mentése „előzőnek”; 11 / 13 = B×B blokkok 2 bites kódokkal
(kihagy / mozgásvektor / egyszínű / vektor-eltérés; forrás az előző, illetve az aktuális kép); 12 = oszloponként 4×4-es
blokkok pixelenkénti kódokkal (bal, fölső, eltolás-táblák A/B, szó szerinti szín); 7 = blokkmozgatások listája.
A `.CLT` (RGB555 → palettaindex) és a `.PAL` csak 8 bites képernyőhöz kell, a portnak nem.
**Hang:** minden chunk egy `.asf` fájl az első 0x18 bájt nélkül: sztereó, 22050 Hz, 34 bájtos blokkok (bal, jobb:
vezérlőbájt + 16 bájt); a DPCM a hanghatásoké, a prediktor csatornánként blokkon és chunkon át folytatódik.
Esc a sorozat többi videóját is átugorja (`0x4a5cf0`), más gomb csak az aktuálisat. Indításkor: ARGLOGO, BRENDER, FXLOGO,
FXINTRO (`0x44427a`; ebben a FIGHT.EXE-ben minden videóútvonal `win.brp`-re van átírva, a nevek a CD-ről valók);
a létra végén WIN (`0x413f2d`), 1P game over után LOSE (`0x413e9d`, Rygillel nem).

## Egyéb
- `FIGHT/VIDEO/*.CLT/*.PAL/*.BRP`: FMV-k (Argonaut-formátum), ❌
- `boutgfx/*.pix`: HUD-grafikák (FIGHT, KO, ROUND, …), BRender pixmapok, ✅ olvashatók
- `boutgfx/colmap.tab`: 64 sor × 256 oszlopos árnyalótábla (BRender-pixelmap „shade_table”); a 48. sor a 10..137
  indexeket változatlanul adja, alatta a fekete (0x0A) felé sötétedik (≈ sor/48), fölötte a 137-es szín felé fehéredik.
- `frontend/shade.tab`: a menü árnyalótáblája.

### Színezés (BRender-indexvilágítás)
A renderelő nem a `FIGHT.EXE`-ben van, hanem a BRender 1.2 meghajtókban (`DLL\SOFTRNDX.BDD`, `SOFTPRIM.BDD`,
`SOFTIZ.BDD`). A harc egyetlen fénye (`0x42f6fd`) irányfény a világ +Z felől (felülről), erőssége 1/c, ahol c
a `0x4a4470[aréna]` táblából jön: 0,9, a 0., 8. és 9. arénában −0,6 (ott a fény gyakorlatilag alulról jön).
Környezeti fény nincs, csak az anyag ka-ja. Csúcspontonként (Gouraud) vagy lapon­ként:
`l = ka + (d > 0 ? kd·d + spec : 0)`, `d = s·(n·L)`, `spec = h > 0,6172 ? h·ks·s / (p − p·h + h) : 0`,
`h = n·normalize(L + szem)`, `l` a [0, 1) tartományba vágva. Textúra nélkül: `paletta[base + 0,5 + range·l]`;
megvilágított textúrával: `colmap[0,5 + 63·l][texel]`; világítás nélküli textúrával a nyers texel.
Harcosanyagok (`0x41c6c0`): mód = textúra·2 | Gouraud, ka/kd/ks a `0x4a4508` táblából (lapos: .3/.3/.5, lapos
textúrás: .4/.3/.5, Gouraud-textúrás: .5/.2/.1, p = 20). Textúrás módban a TX anyagok világítás nélküliek, Gouraud
esetén a TXBANK1–8 és BANK1–8 megvilágított és simított (a TXFBANK/FBANK mindig lapos); textúra nélkül minden anyag
a bankja rámpája (10 + 16(n−1), 15 lépés). Padló (`0x41cdcd`): textúrával a TXF/TXGFLOOR nyers texel; nélküle
TXGFLOOR .35/.30/.99 simított, TXFFLOOR .15/.45/.15 lapos, 10 lépéses rámpa. A port ezt CPU-n számolja (az
OpenGL-világítás ki van kapcsolva); a megvilágított textúrát `min(sor/48, 1)` szürkével modulálja.
