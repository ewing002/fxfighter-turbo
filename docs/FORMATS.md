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
Sebzés = `op × fi[0x40] × fi[0x44] × nehézségi szorzó` (gyakorlatilag az `op`). Visszalökés: 0,1 / 0,2 / 0,3 az irány (`cond>>6`) szerint.
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
"ugyanaz a játékos, a másik tábla" (`StartMove(p^3)`); a 0xE8 a két harcos távolságát állítja (`target << 9`, 16.16).
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
- 0-s bank: `generic` (bemondó: 1 "FIGHT", 2 győzelem, 3 vereség, 4 idő, 7 hibátlan, 14/15 P1/P2 nyert, 16 hirtelen halál, 17 visszaszámlálás, 19 teleport)
- 1/2-es bank: a harcos neve (`magnon.fxl`, …, 45 bejegyzés)
- arénabank: `b%d.fxl` (4 bejegyzés: lépés, puffanás, landolás, dobbanás)

Lejátszás (`FUN_00435eb4(bank, index)`): a mozdulat 0x18-as mezője a `StartMove`-ban, illetve a 0xDE opkód `target` mezője.
< 0x80: a játékos saját bankja (1-alapú), 0x80–0xEF: az arénabank (0-alapú), ≥ 0xF0: nincs hang.
A találati hangokat a reakciómozdulatok saját hangmezője adja (pl. HIT FACE L = 1 = `mag_l5`).

### `sfx/*.slb`
Hangeffekt-paraméterbankok (float mezők; feltehetően térhatás/hangerő). A port egyelőre nem használja.

### Zene
- Zene: CD-audio sávok (MCI, `FUN_0043adf8`). Harc közben az `aréna + 2`-es sáv szól (`FUN_0043b077(a8308 + 2)`), a stáblistánál a 2-es, demó módban a 14-es.
  Az 1. sáv a CD adatsávja (MODE1/2352). A kép egyetlen BIN fájl, a hang benne **big-endian** 16 bites sztereó 44,1 kHz.

## Egyéb
- `FIGHT/VIDEO/*.CLT/*.PAL/*.BRP`: FMV-k (Argonaut-formátum), ❌
- `boutgfx/*.pix`: HUD-grafikák (FIGHT, KO, ROUND, …), BRender pixmapok, ✅ olvashatók
- `boutgfx/colmap.tab`, `frontend/shade.tab`: árnyalótáblák a 8 bites rendereléshez (a modern portnak nem kellenek)
