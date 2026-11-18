# Roadmap – az FX Fighter Turbo teljes portja

## Az eredeti felépítése (az EXE assert-üzeneteiből)
`C:\Projects\Turbo\Source\`: `Fight.c` (harcmotor), `Fsupport.c` (harcos betöltése), `FXInit.c`, `winfront.c` (menük),
`Sfxroot.c`/`Sfxproj.c`/`Fsound.c`/`Windsoun.c` (hang), `Effects.c`, `Sprlib.c`/`Sprplay.c` (2D sprite-ok, HUD),
`Pixmap.c`, `Fastfile.c` (PAC), `Video.c` (FMV), `continue.c`, `Credits.c`, `Poollist.c`, `Dump.c`.
Fordító: Watcom C/386 (regiszteres hívási konvenció: eax, edx, ebx, ecx).
A BRender statikusan van belinkelve, fixpontos (16.16) buildként.

## Hasznos címek a FIGHT.EXE-ben
| Cím | Mit csinál |
|---|---|
| `0x41c300–0x41c500` | harcos betöltése (`fm%d.dat`, `shadow%d.dat`, `f%d.ani`, `f%d.cmp`) |
| `0x41f0a0` | `.ani` betöltő |
| `0x445dc8` | testrész-actorok felépítése (lapos lista) |
| `0x445f16` | **képkocka alkalmazása a csontokra** (a portban `ani_bone_matrix`) |
| `0x445430` | a harc fő ciklusának egy része (frissítés és póz mindkét harcosra) |
| `0x466c90` | BrModelLoadMany |
| `0x46d490` / `0x46e5c0` / `0x46e700` / `0x46e960` | BrMatrix34RotateZ / PostRotateX / PostRotateY / PostTranslate |
| `0x4a8780 + i*4` | klipszám harcosonként |
| `0x4c71e0 + i*4` | animációs tábla pointere harcosonként |
| `0x4231c0` | **kamera**: távolság 0,9·D + 1,6, a függőlegestől mért szög 95 − D/0,3 fok, magasság +0,5, a harcvonal követése 30 fokos holtsávval. D = √(dx² + dy² + h²) a követett medence-pozíciókból (`a8320`/`a8330`: 0,1 holtsáv, ¼ sebesség), h = a magasabb medence (csak 3-nál kisebb távolságnál frissül); a középpont a követett pozíciók átlaga, 0,01-es holtsávval |
| `0x42106c` / `0x4a4be8` | kamera-előbeállítások (128 db: szög, dőlés, távolság, sebesség), a mozdulatok 0x21-es bájtja választja ki |
| `0x41d5b0` | háttér: a kamerához rögzített `bground.dat` sáv, kameraforduló = 320 egység görgetés |
| `0x4221b2` | kamerarendező: mozdulatkamera; ring széle (harcos a széltől 1,8-on belül, távolság < 4: szög 90 ± 12·(3,5 − táv), dőlés 83 − 8·(3,5 − táv)), ha a részletesség (`0x4a5888`) ≥ 1 |
| `0x421728` | rögzített nézetek: Home be/ki (`4a4be0`), Ins/Del a 8–15. előbeállítás, PgUp/PgDn távolság (2–8), nyilak szög/dőlés (30–80) |
| `0x44c666` / `0x44cba0` / `0x44d868` | sprite-effekt kérése (sor, képkockánként 4), indítása, a `.slb` szkript értelmezője (`src/sprfx.c`) |
| `0x433350` / `0x433630` | a meccs `.slb` könyvtárainak betöltése, effekt keresése azonosító és játékos szerint |
| `0x4491f7` | találatvizsgálat: a támadó végtagjainak dobozai összevonva (`0x448a69`, z-ben a kisebbik felső határ) + az előző vizsgálat doboza (söprés); zóna a talált kocka középpontjából |
| `0x43fbca` / `0x4408f9` / `0x43f36e` | taszítás, ringhatár (a harcvonal mentén mérve), ringből kiesés (csak a középtől messzebb lévő harcos) |
| `0x440f42` | ugrás és földet érés (a8290: függőleges eltolás, a legalsó láb a talajra igazítva) |
| `0x496150` | 1 játékos létra: harcosonként 4 sorrend × 15 bájt (7 harcos, majd a főellenségek: 10, 11, 9), a 12-es sorai üresek |

## Mérföldkövek
1. ✅ **Asset-néző**: modellek, textúrák, arénák, animációk
2. ✅ **Mozdulatrendszer** (kész: `src/fight.c`): a `moves/*.hdr` 16 bájtos paraméterei (klipindex, framek, sebzés, hitbox?), a `.ste` állapotgép, az input → mozdulat leképezés.
   Ehhez a `Fight.c` függvényeit kell visszafejteni, amelyek a `moves` adatokat olvassák (a `fight/moves/f%d.hdr` sztring xref-jeiből kiindulva).
3. ✅ **Harc**: ízületi találati kockák, folyamatos találatvizsgálat, sebzés, blokk, reakciók, dobások (idegen tábla), KO, körök, időzítő, forgó harcvonal, oldalazás, ugrás és kitörés, ringhatár, ringből kiesés, kábulás (`cb3f8`), kombó-skálázás (`0x4aa470`), kiszabadulás nyomkodással (0xCC–0xCE) · ⏳ lábak talajhoz igazítása (`a8290`)
4. ✅ **HUD és menük**: `boutgfx/*.pix` sprite-ok (az aréna palettájával), `frontend/align.pix` (saját palettás, fejjel lefelé tárolt képek), `back1.pix`,
   betöltőkép, főmenü, karakter- és arénaválasztó, VS, 3 körös meccs, 1 játékos létra · ⏳ opciók menü (BESTO1..7, idő, nehézség, irányítás),
   folytatás (`continue.fxl`, `p1cont.cmp`), stáblista, rekordtábla (✅ a 3D menüben)
5. ✅ **Hang**: CD-zene MP3-ként, `.asf` hangeffektek, bankok, bemondó, menühangok. Az eredeti nem használ térhatást (nincs
   pan, hangmagasság vagy távolság szerinti halkítás); a `.slb` fájlok nem hangparaméterek, hanem sprite-effektkönyvtárak
   (✅ `src/sprfx.c`: blokk-szikrák, vér, a különleges mozdulatok effektjei, vízi fröccsenés, teleport; lásd `docs/FORMATS.md`.
   A lövedékek (`Sfxproj.c`) halott kód: a Turbo mozdulattáblái nem indítják őket)
6. ✅ **AI**: az eredeti `0x425892` visszafejtve (`ai_think` a `src/fight.c`-ben, leírás: `docs/FORMATS.md`), nehézségi szint az OPTIONS menüben ·
   a `0x424d8f` ringszél-vizsgálat is az eredeti szerint (a követett medencétől 2 egységre: balra, jobbra vagy hátra)
7. **Extrák**: gamepad (SDL_GameController), szélesvásznú kép, magas felbontás, online/LAN a DirectPlay helyett, FMV-lejátszó (✅ `src/video.c`).

## Ismert hibák
- ~~A harcosok és arénák textúrái hibásak voltak~~ → javítva (4×4-es csempe, modellenként egy csempe).
- A színezés az eredeti BRender-indexvilágítás (egy felülről jövő fény, aréna szerinti erősség, a 0., 8. és 9. arénában
  alulról), CPU-n számolva. A `colmap` tábla hatását két textúrafokozat közelíti (sötétítés, 48 fölött
  keverés a 137-es szín felé); eltérés, hogy a textúrázás perspektivikus (az eredetiben affin), és a keverés folytonos, nem
  palettaszínekre kerekített.
- A háttér az eredeti `bground.dat` elrendezését követi; a függőleges helyzete az eredeti képlete (`0x44556c`, `0x41d5b0`):
  y = 11 + trunc(250·cos(dőlés))/2, a `0x4f1b5a` = 178 minden arénában.
- A kamera a mozdulatok kamera-előbeállításait, a ring szélén használt nézetet és a rögzített nézeteket az eredeti szerint kezeli. A saját sebzésre (op 100–149) a kamera függőlegesen megrázkódik (0,1 amplitúdó, tickenként 45°, 0,008-cal csillapodik).
  A „SAT CAM / CAR CAM / SIDE CAM” BRender-kamerák (`0x4a1e38`) halott adatok, semmi nem hivatkozik rájuk.
- Az ugrás íve most az eredeti képlet (FUN_00440f42: (t·v + t²·g/2)·s + 0,3), kb. 1,4 m magas. A talajhoz igazítás a modellek legalsó csúcspontját használja, így a talp a padlón áll.
- Ugrás közben a harcos nem jut ki a ringből (saját döntés); ütés, dobás vagy kilépés után továbbra is ki lehet esni.
- A ringből kiesés után gravitációval zuhan, és az aréna alsó szintjén ér földet (ha ott van geometria). A kamera ilyenkor a kiesőre áll; ez a beállítás saját, nem az eredetiből való.
- A kábulásnak nincs külön látványeleme: a `0x4f1a48 = 0x17f` (`0x449148`) írás halott, semmi nem olvassa; a látható rész a DAZE mozdulat, ez a portban is megvan.
- A portrék és a harcosok összerendelése a `frontend/demo.mat` anyagaiból jön (pl. KIKO → PLYR2, LINNA → PLYR6, KWONDO → PLYR9).
- A menük az eredeti 3D jelenetek, az eredeti szkriptekkel (`docs/FRONTEND.md`); a régi 2D-s menük kikerültek.
- Jerzan (fm12.dat, `pan_cr12`) az eredetiben félbehagyott: a létratáblájában nem szerepel, a teste pontnyira zsugorított, az animációja Magnoné. A portban rejtett harcos (bővítés): Magnon testrészeivel és Jerzan textúrájával jelenik meg, a választóban Magnonon rúgás + ütéssel választható.
