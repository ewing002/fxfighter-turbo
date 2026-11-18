# FX Fighter Turbo – nyílt port (C11 + SDL2 + OpenGL)

Új, platformfüggetlen kód, amely **az eredeti `FIGHT.PAC` adatait használja**. Az eredeti EXE-t nem futtatja és nem emulálja.
Windows 10/11-en, Linuxon és macOS-en is fordítható.

## Állapot: 3. mérföldkő – teljes játékmenet menükkel
- ✅ A `FIGHT.PAC` archívum, az RLE textúrák, a paletták, a BRender modellek, anyagok és pixmapok olvasása
- ✅ Mind a 12 aréna (padló + 360°-os háttér) és a harcosok textúrázott 3D modelljei
- ✅ **Csontanimációk**: a formátum és a csontmátrix képlete az EXE-ből visszafejtve, minden klip lejátszható
- ✅ **Mozdulatrendszer**: az eredeti `moves/*.hdr`/`.ste` állapotgép, az EXE-ből visszafejtve (ütések, rúgások, kombók, blokk, guggolás, ugrás, gurulás)
- ✅ **Harc**: találatvizsgálat a testrészek alapján, reakciótáblák, sebzés, KO, körök, időzítő, életerőcsíkok
- ✅ **3D mozgás**: forgó harcvonal (oldalazás, lendítő ütések), ugrás, kitörés, valódi ringméret, **ringből kiesés** (zuhanás az aréna alsó szintjére), a lábak nem süllyednek a talajba
- ✅ **Kamera az EXE-ből**: alacsony, a harcosok távolságától függő nézet, 45°-os látószög, mozdulatokhoz kötött kamera-beállítások (dobások, győzelem); a háttér az eredeti módon görög
- ✅ **Zene**: a CD zenei sávjai MP3-ként (`port/music/trackNN.mp3`), arénánként az eredeti sáv (aréna + 2)
- ✅ **Hangeffektek**: az eredeti `.asf` minták (bitpontos DPCM-dekóder), harcos-, aréna- és bemondóbankok (`.fxl`), mozdulatokhoz kötött hangok
- ✅ **Menük és HUD az eredeti grafikákkal**: betöltőképernyő, főmenü, karakterválasztó portrékkal, arénaválasztó (2 játékos), VS-képernyő bemondóval,
  ROUND / FIGHT! / KO / RING OUT! / TIME OVER / WINNER / YOU WIN / YOU LOSE / GAME OVER feliratok, eredeti életerőcsík, időzítő és névcímkék
- ✅ **Meccsek**: 3 körből 2 nyert kör kell. 1 játékos módban sorban jön a többi 10 harcos, mindig az ellenfél saját arénájában. Vereség = GAME OVER.
- ✅ **Gépi ellenfél**: az eredeti AI visszafejtve, 8 nehézségi szinttel
- ✅ **Rekordtábla**: a létra végén az eredeti névbeíró képernyő és táblázat, mentve a `hiscores.dat`-ba
- ⏳ FMV-k, stáblista: lásd `docs/ROADMAP.md`

## Futtatás Windows alatt
Indítsd el a `port\bin-win64\fxfighter.exe`-t. A program magától megtalálja a két szinttel feljebb lévő `FIGHT.PAC`-ot.
Ha máshová teszed, a `FIGHT.PAC`-ot rakd mellé, vagy add meg az útvonalát paraméterként.

**Menük:** az eredeti 3D menük futnak: a menüszkripteket a port a saját `FIGHT.EXE`-dből olvassa ki (a `FIGHT.PAC` mellől),
és az eredeti módon futtatja őket. Az 1. játékos a W/A/S/D gombokkal mozog, és F-fel (vagy Enterrel, Space-szel) választ, G = rúgás.
A 2. játékos a nyilakkal mozog, és K-val választ, L = rúgás. Vissza és kilépés: Esc (vagy Backspace). Az OPTIONS menüben a SAVE gomb
menti a beállításokat, a LOAD visszatölti őket. A menetidő, a menetek száma, a nehézség és a hangerők akkor is érvényesek, ha nem mented őket.
A menükhöz kell a `FIGHT.EXE`; ha hiányzik, a program hibaüzenettel kilép (menü nélküli harc: `--p1 N --p2 N`, lásd lent).
A gombokat az OPTIONS → CONTROLS → REDEFINE KEYS menüben lehet átállítani (ATTACK = előre, RETREAT = hátra a kezdőoldalról nézve);
a menüben és harcban is ezek érvényesek, a SAVE elmenti őket. Az alábbi táblázat az alapkiosztást mutatja.
ENDURANCE: két, legfeljebb 4 fős csapat; a menet vesztese hozza a következő harcosát, a győztes megtartja az életét, időkorlát nincs.
TOURNAMENT: 4 vagy 8 induló (ember vagy gép), minden helyet kijátszanak; a gépi meccseket bármelyik gomb átugorja (véletlen győztessel).
Még hiányzik: nincs hálózati játék; a részletességi szintek nem hatnak a harcra.

**Kontroller (gamepad):** Xbox-, PlayStation- és más SDL-kompatibilis kontrollerek, menet közben is csatlakoztathatók. Az első kontroller az 1. játékosé, a második a 2. játékosé.
Harcban: d-pad vagy bal kar = mozgás, X / Y / LB / LT = ütés, A / B / RB / RT = rúgás, Start = szünet, Back/Select = vissza a főmenübe.
Menükben: d-pad vagy bal kar = mozgás, A / X / Start = választás, Y = rúgás, B / Back = vissza. Ha a fighter sérül, a kontroller rezeg (kikapcsolható).

**Beállítások:** a menetidő, a menetek száma, a nehézség, a hendikep, a hangerők és a gombok a 3D menü OPTIONS részében állíthatók.
A többi csak az `options.cfg`-ben: `fullscreen` (0/1, F11-gyel vagy Alt+Enterrel is), `vsync`, `ringouts` (ringből kiesés), `rumble` (rezgés),
`round_time=99` (az eredetiben nincs ilyen menetidő).
A beállításokat a program elmenti (Windowson: `%APPDATA%\FXFighter\Turbo\options.cfg`); a 3D menük ugyanezt a fájlt használják. A rekordtábla ugyanitt van (`hiscores.dat`). A teljes képernyő F11-gyel vagy Alt+Enterrel is váltható.

**Harc** (alapkiosztás)

| | 1. játékos | 2. játékos |
|---|---|---|
| balra / jobbra | A / D | ← / → |
| ugrás / guggolás | W / S | ↑ / ↓ |
| ütés / rúgás | F / G | K / L |

A 2. játékost alapból a gép irányítja. **F3**: P2 ember/gép, **F4**: P1 gép (demó), **F5**: új kör, **P**: szünet, **R**: ringből kiesés be/ki, **M**: zene be/ki, **X**: hangeffektek be/ki, **− / =**: hangerő,
**1 / 2**: P1 harcos csere, **9 / 0**: P2 harcos csere, **N**: következő aréna. Ezek a gyorsgombok nem élnek, ha egy emberi játékos harci gombja.
Néhány mozdulat: hátra + ütés nyomva tartva = védekezés, le + rúgás = gurulás, előre nyomva tartva = futás.

**Néző mód (F1)**: ← / → animáció, ↑ / ↓ és PgUp / PgDn harcos, Space szünet, , / . léptetés.
Harc közben: egérhúzás = kamera forgatása (a kamera magától követi a harcvonalat), görgő = zoom, B = háttér, F11 = teljes képernyő, **Esc = vissza a főmenübe**. A bemutató harcot (tétlenség után indul) bármelyik gomb megszakítja.
Rögzített kameranézetek, mint az eredetiben: **Home** = be/ki, **Insert / Delete** = következő / előző a 8 nézetből, **PgUp / PgDn** = közelebb / távolabb,
nyilak (ha a 2. játékos a gép) = forgatás és dőlés; Shift-tel finomabb lépések.

A 2. játékost 1 játékos módban a gép irányítja. Teszteléshez (F3/F4/F5/R/1/2/9/0/N/F1) a régi gyorsgombok is működnek.

Parancssori opciók: `--arena N --p1 N --p2 N --2p --demo --viewer --fight` (ezek kihagyják a menüt, és végtelen meccset indítanak),
valamint `--clip N --time S --yaw R --ticks N --screenshot out.ppm --screen fe3d`.

## Zene
A játék eredetileg CD-audio sávokat játszott le. A `tools/rip_cd.py` a CD-képből (BIN + CUE) MP3-at készít:
```
python3 tools/rip_cd.py "cd/FX Fighter Turbo.cue" "cd/FX Fighter Turbo.bin" port/music
```
A program a zenét a `music/` mappában keresi: az exe mellett, egy szinttel feljebb, illetve a `FIGHT.PAC` melletti `port/music/` mappában.
Ha nincs zene, a játék anélkül fut. Sávok: 2 = stáblista, 3–13 = az arénák (aréna + 2), 14 = demó mód.

## Fordítás
**Linux/macOS:** SDL2 fejlesztői csomag kell hozzá (`libsdl2-dev` vagy `brew install sdl2`).
```
cmake -S . -B build && cmake --build build
```
**Windows (MSVC vagy MinGW):** telepítsd az SDL2-t (vcpkg: `vcpkg install sdl2`), utána ugyanez a CMake-parancs.
MinGW-vel kereszt-fordítva:
```
x86_64-w64-mingw32-gcc -O2 -std=c11 -Dmain=SDL_main -I<SDL2>/include/SDL2 src/main.c src/assets.c src/fight.c src/audio.c src/ui.c ^
  -L<SDL2>/lib -lmingw32 -lSDL2main -lSDL2 -lopengl32 -mwindows -o fxfighter.exe
```

## Mappák
- `src/assets.[ch]`: az összes formátum betöltője
- `src/fight.[ch]`: harcmotor (mozdulat-állapotgép, bemenet, találat, körök)
- `src/tables.h`: az EXE statikus táblái (testrészcsoportok, reakciótáblák)
- `src/main.c`: renderelés, kamera, bemenet, HUD, a 3D menü bekötése és a meccslogika, néző
- `src/ui.[ch]`: a harc 2D sprite-jai (`boutgfx/*.pix`) 640×480-as virtuális képernyőn
- `src/fe3d*.[ch]`: a 3D menü (eszközök, szkript-VM, renderelő)
- `src/audio.[ch]`, `src/dr_mp3.h`: zene (MP3, dr_mp3 v0.7.2, public domain / MIT-0) és hangeffekt-keverő, `.asf`-dekóder
- `tests/fightsim.c`: fej nélküli harcszimuláció (két CPU), regressziós teszthez
- `tools/*.py`: Python-eszközök (kicsomagolás, dump, formátumkutatás)
- `docs/FORMATS.md`: a visszafejtett formátumok leírása
- `docs/ROADMAP.md`: a további munka terve

A játékadatok (`FIGHT.PAC`) nincsenek benne a repóban, azokat az eredeti játékból kell venni.
