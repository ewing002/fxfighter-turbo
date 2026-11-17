# A 3D front end (winfront.c) – visszafejtési jegyzet és terv

Az eredeti menük nem 2D-s képek, hanem egy BRender 3D jelenet, amelyet egy bájtkódos szkript-VM mozgat
(a dekompilált kódban „strat block”). A C-kód csak az értelmezőt, az objektumrendszert, a triggereket,
a renderelést és néhány rendszerhívást adja. Minden menülogika, elrendezés, animáció és opciókezelés
bájtkódként van a `FIGHT.EXE` adatterületén.

Segédeszközök (a port mappájából futtatva, a `FIGHT.EXE` alapból `../FIGHT.EXE`, vagy `FIGHT_EXE=<útvonal>`):
- `PYTHONIOENCODING=utf-8 PYTHONPATH=tools py -3 tools/stratdis.py [n ...]` – a szkriptek visszafordítása
- `PYTHONIOENCODING=utf-8 PYTHONPATH=tools py -3 tools/menutab.py` – a menütáblák (elemek, pozíciók, kurzorhelyek)

## 1. Adatok az EXE-ben

| Cím | Tartalom |
|---|---|
| `0x49fb88` | szkript-mutatótábla, 93 bejegyzés; a bájtkód `0x499ba8`–`0x49fb88` (kb. 24 KB) |
| `0x4a0048` | műveleti kódok táblája, 238 bejegyzés (0x00–0xED), kezelők `0x404d8f`–`0x40c25a` |
| `0x4a0408` | triggertípusok, 16 bejegyzés |
| `0x4a2948` | modelltípus → modellnév a `crshipl1.dat`-ban, 212 bejegyzés |
| `0x49ae53` | karakter → portrémodell; `0x49d5a9` hordólap → karakter |

Fájlok a PAC-ban (`fight/frontend/`): `crshipl1.dat` (210 modell), `demo.mat` (246 anyag), `demo.pix`, `demo.pal`,
`align.pix` (név szerint betöltött textúrák, `0x4129c0`), `back1.pix` (háttér a keretekkel), `text.pix` (betűkészlet,
csak a stáblistához és üzenetekhez), `shade.tab` (8 bites árnyalótábla).

## 2. Végrehajtás

- **Főciklus** (`0x4135f1`): `back1.pix` a teljes képernyőre → bemenet (`0x42784c`) → N szkript-tick
  (`0x404902`, N = eltelt idő / 66,67 ms, legalább 1, tehát kb. 15 Hz) → a jobb és a bal világ a két oldalsó
  panelbe → a fő világ a teljes képernyőre. Ha a `DAT_004a1df0` (mód) beállt, kilép, és elindítja a játékot.
- **Objektum** (`0x404a96`, 0x108 bájt): pozíció (1000 = 1,0 egység), forgatás (0,1 fok), méret (256 = 1,0),
  sebesség, célpont, szülő/gyerek lánc, utasításmutató (`+0xf8`), verem, triggerlista, helyi változók
  (`+0x74`…`+0xd0`), világ (0 fő, 1 jobb, 2 bal). Mátrix: `RotY · RotX · RotZ`, eltolás, majd méretezés;
  a gyerekek transzformációja egymásba ágyazódik.
- **Globálisok:** `G[n]` = `DAT_004f3e20 + 4n` (16.16). Fontosak: G69 / G72 / G75 a felső / alsó / középső doboz
  állapota, G73/G74 kiválasztott / kurzor elem, G11/G12 a két játékos karaktere, G20–G57 az opciók,
  G58 belépési mód (visszatérés a játékból).
- **Értelmező** (`0x40c28c`): `ip = op[*ip](objektum)`, amíg nem enged át (yield). Ugrások a művelet saját
  címéhez képest, előjeles 16 bitesek. Utána fizika (`0x40c39e`), triggerek (`0x40c9ba`).
- **Triggerek:** 0 soha, 1 mindig, 2–8 minden 2…128. képkockában, 9 ütközés, 10 arg ≤ 0, 11 távol z-ben,
  12/13 rendszerbitek, 14 időzítő, **15 billentyű**. A billentyűk a játékosok bemeneti slotjai (P1 jobb/bal/le/fel/
  ütés/rúgás, P2 ugyanez, Esc), így a menü a beállított gombokkal és a kontrollerrel is működik.
- **Játék indítása:** a 0xD7 művelet írja a módot (`DAT_004a1df0`: 0 demó, 1 egyjátékos, 2 kétjátékos,
  3 állóképesség, 4 torna, 5 hálózat, 6 kilépés, 7/8/9 vissza a menübe) és a karaktereket, arénát, hendikepet.
- **Opciók:** `0x433c23` másolja a beállításokat a G20–G57 globálisokba, `0x434001` vissza.

## 3. Jelenet

- **Fő kamera:** perspektivikus, 45° látószög, hither 0,1, yon 50, képarány 1,46, a (0; 0; 0,2) pontban, −Z felé néz.
- **Oldalsó kamerák:** párhuzamos vetítés, hither 0,2, yon 20, szélesség 0,40, magasság 0,378, a saját világukban
  (±0,19; 0; 0)-ban. A panelek: bal x 30–143, jobb x 496–609, y 17–458.
- **Fények:** világonként egy irányított fény (Y körül 45°, majd még 45°).
- **Rajzolás:** az eredeti Z-rendezéssel rajzol, mélységpuffer nélkül; a portban mélységpuffer elég.
- **Dobozok:** a felső doboz (0; 63; −200), az alsó (0; −81; −200), méret 167. Állapotváltáskor 2 × 90°-ot
  fordulnak („kártyafordítás”), közben kicserélik a tartalmukat. A menüelemek a doboz gyerekei.

A képernyők részletes elrendezését a `tools/menutab.py` kimenete adja (főmenü, opciók, irányítás, részletesség,
menet, hang, gombok átállítása, aréna, VS panel, betűrács, statisztika).

## 4. Terv (kis lépésekben)

1. ✅ Felderítés, eszközök (`tools/stratdis.py`, `tools/menutab.py`, `tools/pe.py`).
2. **Eszközök a C-oldalon:** `crshipl1.dat` modellek, `demo.mat` anyagok, `demo.pal` + `demo.pix` / `align.pix`
   textúrák, és a bájtkód beolvasása futás közben a felhasználó `FIGHT.EXE`-jéből (a repóba nem kerül eredeti kód).
3. **Álló jelenet:** a három nézet (fő perspektivikus, két párhuzamos oldalsó), `back1.pix` háttér, fények;
   próbaként a főmenü kézzel elhelyezve a táblák alapján. Képernyőképpel ellenőrizve.
4. **Szkript-VM:** objektumkészlet hierarchiával, kb. 70 műveleti kód, verem, triggerek, 15 Hz-es tick.
   A hálózati műveletek (E1–E3) csonkok („nincs hálózat”).
5. **Összekötés:** bemenet → slotok, G20–G57 ↔ a port beállításai, 0xD7 → a port meccsindítása,
   hangok a `frontend` bankból, gombok átállítása (E4/E5/EB).
6. A régi 2D-s menük kivezetése, amikor a 3D front end mindent tud.

Kockázatok: a 8 bites paletta és a `shade.tab` árnyalás csak közelíthető; a 0xBD/0xBE és a D8–E0, E8–ED
rendszerműveletek jelentése csak részben ellenőrzött; a 15 Hz-es ütem ezredmásodperces időzítőt feltételez.
