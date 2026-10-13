# Ghidra deep pass (2026-10-13)

The research reports' names are merged into one database, applied to the Ghidra project, and the export
is regenerated with the game's vtables walked. Start new work from `re/subsystems.md`.

## What was done

1. **Names merged into `re/names.tsv`** (561 entries: 414 functions, 147 labels and globals, 52 typed).
   Sources: the old names.tsv, every report's names table, `src/game.h` (patch sites as labels such as
   `Video_InitResolution_Res7Width`), `WIKI_LOG.md`, and names read from the code in this pass.
   Precedence, later wins: old names.tsv < display-pipeline < text-rendering < frame-timing < sky-2048 <
   startup-and-switches < mouse-input < frontend-menus < deep pass. The losing name stays in the note as
   "(also: X [report])". 13 addresses had conflicting names, for example `Lang_LoadTextTables`
   (also `Language_LoadResourcesAndFonts`), `StaticInit_FpsVars` (also `Debug_ToggleFpsCounter`, which was
   wrong) and `BDEngine_Present` (also `Render_Flip`). The old 4th column ("function" / "global") is
   replaced by real types.
2. **`re/types.tsv` (new):** `GameTimer`, `RealClock`, `Config`, `Widget`, `Button`, `TextRenderer`,
   `TextMenu`, `FrontEnd`, `CursorTracker`, `InputManager`. The globals that use them are typed
   (`g_GameTimer`, `g_Config`, `g_InputManager`, and `Widget *` / `TextMenu *` / `FrontEnd *` pointers),
   so the decompiler shows `g_Config.resolution`, `timer->fixedStep` and `this->textRenderer`.
3. **Timer corrected (H, read from the instructions).** frame-timing.md 5.1 was partly wrong: +0x00 is an
   unused base clock, not "status flags"; +0x44..+0x4c are offsets and the saved time; +0x50 is a second,
   unscaled clock (`RealClock`). `Timer_GetTime` 0x00402f10 returns `fixedTimeMs` (+0x3c) while `fixedStep`
   (+0x38) is set, else the unpaused scaled time + `fixedOffsetMs` (+0x44). `Timer_BeginFixedStep` 0x00402ea0
   (`stepMs` = 1000/fps) is called by `Game_Init` with 32 (31 ms steps) and, in the park, every frame while
   `g_CaptureEveryFrame` is set. `Timer_EndFixedStep` 0x00402ed0 is called at front-end init (0x0054e5ad) and
   every other park frame. `Timer_AdvanceFixedStep` 0x00402ef0 adds `stepMs` once per frame: the engine frame
   callback `Game_FrameCallback` 0x0054e2b0 calls 0x0040f220, which calls `Game_FrameTimerStep` 0x00409260,
   which calls it. `Game_FrameCallback` was not a function before this pass. So the start-up and the intro
   movies run on fixed 31 ms steps per frame, and the front end and park on scaled real time. The 36
   timer and clock functions are named; 27 have typed prototypes.
4. **Ghidra:** `MarkVisualStudio` and `RunRttiAnalyzerScript`, then `ApplyNames` (the skill's script; it
   found the 52 `std::` RTTI classes and nothing else, because the game classes have no RTTI), then the new
   **`tools/ghidra/TPWVtables.java`**, then `ExportCaches`.
   - **TPWVtables** finds vtables without RTTI: addresses that a `mov [reg+d], imm32` stores and that start
     a run of code pointers. It found 498 vtables (2,973 slots), created 792 functions reachable only
     through vtables and split 2 that had run past their end. It names unnamed slots `Class::vfXX` (owner =
     the smallest vtable holding the function at that slot) and makes them `__thiscall`, and names
     constructors and destructors. It builds a typed `Class_vftable` struct per vtable. 28 classes are named
     through `Class::vftable` lines in names.tsv (Widget, Button, TextRenderer, FrontEnd, FEScreenMgr, the
     five FE screens, D3DRenderer, SoftRenderer, DDrawContext, CursorTracker, DIMouse, DIKeyboard,
     QMixerDriver, FileMount, DirMount, WadMount, Win32File, BFString, ...). The other 470 are `Class_<vtable>`.
   - It also creates functions at other code pointers that follow alignment padding. These are mostly the
     **7,156 CRT static initialisers** (table 0x00727004..0x0072e0e8, nearly all building global `BFString`
     path constants) and the **6,513 atexit destructors** they register. They are named
     `StaticInit_<addr>` / `StaticDtor_<addr>` so readers can skip them. Keybind handlers, widget procs and
     menu callbacks also became functions this way.
   - Writes `re/ghidra-export/vtables.tsv` (the export's `classes.tsv` lists RTTI classes only).
5. **Export regenerated** in `re/ghidra-export/` (overwritten).
6. **`re/subsystems.md`**: entry points per subsystem. It includes first notes on movies, sound, files and
   saves, park ticks and the camera, which have no report yet.

## Counts

| | before | after |
|---|---|---|
| functions (functions.tsv) | 11,760 | 27,641 |
| decompiled ok / failed | 11,674 / 1 | 27,465 / 1 (0x004eab40, "Illegal op in iblock", as before) |
| project names applied | 0 (the 11 old names were never applied) | 561 (414 functions, 147 labels) |
| named functions (excl. FUN_, Unwind@, Catch@, imports) | 54 (library signatures) | 15,732: 457 plain + 406 in named classes + 1,660 `Class_xxx::` + 7,156 StaticInit + 6,513 StaticDtor |
| still `FUN_` | 7,978 | 7,721 |
| vtables / classes | 52 RTTI (std::) | 52 RTTI + 498 game vtables (28 named classes, 470 `Class_<addr>`) |
| structs in types.h | 0 | 555 (10 hand-typed, 498 vtable structs, class structs) |
| xrefs / symbols | 261,776 / 18,113 | 268,254 / 35,017 |

## Commands (to rerun)

Close the Ghidra GUI first (project lock). From the project root, in `cmd` (PowerShell and Git Bash pass the
`;` in `-scriptPath` badly to the .bat, so use cmd or a .cmd file):

```
set ROOT=C:\Users\Joe\Desktop\Dev\ThemeParkWorld-HD
set SKILL=C:\Users\Joe\.claude\skills\newgame-plus\tools\ghidra
C:\Users\Joe\Tools\ghidra_12.1.4_PUBLIC\support\analyzeHeadless.bat %ROOT%\re\ghidra TPW -process TP.exe -noanalysis ^
  -scriptPath "%SKILL%;%ROOT%\tools\ghidra" ^
  -postScript MarkVisualStudio.java -postScript RunRttiAnalyzerScript.java ^
  -postScript ApplyNames.java %ROOT%\re\names.tsv %ROOT%\re\types.tsv ^
  -postScript TPWVtables.java %ROOT%\re\ghidra-export\vtables.tsv ^
  -postScript ExportCaches.java %ROOT%\re\ghidra-export < nul
```

About 10 minutes, most of it the export (27k functions decompiled). `MarkVisualStudio` and
`RunRttiAnalyzerScript` are only needed after a fresh import, but they do no harm. The run is re-runnable:
ApplyNames resets the generated `Class::vfXX` names and TPWVtables puts them back. Names from names.tsv
always win. A second run created 150 more functions (code the first run's analysis had uncovered), and a
third created none, so the project is now stable. To add knowledge, edit `re/names.tsv` / `re/types.tsv`
and rerun. Naming a class means adding a `0x<vtable>	Name::vftable` line.

The Ghidra program is `TP.exe` in project `re/ghidra/TPW.gpr`.

## Problems and gotchas

- **First attempt looked wrong but wasn't:** the code-pointer pass created ~14,600 functions, which looked
  like garbage at first. A check of samples showed they are the static initialisers (`push str; mov ecx,
  obj; call BFString_Ctor; push dtor; call atexit`) and their destructor stubs. They are real code, so they
  stay, named.
- Split rule: a vtable slot inside an existing function splits it when padding or a ret/jmp comes before
  it. Other pointers split only at 16-aligned nop/int3 padding, because `__except` handlers and case labels
  sit inside functions. The split cuts only the body chunk that holds the target. The first version cut
  from the target to the function's end, and that removed whole `Unwind@` funclets.
- Vtable runs end at the next vtable start or the first non-code dword. 2 slots that were really data
  (0x00500000, 0x00520000) are dropped after function creation. The 1-slot vtables at
  0x00700b2c..0x00700bbc are real: small sound command objects built on the stack.
- Ownership by "smallest vtable" is a heuristic. Shared stubs (`CRT_purecall`, `Debug_Trace` as the empty
  method, `Stub_Ret0C`, `VThunk_AdjustThis_424ec0`) are left unowned, and the vtable structs name their
  slots `vfXX`. Ctor/dtor names are heuristics too (a dtor is a slot, or is called from a slot that calls
  `operator_delete`).
- Only class structs with a size line in types.tsv get the vftable pointer at +0 (5 now). An empty class
  struct is safer than a too-small one, because accesses past the end decompile as `this[n]`.
- Data inside .text (`c_dfDIMouse` 0x00679440, `c_dfDIKeyboard` 0x0067a460) is kept out of names.tsv,
  because ApplyNames would turn an address in an executable block into a function.
- Names from the reports that disagree with the code were fixed in the notes, not silently: `Video_PlayTGQFrame`
  0x0051b010 starts a movie (it is not a per-frame call), and `Sim_TickSimulation` 0x00520130 also ticks in the
  front end and walks one table of 0x140-byte objects, so it is probably not the whole park simulation.
- The old export's `types.h` had no structs, and its `functions.tsv` had none of the old names.tsv names: they
  had never been applied (ExportCaches ignores a names.tsv argument).
