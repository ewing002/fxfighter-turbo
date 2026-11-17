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
| `0x4231c0` | **kamera**: távolság 0,9·D + 1,6, a függőlegestől mért szög 95 − D/0,3 fok, magasság +0,5, a harcvonal követése 30 fokos holtsávval |
| `0x42106c` / `0x4a4be8` | kamera-előbeállítások (128 db: szög, dőlés, távolság, sebesség), a mozdulatok 0x21-es bájtja választja ki |
| `0x41d5b0` | háttér: a kamerához rögzített `bground.dat` sáv, kameraforduló = 320 egység görgetés |
| `0x4491f7` | találatvizsgálat: a támadó végtagjainak dobozai összevonva + az előző vizsgálat doboza (söprés) |
| `0x440f42` | ugrás és földet érés (a8290: függőleges eltolás, a legalsó láb a talajra igazítva) |

## Mérföldkövek
1. ✅ **Asset-néző**: modellek, textúrák, arénák, animációk
2. ✅ **Mozdulatrendszer** (kész: `src/fight.c`): a `moves/*.hdr` 16 bájtos paraméterei (klipindex, framek, sebzés, hitbox?), a `.ste` állapotgép, az input → mozdulat leképezés.
   Ehhez a `Fight.c` függvényeit kell visszafejteni, amelyek a `moves` adatokat olvassák (a `fight/moves/f%d.hdr` sztring xref-jeiből kiindulva).
3. ✅ **Harc**: ízületi találati kockák, folyamatos találatvizsgálat, sebzés, blokk, reakciók, dobások (idegen tábla), KO, körök, időzítő, forgó harcvonal, oldalazás, ugrás és kitörés, ringhatár, ringből kiesés · ⏳ kábulás (`cb3f8`), lábak talajhoz igazítása (`a8290`)
4. ✅ **HUD és menük**: `boutgfx/*.pix` sprite-ok (az aréna palettájával), `frontend/align.pix` (saját palettás, fejjel lefelé tárolt képek), `back1.pix`,
   betöltőkép, főmenü, karakter- és arénaválasztó, VS, 3 körös meccs, 1 játékos létra · ⏳ opciók menü (BESTO1..7, idő, nehézség, irányítás),
   folytatás (`continue.fxl`, `p1cont.cmp`), stáblista, rekordtábla, Jerzan (12. harcos) mint főellenség
5. ✅ **Hang**: CD-zene MP3-ként, `.asf` hangeffektek, bankok, bemondó · ⏳ `.slb` paraméterek (térhatás), menühangok
6. ◐ **AI**: most egy egyszerű helyettesítő AI van. Az eredeti a `0x425892`-ben található (az AI-átmenetek `val=0x00FF` jelűek, nehézségi szint, véletlen).
7. **Extrák**: gamepad (SDL_GameController), szélesvásznú kép, magas felbontás, online/LAN a DirectPlay helyett, FMV-lejátszó.

## Ismert hibák
- ~~A harcosok és arénák textúrái hibásak voltak~~ → javítva (4×4-es csempe, modellenként egy csempe).
- A sima színű lapok egy fix árnyalatot kapnak a bankból (¾-es fényerő) + OpenGL-világítást. Az eredeti a `colmap.tab` árnyalótáblával számolt, ezt pontosabban is lehetne emulálni.
- A háttér az eredeti `bground.dat` elrendezését követi; a függőleges eltolás egyik futásidejű értékét (0x4f1b5a) becsültem.
- A kamera a mozdulatok kamera-előbeállításait (dobások, győzelem, vereség) használja; a pálya szélén használt külön kamera és a SAT/CAR/SIDE kamerák még hiányoznak.
- Az ugrás íve most az eredeti képlet (FUN_00440f42: (t·v + t²·g/2)·s + 0,3), kb. 1,4 m magas. A talajhoz igazítás a modellek legalsó csúcspontját használja, így a talp a padlón áll.
- Ugrás közben a harcos nem jut ki a ringből (saját döntés); ütés, dobás vagy kilépés után továbbra is ki lehet esni.
- A ringből kiesés után gravitációval zuhan, és az aréna alsó szintjén ér földet (ha ott van geometria). A kamera ilyenkor a kiesőre áll; ez a beállítás saját, nem az eredetiből való.
- A kábulás (dizzy) mechanikája még hiányzik.
- A portrék és a harcosok összerendelése szemre történt; a PLYR2 = Linna és a PLYR9 = Kwondo bizonytalan.
- Jerzan (fm12.dat, `pan_cr12`) modellje még szétesik, ezért kimaradt az 1 játékos létrából. Valószínűleg ő a főellenség az arénában 0 (kisebb ring).
- A menük elrendezése az eredeti grafikákra épül, de a pontos pozíciókat (winfront.c) még nem fejtettem vissza.
