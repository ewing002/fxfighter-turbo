# FX Fighter Turbo – nyílt port

## Cél
Az eredeti Windows-os FX Fighter Turbo újraírása modern rendszerre. Új C/SDL2 kód fut az eredeti `FIGHT.PAC` adatain. A dokumentáció és a felhasználóval folytatott kommunikáció magyar nyelvű.

## Hol tart most
- **Kész:** asset-betöltés (modellek, textúrák, arénák, animációk), harcmotor (ütések, blokk, dobás, ugrás, ringből kiesés, körök, időzítő), hang (CD-zene MP3-ban, ASF-effektek, bankok, bemondó), 3D kamera az EXE-ből visszafejtett paraméterekkel, HUD az eredeti sprite-okkal, főmenü, betöltőkép, karakter- és arénaválasztó, VS-képernyő, 3 körös meccs, 1 játékos létra (az eredeti sorrendtábla), gépi ellenfél (az eredeti AI, `0x425892`), kontrollertámogatás, OPTIONS menü nehézségi szinttel, mentett beállítások.
- **Nincs élőben kipróbálva:** igazi Windowson és igazi kontrollerrel. A tesztek Linuxon, Xvfb-ben és headless szimulátorral futottak.

## Könyvtárszerkezet
- `src/assets.[ch]` – PAC, CMP, pixmap, modell, anyag, ANI betöltés
- `src/fight.[ch]` – harcmotor (mozgás, találat, állapotgép)
- `src/audio.[ch]` – hangkeverő, ASF-dekóder, zene (`dr_mp3.h`)
- `src/ui.[ch]` – 2D sprite-ok, 640×480 virtuális képernyő
- `src/main.c` – renderelés, kamera, menük, meccslogika, bemenet, opciók
- `src/tables.h` – EXE-ből kinyert táblák (ütésvonalak, reakciók)
- `tools/` – Python segédek (unpac, cmp, brmodel, ani, moves, asf, pixlib, rip_cd)
- `tests/fightsim.c` – headless CPU-vs-CPU szimuláció
- `docs/FORMATS.md`, `docs/ROADMAP.md`, `README.md` – formátumleírás, mérföldkövek, használat
- `bin-win64/` – Windows-os exe és SDL2.dll

## Referenciaanyagok (nem része a repónak)
- Eredeti `FIGHT.EXE` és `FIGHT.PAC` – a játék mappájából. A futtatáshoz a `FIGHT.PAC` kell.
- Ghidra-dekompilált EXE (`fight_decomp.c`) – függvénykeresésre. Ha nincs meg, a `docs/FORMATS.md` és a kommentek az elsődleges forrás.
- Kicsomagolt adatok – `tools/unpac.py` állítja elő a `FIGHT.PAC`-ból.

Az EXE-ben való kereséshez használt cím-táblázat a `docs/ROADMAP.md` alján van.

## Fordítás és futtatás
- **Linux:** `mkdir -p build && cd build && cmake .. && make`
- **Windows (MinGW keresztfordítás, SDL2 2.30.9):**
  ```
  S=<sdl2>/x86_64-w64-mingw32
  x86_64-w64-mingw32-gcc -O2 -std=c11 -Dmain=SDL_main -I$S/include/SDL2 \
    src/main.c src/assets.c src/fight.c src/audio.c src/ui.c \
    -L$S/lib -lmingw32 -lSDL2main -lSDL2 -lopengl32 -mwindows -o bin-win64/fxfighter.exe
  ```
  A `SDL2.dll` a `bin-win64/` mappában van.
- **Windows (natív, ezen a gépen):** MSYS2 MinGW64 gcc és SDL2 a `C:\msys64\mingw64` alatt. Git Bash-ből, a port mappájában:
  ```
  export PATH=/c/msys64/mingw64/bin:$PATH
  mkdir -p build-win
  gcc -O2 -std=c11 -Dmain=SDL_main -I/c/msys64/mingw64/include/SDL2 \
    src/main.c src/assets.c src/fight.c src/audio.c src/ui.c \
    -L/c/msys64/mingw64/lib -lmingw32 -lSDL2main -lSDL2 -lopengl32 -mwindows -o build-win/fxfighter.exe
  gcc -O2 -Isrc tests/fightsim.c src/fight.c src/assets.c -lm -o build-win/fs.exe
  ```
  Futtatáshoz a `bin-win64/SDL2.dll`-t a `build-win/` mellé kell másolni. A `FIGHT.PAC` eggyel feljebb van: `../FIGHT.PAC`. Windowson nincs szükség Xvfb-re, a `--screenshot` közvetlenül futtatható.
- **Szimulátor:** `gcc -O2 -Isrc tests/fightsim.c src/fight.c src/assets.c -lm -o fs`, majd `./fs <FIGHT.PAC> <p1> <p2> <ticks> <seed>`.
  - Környezeti változók: `V=1` (mozdulatok), `SND=1` (hangok), `FLOOR=1` (talajtávolság), `FORCE_IN=<hex>` és `HOLD=<tick>` (adott bemenet tartása), `NOMESH=1` (modell nélkül), `AI=<0..7>` (nehézség, alapértéke 4). A játékban és a szimulátorban `FX_AITRACE=1` kiírja az AI döntéseit.
- **Képernyőkép:** `./build/fxfighter <FIGHT.PAC> --p1 4 --p2 6 --ticks 130 --screenshot out.ppm`. Menühöz `--screen title|menu|select|arena|vs|options`. Headless futtatáshoz `xvfb-run -a`.
- **Gyorstesztelés:** `FX_AUTOTEST=1` (állapotnapló), `FX_AUTOWIN=1` (a 2. játékos ne legyen CPU), `FX_SPEED=8` (gyorsított idő).
- Parancssori kapcsolók, amelyek kihagyják a menüt: `--p1`, `--p2`, `--arena`, `--2p`, `--demo`, `--fight`, `--viewer`.

## Fontos technikai tudnivalók
- Játékfrekvencia: 48 Hz (`FIGHT_HZ`).
- Harcosmodellek: `fm%d.dat`, a modell lokális −Y irányba néz, Z felfelé.
- Mozdulattáblák: `moves/f%d.hdr` (36 bájtos rekordok) és `.ste` (12 bájtos állapotgép-lépések). Részletek: `docs/FORMATS.md`.
- Kamera: `0x4231c0` (képlet; középpont és D a harcmotor követett medence-pozícióiból, `Player.tx/ty`), `0x4a4be8` (előbeállítások), `0x4221b2` (rendező: mozdulatkamera, ring széle). A mozdulat 0x21-es bájtja választja ki az előbeállítást. `FX_CAMTRACE=1` kiírja a ring szélén használt nézet váltásait.
- Találat: a támadó dobozai összevonva az előző ellenőrzés dobozával (söprés), `0x4491f7`. Az ütközések (találat, taszítás, ringhatár, kiesés) az eredeti szerint mennek, az eredeti hibáival együtt. Részletek: `docs/FORMATS.md`.
- Ugrás: `FUN_00440f42` képlete, +0,3 alappal.
- Talajhoz igazítás: a modell legalacsonyabb pontja (`lowest_joint`).
- Hangbankok: FXL fájlok. A `frontend` bank menüben, a `generic` harcban van.
- AI: nem gombot nyom, hanem átmenetet választ a mozdulatfejléc 0x19–0x20-as bájtjai alapján. Részletek: `docs/FORMATS.md`.

## Nyitott feladatok (prioritás szerint)
1. **AI apróságok:** az eredeti AI kész. A `0x424d8f` pontos eltolása hiányzik. A 13-as harcos a szimulátorban összeomlik.
2. **Jerzan (12. harcos):** nem kell javítani. Az eredeti sosem állítja ki: a létratáblában (`0x496150`) az ő sorai üresek, a `fm12.dat`/`fm12b.dat` testrészei pedig szándékosan pontnyira vannak zsugorítva (csak a fej, a kezek és a lábfejek látszanak). Félbehagyott tartalom.
3. **Kábulás (dizzy):** a mechanika kész (kábulásmérő, DAZE mozdulatok, kombó-skálázás, kiszabadulás nyomkodással). Az effektje (`f1a48`) hiányzik.
4. **Kamera:** kész a ring szélén használt nézet (`0x4221b2`), a 8 rögzített nézet (`0x421728`) és a rázkódás. A SAT/CAR/SIDE CAM csak halott BRender-adat (`0x4a1e38`), nincs mit portolni. A ringből kiesés kamerája saját.
5. **Menük:** az eredeti front end egy BRender 3D jelenet (`frontend/crshipl1.dat`: 210 modell, pl. forgó opciókockák, nyolcszögek; `demo.mat` anyagok az `align.pix` textúráival; a `0x412a7a` építi fel, egy objektum/trigger szkript mozgatja). A port 2D-s menüi ennek átértelmezései, a pozíciók szemre vannak. A portrék párosítása a `demo.mat` alapján már pontos.
6. **Folytatás, stáblista, rekordtábla, FMV:** nincs kész.
7. **Színezés:** a `colmap.tab` árnyalótáblát nem emulálja.
8. **Háttér:** a függőleges eltolás (`0x4f1b5a`) becsült.

## Nyitott kérdések (a felhasználótól)
- Az ugrás közben tiltott ringből való kiesés saját döntés, nem az eredetiből származik.
- Egy elrugaszkodás élőben kicsit rántást okozhat (szimulációban látszott).

## Munkamód
- Kis lépésekben haladunk. Minden változtatás után buildelünk, screenshotot vagy szimulációs logot nézünk, és frissítjük a dokumentációt.
- Kiadás előtt a Linuxos és a Windowsos buildet is ellenőrizzük.
- A felhasználó visszajelzései alapján haladunk a következő mérföldkőre, a `docs/ROADMAP.md` szerint.
- A felhasználóval magyarul beszélünk, a kódkommentek és a dokumentáció is magyar.
