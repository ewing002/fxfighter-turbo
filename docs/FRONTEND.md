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
  (`+0x74`…`+0xd0`), világ (0 fő, 1 jobb, 2 bal). Mátrix: `RotY · RotX · RotZ`, eltolás, majd *elő*-méretezés
  (`0x46ea60` = S · M, tehát a méret csak a modellre hat, a saját pozícióra nem); a gyerekek mátrixa a szülőével
  szorzódik, így a szülő mérete a gyerekek pozícióját is skálázza.
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
- **Rajzolás:** az eredeti Z-rendezéssel rajzol, mélységpuffer nélkül; a portban mélységpuffer elég. A textúrák
  0-s palettaindexe átlátszó (a dobozok elő- és hátlapja ~82%-ban, a portrék `EMPTY` kerete 91%-ban az), a lapok
  egyoldalúak (a hátsó lapokat a BRender nem rajzolja). Mindkettő kell: enélkül a dobozba beúszó portrét a keret
  fekete belseje takarja, a hengereken pedig átlátszanak a hátsó portrék.
- **Dobozok:** a felső doboz (0; 63; −200), az alsó (0; −81; −200), méret 167. Állapotváltáskor 2 × 90°-ot
  fordulnak („kártyafordítás”), közben kicserélik a tartalmukat. A menüelemek a doboz gyerekei.

A képernyők részletes elrendezését a `tools/menutab.py` kimenete adja (főmenü, opciók, irányítás, részletesség,
menet, hang, gombok átállítása, aréna, VS panel, betűrács, statisztika).

## 4. Terv (kis lépésekben)

1. ✅ Felderítés, eszközök (`tools/stratdis.py`, `tools/menutab.py`, `tools/pe.py`).
2. ✅ **Eszközök a C-oldalon** (`src/fe3d.c`, ellenőrzés: `tests/fecheck.c`; 210 modell, 246 anyag, ebből 235 textúrás – a hiányzó 11 az eredetiben sem létezik –, 93 szkript, 212 modelltípusból 208 párosítva): `crshipl1.dat` modellek, `demo.mat` anyagok, `demo.pal` + `demo.pix` / `align.pix`
   textúrák, és a bájtkód beolvasása futás közben a felhasználó `FIGHT.EXE`-jéből (a repóba nem kerül eredeti kód).
3. ✅ **Álló jelenet** (`src/fe3d_gl.c`, `--screen fe3d`; az oldalsó párhuzamos kamera magassága adja a 442 px-t, négyzetes pixelekkel – a portréhenger pontosan kitölti a panelt): a három nézet (fő perspektivikus, két párhuzamos oldalsó), `back1.pix` háttér, fények;
   próbaként a főmenü kézzel elhelyezve a táblák alapján. Képernyőképpel ellenőrizve.
4. ✅ **Szkript-VM** (`src/fe3d_vm.c`, fej nélküli teszt: `tests/fevm.c`; a `--screen fe3d` már a VM jelenetét
   rajzolja, `FX_FE_TICKS` tick és `FX_FE_INPUT` bemenet-szkript után): 512 objektum hierarchiával, verem,
   triggerek, 15 Hz-es tick. A 93 szkript ténylegesen 112 műveleti kódot használ, ezek mind megvannak; további
   kb. 40 (fordulás, követés, sebességfizika) a visszafejtésből készült, de egyik szkript sem használja, így
   ellenőrizetlen. Ellenőrizve: üresjáratban felépül a főmenü; a kurzor mozog; az OPTIONS megnyílik és
   bezárul; az 1P-indítás `START_GAME mode 1`-et ad; kb. 61 s tétlenség után demó (mode 0; a gyökérszkript
   30×30 tickig számol, nem 30 s-ig); véletlen bemenettel 50 000 tick alatt az 1–4. és 6–9. mód mind
   előáll, szkripthiba nélkül. Csonkok és eltérések:
   - hálózat: E1 = 0, E2 = −1, E3/E8 üres („nincs hálózat”, az 5. mód így elérhetetlen);
   - E4 (nyers billentyű): az eredeti blokkol, itt a művelet a következő tickben újrapróbál
     (`fe_vm_key_raw`, DirectInput-kódok);
   - kamerák (0x96, 0x2A): nincs hatásuk, a renderelő kamerái rögzítettek;
   - háttér (0x97): csak feljegyzés (`bg[]`), a felső doboz textúracseréje még nincs bekötve;
   - rendszerműveletek (D8–E0, E9/EA, EC/ED): a cfg ↔ G átírást a VM végzi, a fájl-, hangerő- és
     zenekezelés a gazdára vár (`sys` visszahívás);
   - a 12/13-as triggertípusok nem sülnek el, ütközés nincs (egy szkript sem állít maszkot);
   - a világpozíció függvény az eredetihez hűen 1/10 szkriptegységet ad;
   - a méret tengelyenkénti (a középső doboz 256/128/128), ezért `FeNode.scale[3]`.
5. ✅ **Összekötés** (`src/main.c`, `fe_*` függvények; a 3D menü az alapértelmezés, `--menu2d` a régi):
   - bemenet 15 Hz-en: P1 W/A/S/D + F/G (Enter, Space is), P2 nyilak + K/L, kontrollerek, Esc/Backspace;
     a tickek közti rövid lenyomásokat a port megjegyzi; nyers billentyűk (E4) DirectInput-kóddal (`di_scan`);
   - hangok a `frontend` bankból (0. bank; a `#` jelű bemondások sorban, egymás után, lásd `docs/FORMATS.md`),
     zene: E9 → 14. sáv, EA/START_GAME → leáll;
   - a szkriptek az eredeti 15 Hz-en futnak, de a port minden képkockát rajzol: a VM megőrzi az előző tick helyzeteit,
     és a rajz a két tick között interpolál (`fe_vm_scene_lerp`; tickenként 120°-nál nagyobb fordulás vagy 0,4
     egységnél nagyobb ugrás nem simul). A rajz így legfeljebb egy tickkel (67 ms) késik;
   - beállítások: a port `options.cfg`-je ↔ a konfigurációs blokk (nehézség = `0x4a587c`, menetidő 30/60/90/végtelen,
     győzelmek 1–3 ↔ 1/3/5 menet, hangerő 1–8 ↔ 0–10, elcsúszás nélkül). Mint az eredetiben, a SAVE (D9/DD) ment,
     a LOAD (D8) tölt, a hangerő-csúszkák (DF/E0) élőben hatnak; meccsindításkor a G → cfg átírás mentés nélkül is érvényes;
   - START_GAME: 0 bemutató (gép–gép, egy menet, bármely gomb megszakítja), 1 létra (a port létratáblájával),
     2 kétjátékos (a VM karaktereivel és arénájával), 3 állóképesség, 4 torna (lent), 6 kilépés, 7/8/9 a menü
     újraindítása, 5 (hálózat) nem érhető el;
   - **állóképesség** (`FUN_00419219`): p1 / p2 egy-egy legfeljebb 4 fős csapat, a felső bájttól lefelé; bájtonként
     `& 0xf` a harcos, `0x20` = ember (`DAT_004f3264` emberi maszk: 1P = 1, 2P = 3, bemutató = 0). Az aréna a 2. csapat
     első harcosáé, a menetidő végtelen (`0x4a587e = -1`). Egy-egy menet után csak a vesztes hozza a következő harcosát,
     és csak az ő élete áll vissza 120-ra (a meccsfüggvény `0x4f3268 == 0x16` mellett nem tölti fel). A győzelem-jelek
     helyén a csapatban váró harcosok látszanak; ha valamelyik csapatnak egy harcosa maradt, a bemondó „utolsó harcos”
     (generic 11). Amelyik csapat elfogy, az veszít;
   - **torna** (`FUN_00431676`, `Wraptour.c`; a VS-képernyő a 29., az eredménytábla a 88. szkript): az induló bájtok
     sorrendje p1>>24, p2>>24, p1>>16, p2>>16, …, `& 0x1f` a harcos, `0x20` = ember; 4 vagy 8 induló (1–4, illetve 5–8
     ember, a többi gépi). Helyosztós ágrajz 20 statikus csomóponttal (`0x4a6500`): 8 indulónál 4 negyeddöntő, 2 vigaszág,
     2 elődöntő, a 7., az 5., a 3. hely és a döntő (12 meccs), 4 indulónál 2 elődöntő, 3. hely, döntő. A párosítás a
     helyek sorrendje; ember a gép ellen mindig az 1. oldalra kerül; az aréna a 2. oldali harcosé; a menetek és az idő
     a szokásosak. Gép–gép meccset bármely gomb véletlen győztessel átugor. Harc közben a névpanel `pan_pl<n>` vagy
     `pan_cpu`. A 2. meccstől a front end VS-képernyője jön (belépési mód 1, G59/G60 a két harcos 16.16-ban, G63 a két
     címke: induló 0–7, 8 = cpu), a végén a helyezések (belépési mód 6, G59/G60 a helyezettek harcosai, G64/G65 az
     indulók, nyers bájtokként, felül a jobb hely); a táblázatról ütés a főmenübe visz. Nincs ágrajz-képernyő.
     A tornán a 0-s hangbank a `tour.fxl` (`0x413a6d`: a `generic` lista + 20–30 a harcosnevek + 31 „wins”), és
     minden menet végén a bemondó a győztes nevét mondja (`0x42263c`: harcos + 0x13), teljes életnél utána a
     „perfect”-et (7); a „you win” / „player n wins” helyett;
   - hendikep (2P): a START_GAME 5–6. paramétere (1–8) a konfigurációs blokk 0–1. bájtjába kerül (`0x40bce7`), és
     két ember között a támadó sebzését szorozza 0,7…1,4-gyel (a 4 semleges; lásd `docs/FORMATS.md`, sebzés). A port
     az `options.cfg`-ben tartja (`handicap1`, `handicap2`, alapból 4; az eredeti gyári értéke 3);
   - a meccs után (vereség, létra vége, 2P, bemutató, Esc) vissza a 3D menübe (G58 = 0);
   - gombkiosztás: a harci bemenet (`0x428036`) játékosonként 6 gombot olvas a `0x4a588a + 6p` címről: ATTACK
     (előre a kezdőoldalról), RETREAT, JUMP, DUCK, PUNCH, KICK (DirectInput-kódok; bitek 0x01/0x02, 0x08, 0x04, 0x10,
     0x20). Az ATTACK és a RETREAT szerepe felcserélődik, ha a harcosok oldalt cserélnek (`0x4a8300`, a kamerafüggvény
     állítja), vagyis a gombok képernyő-irányok: az 1. játékosnál az ATTACK a jobbra, a 2.-nál a balra gomb. A port a
     12 kódot az `options.cfg`-ben tartja (`key1`…`key12`, alapból D A W S F G és ← → ↑ ↓ K L), a menü CONTROLS →
     REDEFINE KEYS képernyője ezeket állítja (SAVE ment), és harcban, valamint a menüben is ezek érvényesek. A harci
     gyorsgombok (B, N, R, M, X, 1, 2…) nem élnek, ha egy emberi játékos gombja. A nyers billentyűre várás (E4)
     indulásakor a VM elfelejti a korábbi lenyomásokat, különben a helyet kijelölő ütésgombot venné fel;
   Ellenőrizve gyorsított, szkriptelt futással (`FX_AUTOTEST=1 FX_SPEED=10 FX_FE_INPUT=…`): 1P-létra, 2P (hendikep- és
   LOADING-képernyővel), bemutató, kilépés, SAVE, gombátállítás és -mentés, állóképesség, torna (VS- és eredményképernyő
   képernyőképen, `FX_FE_ENTRY`/`FX_FE_G`). Hátra van: rekordtábla (DE), játéksebesség (EC), részletesség, a létra folytatása (ED, G67), a felső doboz képcseréje (0x97),
   a visszatérés pontos belépési módja (G58) a meccs után.
6. A régi 2D-s menük kivezetése, amikor a 3D front end mindent tud.

Kockázatok: a 8 bites paletta és a `shade.tab` árnyalás csak közelíthető; a 0xBD/0xBE és a D8–E0, E8–ED
rendszerműveletek jelentése csak részben ellenőrzött; a 15 Hz-es ütem ezredmásodperces időzítőt feltételez.
