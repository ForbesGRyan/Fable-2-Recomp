# Fable 2 frame map and sub-project 3 recommendation

Status: sub-project 3 (capture layer and clay pass) implemented and validated. Census, pitch ablation, tiling capture and discovery (D1-D4, sections 8 and 9) are complete. Validation (spec success criteria, plan Task 14) is in section 10: alignment passed at three locations, gameplay coverage D / C = 0.949 over a 25-minute run, no crash, view-off capture 0.04 ms per frame. The uncapped F3 reading is still pending (see "Pending"). Sections 1b, 2c, 3, 5, 6b and 7 carry the gameplay census results.
Gameplay capture: `out\build\win-amd64-release\logs\d3d_census_20261001_140912.jsonl` (900 rows, 30 s walking in the world, armed after 90 s; no repeated `gpu_frame`).
Menu capture used: `out\build\win-amd64-release\logs\d3d_census_20261001_130522.jsonl` (600 rows). Row 1 accumulates every call since process start and is dropped by `summarize_census.py`. The capture has two distinct segments (section 2): frames 2-480 are a static screen (likely splash/loading/title) and frames 481-600 are a 3D scene. Treat the 3D segment as the one that matters. This capture predates `gpu.gpu_frame`, so repeated GPU frames could not be dropped.

## 1. D3D function map

Copied from `docs/native-renderer/xdk-map.md` (Skate 3 TU3 reference, Fable 2 address, verified column).

| Name | Skate 3 TU3 | Fable 2 | Confidence | Score | Runner-up | Via | Verified | Notes |
|---|---|---|---|---|---|---|---|---|
| D3DDevice_SetIndices | 0x82B79190 | 0x8219CCD8 | fuzzy | 0.972 | 0.647 |  | yes | verified by disassembly (36/36 instrs same mnemonics order-swapped; offsets/regs differ) |
| D3DDevice_SetStreamSource | 0x82B78FF0 | 0x821B6DA0 | fuzzy | 0.958 | 0.571 |  | yes | verified by disassembly (same body; Fable 2 variant takes one extra arg, stride/offset math differs) |
| D3DDevice_DrawIndexedVertices | 0x82B7AD68 | - | none | 0.969 | 0.870 |  | no | 0x8221E0F0; unmatched: best candidate rejected by margin rule (sibling draw functions are near-identical); trace correlation inconclusive (menu only) |
| D3DDevice_DrawVertices | 0x82B7A970 | - | none | 0.986 | 0.851 |  | no | 0x8221C518; unmatched: best candidate rejected by margin rule (sibling draw functions are near-identical); trace correlation inconclusive (menu only) |
| D3DDevice_BeginVertices | 0x82B79FC0 | - | none | 0.961 | 0.721 |  |  | 0x822060A0 |
| D3DDevice_SetPixelShader | 0x82B7F408 | - | none | 0.983 | 0.903 |  |  | 0x822324E0 |
| D3DDevice_SetVertexShader | 0x82B7F150 | - | none | 0.973 | 0.894 |  |  | 0x82208CE8 |
| D3DDevice_SetRenderState | 0x82B83C48 | 0x8221C908 | fuzzy | 0.979 | 0.598 |  | yes | verified by disassembly (48 instrs, 45 identical positions; one mr/lwz reorder) |
| D3DDevice_SetViewport | 0x82B74310 | 0x821D1270 | fuzzy | 0.915 | 0.587 |  | yes | verified by disassembly (same shape; reorder, struct offsets 0xc->8, 0x4228->0x4158, arg r4 literal 0x420) |
| D3DDevice_SetScissorRect | 0x82B769C0 | 0x821F9BD0 | fuzzy | 0.980 | 0.421 |  | yes | verified by disassembly (150 instrs, 144 identical positions; struct offsets 0x3148->0x3098) |
| D3DDevice_SetPending_AluConstants | 0x82B83FE0 | - | none | 0.911 | 0.635 |  |  | 0x8221DF98 |
| D3DDevice_Swap | 0x82B82E08 | - | none | 0.311 | 0.308 |  |  | 0x8260A0E8 |
| RegisterTexture | 0x82C9A618 | - | none | 0.614 | 0.614 |  |  | 0x830207F0 |
| D3DDevice_SetIndices.callee1 | 0x82B85888 | 0x82B9B340 | callgraph | 0.948 |  | fuzzy | yes | verified by disassembly (callgraph; 48 vs 49 instrs, one extra li r5,0 before call) |
| D3DDevice_SetRenderState.callee1 | 0x82B83600 | 0x8228EAF0 | callgraph | 0.989 |  | fuzzy |  |  |
| D3DDevice_SetViewport.callee0 | 0x82B74840 | 0x82206820 | callgraph | 0.981 |  | fuzzy |  |  |
| D3DDevice_SetScissorRect.callee1 | 0x82B78EF8 | 0x821F9AD8 | callgraph | 0.968 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1 | 0x82B754C0 | 0x821E8D20 | callgraph | 0.984 |  | fuzzy |  |  |
| D3DDevice_SetViewport.callee0.callee1 | 0x82B74610 | 0x822065F0 | callgraph | 0.932 |  | fuzzy |  |  |
| D3DDevice_SetScissorRect.callee1.callee0 | 0x82B766D0 | 0x821F97E8 | callgraph | 0.910 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee0 | 0x82B74F78 | 0x82283530 | callgraph | 0.960 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee2 | 0x82B753E8 | 0x82286430 | callgraph | 0.916 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee3 | 0x82B75260 | 0x821E8B98 | callgraph | 0.990 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee4 | 0x82B755C0 | 0x82242628 | callgraph | 0.951 |  | fuzzy |  |  |
| D3DDevice_SetViewport.callee0.callee1.callee1 | 0x82B74558 | 0x82206538 | callgraph | 0.978 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee2.callee2 | 0x82B74438 | 0x822AAFB8 | callgraph | 0.943 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee3.callee1 | 0x82B85948 | 0x82B9B408 | callgraph | 0.948 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee3.callee5 | 0x82B749E0 | 0x821D17B8 | callgraph | 0.981 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee4.callee2 | 0x82B76080 | 0x82B9BF90 | callgraph | 0.963 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee4.callee3 | 0x82B75FB8 | 0x82B9BEC8 | callgraph | 0.980 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee3.callee5.callee1 | 0x82B744E0 | 0x82B9B770 | callgraph | 1.000 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee4.callee2.callee2 | 0x82B86F00 | 0x82BA0E80 | callgraph | 0.969 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee4.callee2.callee2.callee1 | 0x82B86BB0 | 0x82BA0B30 | callgraph | 0.969 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee4.callee2.callee2.callee1.callee14 | 0x82B867F0 | 0x82BA0770 | callgraph | 1.000 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee4.callee2.callee2.callee1.callee20 | 0x82B86870 | 0x82BA07F0 | callgraph | 0.929 |  | fuzzy |  |  |
| D3DDevice_SetRenderState.callee1.callee1.callee4.callee2.callee2.callee1.callee32 | 0x82B86AD8 | 0x82BA0A58 | callgraph | 0.916 |  | fuzzy |  |  |

Notes:
- `Score` is the opcode-sequence similarity for fuzzy matches and for near misses; a `none` row with a
  candidate in Notes is a rejected near miss (best candidate too close to the runner-up), not a match.
- `Via` is `strict`/`loose` when every ancestor of a call-graph row was an exact match, else `fuzzy`;
  call-graph rows under a fuzzy ancestor are accepted only if the callee pair itself scores >= 0.85.
- `Verified` is yes when the row was compared by side-by-side disassembly.
- Rows with confidence `none` have no confirmed Fable 2 address.


## 1b. Draw function identification

Question: which Fable 2 function is the real `D3DDevice_DrawIndexedVertices` (Skate 3 `0x82B7AD68`)? The signature matcher rejected `0x8221E0F0` (score 0.969, runner-up 0.870) only by the margin rule, and the census never saw it called.

**Census facts (whole capture, 599 frames).** `D3DDevice_SetIndices` (0x8219CCD8) was called 385 times in total, from exactly one call site: LR `0x821B6F50`. So there are not three callers to rank; there is one. (`D3DDevice_SetStreamSource` has 8 call sites; the top two, `0x821CB1A8` and `0x821CB120`, have 4401 calls each, both inside `0x821CA7E8`.)

**Containing function of the only SetIndices caller.** `.pdata` start `0x821B6EC0`, 43 instructions (172 bytes). Disassembly (`python tools\xdk_sigmatch\sigmatch.py disasm out\xdk\fable2 0x821B6F40 12`): four `bl 0x821B6DA0` (SetStreamSource), then `li r4,0; lwz r3,0x19c(r31); bl 0x8219CCD8` (SetIndices with a null buffer), then `bl 0x821B6F70` and `bl 0x821B5358`, then the epilogue.

`bl` targets after the SetIndices call within that function:

| Call site | Target | Size | Fuzzy score vs Skate 3 DrawIndexedVertices | What it is |
|---|---|---|---|---|
| 0x821B6F50 | 0x821B6F70 | 44 instrs | 0.168 | loops over 0x14 per-slot table entries; calls `0x821B7020` for empty slots (clears bound slots) |
| 0x821B6F54 | 0x821B5358 | 51 instrs | 0.195 | similar slot-clearing loop, calls `0x82B60D10` and `0x821B7020` |

Neither is a draw (Skate 3 DrawIndexedVertices is 277 instructions). Conclusion: `0x821B6EC0` is a "reset stream sources / index buffer / bound slots" function, and the census SetIndices hits are null resets, not per-draw binds. SetIndices is therefore not a usable anchor for finding the indexed draw in the menu.

**Best candidates for DrawIndexedVertices, scanning every Fable 2 function within +/-40% of 277 instrs by fuzzy opcode-sequence score:**

| Fable 2 function | Instrs | Fuzzy score | Static `bl` callers | Seen in census |
|---|---|---|---|---|
| 0x8221E0F0 | 276 | 0.969 | 8 (in functions `0x821A3AE8`, `0x821DDF70`, `0x821FEA90`, `0x8221BCC8`, `0x8221EBC0`, `0x82225CC8`, `0x82B21448`, `0x82FE8AF0`) | never called |
| 0x8221C518 | 252 | 0.870 | 11 | 4401 calls (the hooked `DrawVertices?`) |
| 0x82217EE8 | 327 | 0.785 | not checked | not hooked |
| 0x8221C9C8 | 259 | 0.784 | not checked | not hooked |
| 0x82207C30 | not recorded | 0.778 | not checked | not hooked |
| 0x822060A0 | not recorded | 0.736 | not checked | hooked as `BeginVertices?` (1342 calls, section 6); not a draw confirmation |

Evidence that `0x8221C518` is a real draw function (DrawVertices-style): it is called from `0x821CA7E8` (626-instr function) at `0x821CB188`, immediately before that function's `bl 0x821B6DA0` (SetStreamSource unbind) at `0x821CB1A4`, the draw-then-unbind order; its body calls 0x8221C908 (mapped as SetRenderState) 8 times and AluConstants (0x8221DF98) twice, and the 0x8221C908 LR `0x8221C60C` (4398 calls) lies inside it, matching its 4401 calls.

Caveat: the argument "DrawVertices calls SetRenderState" is weak. An XDK draw function normally does not call the public `SetRenderState`; either `0x8221C908` is really an internal state setter (so its identification as the public SetRenderState is wrong for this purpose) or one of the two identifications is wrong. The draw-then-unbind order and the 4401-call match are the stronger evidence.

**Verdict.** `0x8221E0F0` remains the most likely real `D3DDevice_DrawIndexedVertices`: one instruction different in size from the Skate 3 function (276 vs 277), 0.969 similarity, and the next-best candidate is a different (non-indexed) function. Confidence: medium (about 65%). It is unconfirmed because the menu never executes it; gameplay should hit it if it is correct. `0x8221C518` is, at medium-high confidence (about 80%), `D3DDevice_DrawVertices` (call-site evidence above, 0.986 similarity to Skate 3's DrawVertices per the xdk-map). Neither hook explains the draw rate (section 2): in the 3D segment about 87% of `gpu.draws` have no hooked draw call, and `DrawIndexedVertices?` never fires even in 3D.

Recommendation (no hook edits made in this task): re-run `gen_census_hooks` with an annotation promoting `0x8221E0F0` and `0x8221C518` to named draw hooks, add census hooks for `0x82217EE8` and `0x8221C9C8`, and capture a gameplay run. If `0x8221E0F0` still never fires, the indexed draw is inlined at its call sites or reached through a different entry.

**Gameplay update (capture `d3d_census_20261001_140912`).** `0x8221E0F0` fires 975.5 times per frame in gameplay (correlation with `gpu.draws` +0.951), mostly from `0x8221BCC8` (822/frame) and `0x82B21448` (135/frame). Sampled arguments look like an indexed draw: `r3` = device (`0x4413A780`), `r4` = 6 (`D3DPT_TRIANGLESTRIP` on Xenos), `r7` = an index count (e.g. `0x557`), `r8` = `0x30`. The menu simply never takes this path. Treat `0x8221E0F0` as `D3DDevice_DrawIndexedVertices` (confidence high: size, 0.969 similarity, gameplay rate, arguments, and it builds a `DRAW_INDX` header, section 6b).

**Complete draw-emitter set (static scan, `tools\xdk_sigmatch\pm4_emitters.py out\xdk\fable2`).** The scan lists every `.pdata` function that builds a PM4 type-3 header with `lis rX,0xC0xx` + `ori/addi`. Exactly seven functions build `DRAW_INDX` (0x22) and seven build `DRAW_INDX_2` (0x36, indices in the packet: clears, resolves, rect lists). Section 6b has the table. Hooking these 14 covers every guest draw that the game emits directly.

## 2. Frame statistics by segment (real capture)

The menu capture is two segments (gameplay is section 2c), summarized with `summarize_census.py --from-frame/--to-frame` (row 1 dropped):

- Frames 2-480 (479 frames): static screen, likely splash/loading/title.
- Frames 481-600 (120 frames): 3D scene. Frame 481 (19.5 ms IssueDraw CP time, 9.7 ms resolve CP time, 244 draws) is the first 3D frame and most likely pipeline/shader creation, so steady-state claims below use frames 482-600 (119 frames).

Commands:
```
python tools\xdk_sigmatch\summarize_census.py <capture> --from-frame 2 --to-frame 480
python tools\xdk_sigmatch\summarize_census.py <capture> --from-frame 482 --to-frame 600
```

### 2a. Menu 3D scene (frames 482-600 of the menu capture, steady state)

| Quantity | Median | Mean | p90 / max |
|---|---|---|---|
| emulated draws per frame | 281 | 293.6 | p90 337 |
| resolves (copies) per frame | 18 | | |
| guest frame time (`guest_ms`) | 33.32 ms | 33.32 ms | max 65.9 ms |
| swap interval (`swap_interval_ms`) | 33.25 ms | 33.34 ms | max 36.4 ms |
| emulated IssueDraw CP time (`draw_cpu_ms`) | 0.308 ms | 0.436 ms | max 5.27 ms |
| resolve CP time (`copy_cpu_ms`) | 0.077 ms | 0.079 ms | max 0.141 ms |

Draws per render-target pitch (median per frame): 1280: 158, 560: 79, 320: 37, 280: 4, 160: 2.

Hooked draw calls / emulated draws: median 0.118, total (sum over frames) 0.126, so about 87-88% of emulated draws have no hooked draw call. `DrawVertices?` fires in 115 of the 120 frames 481-600 (4401 calls in total, up to 59 per frame, all from LR `0x821CB18C`); `DrawIndexedVertices?` (0x8221E0F0) never fires.

Frame 481 for reference: 244 draws, `draw_cpu_ms` 19.53, `copy_cpu_ms` 9.69 (excluded above).

### 2c. Gameplay (capture `d3d_census_20261001_140912`, 899 frames)

`python tools\xdk_sigmatch\summarize_census.py out\build\win-amd64-release\logs\d3d_census_20261001_140912.jsonl`

| Quantity | Median | Mean | p90 / max |
|---|---|---|---|
| emulated draws per frame | 2560 | 2911.1 | p90 4011, max 4127 |
| resolves (copies) per frame | 39 | | p90 43 |
| guest frame time (`guest_ms`) | 33.31 ms | | p90 35.83, p99 37.51, max 38.99 |
| swap interval (`swap_interval_ms`) | 33.26 ms | | p90 35.13, max 37.50 |
| emulated IssueDraw CP time (`draw_cpu_ms`) | 6.65 ms | | p90 8.22, max 12.29 |
| resolve CP time (`copy_cpu_ms`) | 1.27 ms | | p90 1.76, max 3.58 |

Draws per render-target pitch (median per frame): 1120: 1779, 1040: 340, 320: 139, 1280: 133, 560: 114, 280: 112, 0: 49, 80: 37, 640: 11, 160: 7, 520: 2.

Hooked draw calls / emulated draws: median 0.359, total 0.373. Gameplay is roughly ten times the menu's 3D scene (2560 vs 281 draws, 39 vs 18 resolves), and the main scene moves from pitch 1280 to pitches 1120 and 1040.

### 2b. Static screen (frames 2-480)

| Quantity | Median | Mean | max |
|---|---|---|---|
| emulated draws per frame | 26 | 25.9 | p90 26 |
| resolves (copies) per frame | 1 | | |
| guest frame time | 34.53 ms | 36.50 ms | 111.4 ms |
| swap interval | 33.68 ms | 36.60 ms | 97.7 ms |
| emulated IssueDraw CP time | 0.182 ms | 0.196 ms | 3.66 ms |
| resolve CP time | 0.021 ms | 0.022 ms | 0.506 ms |

Draws per pitch (median per frame): 1280: 25, 640: 1. No hooked draw call ever fires in this segment (ratio 0.0). This segment is not representative of gameplay and must not drive pitch recommendations.

Notes on the numbers:
- `gpu.draws` counts every `IssueDraw` call including early-return calls (no-op draws, clears) and, during ablation runs, draws that were suppressed. Treat it as an upper bound on drawn geometry.
- Nested `.calleeN` call counts overlap (a callee is also counted inside its parent's callees); do not sum them.
- The census pairs `funcs` (calls since the previous guest iteration) with `gpu` (the last swap the GPU thread closed), which may repeat or skip. Newer captures carry `gpu.gpu_frame` and the summarizer drops consecutive repeats; this capture predates that key.

### Top call sites (whole capture, 599 frames)

### D3DDevice_SetIndices

| Caller (LR) | Calls |
|---|---|
| 0x821B6F50 | 385 |

### D3DDevice_SetStreamSource

| Caller (LR) | Calls |
|---|---|
| 0x821CB1A8 | 4401 |
| 0x821CB120 | 4401 |
| 0x821B6F44 | 385 |
| 0x821B6F28 | 385 |
| 0x821B6F0C | 385 |
| 0x821B6EF0 | 385 |
| 0x82AB071C | 5 |
| 0x82AB05CC | 5 |

### D3DDevice_SetRenderState

| Caller (LR) | Calls |
|---|---|
| 0x8221C60C | 4398 |
| 0x82206F9C | 2203 |
| 0x82218038 | 1351 |
| 0x822062A8 | 1342 |
| 0x8220627C | 1342 |
| 0x8220622C | 1342 |
| 0x82206204 | 1342 |
| 0x822061D8 | 1342 |
| 0x822061B8 | 946 |
| 0x8222BC30 | 541 |

### D3DDevice_SetScissorRect

| Caller (LR) | Calls |
|---|---|
| 0x821F9E98 | 9265 |
| 0x821C6760 | 122 |

### D3DDevice_SetViewport.callee0

| Caller (LR) | Calls |
|---|---|
| 0x822074A8 | 2673 |
| 0x822181C0 | 2291 |
| 0x822181AC | 2291 |
| 0x821EFA0C | 2187 |
| 0x8220634C | 1342 |
| 0x821D12C8 | 1098 |
| 0x8228359C | 847 |
| 0x821E8D8C | 725 |

### D3DDevice_SetScissorRect.callee1

| Caller (LR) | Calls |
|---|---|
| 0x821F9DA0 | 9387 |
| 0x821C676C | 122 |
| 0x821C66A0 | 122 |
| 0x821B9D38 | 122 |

### D3DDevice_SetRenderState.callee1.callee1

| Caller (LR) | Calls |
|---|---|
| 0x82B9D3BC | 599 |
| 0x822A5CBC | 122 |
| 0x8227F328 | 122 |
| 0x821C6518 | 122 |
| 0x821B9BBC | 122 |
| 0x82193144 | 122 |
| 0x82BA4ED0 | 1 |
| 0x82B9D110 | 1 |
| 0x82B9CD5C | 1 |
| 0x82242680 | 1 |

### D3DDevice_SetViewport.callee0.callee1

| Caller (LR) | Calls |
|---|---|
| 0x82206870 | 13820 |
| 0x821D1844 | 599 |

### D3DDevice_SetScissorRect.callee1.callee0

| Caller (LR) | Calls |
|---|---|
| 0x821F9BB8 | 9753 |
| 0x8219CEF0 | 1463 |
| 0x8222BFD0 | 1455 |
| 0x82207660 | 1342 |
| 0x82206DFC | 1342 |
| 0x822A63D0 | 843 |

### D3DDevice_SetRenderState.callee1.callee1.callee0

| Caller (LR) | Calls |
|---|---|
| 0x821E8D70 | 725 |
| 0x82286D2C | 122 |

### D3DDevice_SetRenderState.callee1.callee1.callee2

| Caller (LR) | Calls |
|---|---|
| 0x821E8DAC | 725 |
| 0x821E8CD4 | 725 |
| 0x82286EFC | 122 |
| 0x82286EC8 | 122 |
| 0x82286E7C | 122 |

### D3DDevice_SetRenderState.callee1.callee1.callee3

| Caller (LR) | Calls |
|---|---|
| 0x821E8DB4 | 1213 |

### D3DDevice_SetViewport.callee0.callee1.callee1

| Caller (LR) | Calls |
|---|---|
| 0x822066E8 | 13820 |
| 0x82206800 | 599 |

### D3DDevice_SetPixelShader?

| Caller (LR) | Calls |
|---|---|
| 0x8222C2FC | 2528 |
| 0x822324C8 | 2056 |

### D3DDevice_SetVertexShader?

| Caller (LR) | Calls |
|---|---|
| 0x82208CDC | 3928 |
| 0x82220A00 | 2061 |

### D3DDevice_SetPending_AluConstants?

| Caller (LR) | Calls |
|---|---|
| 0x8221C560 | 4401 |
| 0x8221C580 | 4233 |
| 0x82217FAC | 2291 |
| 0x82217F8C | 2291 |
| 0x8220614C | 732 |
| 0x8220612C | 122 |

### D3DDevice_BeginVertices?

| Caller (LR) | Calls |
|---|---|
| 0x821EBFD4 | 1342 |

### D3DDevice_SetViewport

| Caller (LR) | Calls |
|---|---|
| 0x822A5DF0 | 122 |
| 0x822A5DD4 | 122 |
| 0x822A5DB8 | 122 |
| 0x822A5D9C | 122 |
| 0x822A5D80 | 122 |
| 0x822A5D64 | 122 |
| 0x822A5D48 | 122 |
| 0x821B9CB0 | 122 |

### D3DDevice_SetRenderState.callee1.callee1.callee2.callee2

| Caller (LR) | Calls |
|---|---|
| 0x822864A8 | 493 |

### D3DDevice_DrawVertices?

| Caller (LR) | Calls |
|---|---|
| 0x821CB18C | 4401 |



Reading the numbers:
- In the 3D scene the median frame is 281 emulated draws (p90 337) over five pitches, 18 resolves, one 30 Hz vsync interval.
- `DrawVertices?` fires in 115 of the 120 3D-segment frames (and never in the static segment). Even there the hooked draw functions explain only about 12-13% of `gpu.draws`; about 87-88% of 3D-scene draws are unhooked, so the real draw submission path is mostly not covered by the current hooks.
- `DrawIndexedVertices?` (0x8221E0F0) never fires, even in the 3D scene. SetIndices only appears as null resets (section 1b).

## 3. Gameplay frame

Statistics are in section 2c. Where the draws come from (per frame, gameplay; caller LRs mapped to their containing `.pdata` function):

| Draw function | Calls/frame | Main callers |
|---|---|---|
| `0x8221E0F0` DrawIndexedVertices | 975.5 | `0x8221BCC8` 822, `0x82B21448` 135, `0x8221EBC0` 9, `0x821DDF70` 9 |
| `0x8221C9C8` (unhooked) | ~211 (proxy) | `0x8221FD60` (only static caller) |
| `0x8221C518` DrawVertices | 110.0 | `0x82219870` 43, `0x8222EA68` 37, `0x8222C788` 22, `0x821CA7E8` 6 |
| `0x82207C30` (unhooked) | ~53-82 (proxy) | `0x82A82F10` |
| `0x82217EE8` (unhooked) | ~51-90 (proxy) | `0x821D71E8`, `0x8220A528`, `0x8222E168`, `0x82A9B3F8` |
| `0x822060A0` (hooked as `BeginVertices?`) | 15.8 | `0x821EBDA0`, `0x82242C18` |
| `0x82B9C068` (unhooked) | unknown | `0x82BA0220` (XDK internal, see 6b) |

"Proxy" counts are the hooked `SetRenderState` / `SetPending_AluConstants?` calls made from inside that function, which track its call rate but are not exact. Sum of hooked draws plus proxies: about 1430 per frame (mean), against 2911 emulated draws.

**Emulated draws are about twice the guest draw calls.** Per frame, `gpu.draws / (hooked + proxy draws)` has median 2.07 (p10 1.83, p90 2.37), and the excess sits almost entirely on pitches 1120 and 1040. Likely cause: Xenos predicated tiling. The main scene does not fit in 10 MB of EDRAM, so the XDK records it once and replays it per screen tile. Evidence (static, section 6b):
- `0x822A6318` builds `SET_BIN_SELECT_LO/HI` and `SET_BIN_MASK` headers and is called from the XDK Swap `0x82B9CD68`.
- `0x82B9ED28` calls `0x82B9E848` (builds `SET_BIN_SELECT_LO`) and calls `0x82286248` (the only `INDIRECT_BUFFER` builder) twice, which is consistent with a two-tile replay loop.
- Six of the seven `DRAW_INDX` functions also build a `SET_BIN_MASK_LO` header (draws are predicated per tile).

The command processor executes a predicated packet whenever `bin_select & bin_mask` is non-zero (`command_processor.cpp`, `ExecutePacketType3`), so each tile replay reaches `IssueDraw` again. Unconfirmed at runtime: a per-frame counter of `SET_BIN_SELECT` packets and predicated draws is needed (Pending, item 2).

Consequence for sub-project 3: on the PC there is no EDRAM limit, so a native pass can draw the tiled scene once instead of once per tile. That roughly halves the emulated main-scene draw work in gameplay.

### 3b. Tiling confirmed (capture `d3d_census_20261001_145058`)

Second gameplay capture, 899 frames, with every PM4 draw emitter hooked and the SDK tiling counters on. The player stood still in a dense area, so per-frame counts are nearly constant (draws 6970 or 6973).

| Quantity (per frame) | Value |
|---|---|
| Emulated draws (`IssueDraw`, excluding resolves) | 6970 |
| Guest draw calls (all hooked emitters) | 3174: DrawIndexedVertices 2483, `0x8221C9C8` 328, DrawVertices 200, `0x82217EE8` 59, `0x82207C30` 50, `0x821EF988` 30, BeginVertices 18, `0x82B98770` 3, `0x82B988C8` 3 |
| Emulated / guest draws | 2.20 |
| Predicated draw packets executed | 6536 |
| Non-predicated draws (6970 - 6536) | 434 |
| Predicated packets skipped by the bin check | 12826 |
| SET_BIN_SELECT writes / distinct values | 11 / 6 (`0xFFFFFFFF`, `0x80000003`, `0x80000001`, `0x2`, `0xC`, `0x8`, identical every frame) |
| `TileReplay?` `0x82B9ED28` / `SetBinSelect` `0x82B9E848` calls | 3 / 9 |
| Indirect-buffer inserts (`0x82286248`) | 39 |
| Resolves | 34 |

`0x82B9C068`, `0x82B928A8`, `0x82BA29B8`, `0x82BA5448` and `0x82BA5CE8` were hooked but never called in this capture.

Reading:
- Every guest draw is now accounted for. About 434 draws per frame run once (not predicated), and about 2740 run under predication; those execute 6536 times, about 2.4 times each. The replay function runs 3 times per frame with 3 bin selects per run, which fits three screen tiles where each draw runs once for every tile its bounds touch.
- Pitch 1120 carries 5497 of the 6970 emulated draws, so the tiled pass is the main scene.
- A native main-scene pass that draws each guest call once would issue about 2740 draws instead of 6536, removing about 3800 of the 6970 emulated draws per frame (55%).

Extents per pitch (max window-scissor corner / max viewport with scaling enabled):

| Pitch | Viewport | Reading |
|---|---|---|
| 1280 | 1280 x 720 | final composite at output resolution |
| 1120 | 1120 x 720 | main scene: 1120 x 720, stretched horizontally to 1280 in the composite |
| 1040 | 1024 x 1024 | shadow map 1024 x 1024 (pitch padded) |
| 560 | 560 x 360 | half-resolution scene (1120 x 720 / 2) |
| 320 | 280 x 256 | quarter-resolution scene (280 x 180) and a 256 x 256 target share the padded pitch |
| 280 / 160 / 80 | 256 / 128 / 64 square | likely luminance downsample chain (HDR adaptation) |

## 4. Pass list

Pitch ablation, 2026-10-01, in the world (Bowerstone Old Town, night, snow), one run per pitch with `--native_render_suppress_debug=true --native_render_suppress_pitches=<p>`, vsync on. Draw counts are gameplay medians from section 2c.

| Pitch | Draws | Observed effect when suppressed | Pass |
|---|---|---|---|
| 1120 | 1779 | No world at all; the last loading-screen image stays on screen in horizontal bands with the live HUD over it; game keeps running (audio) | Main 3D scene (tiled). The 1280 composite samples it, so a stale scene texture stays visible |
| 1040 | 340 | World, characters and HUD fine; shadows wrong | Shadow maps |
| 320 | 139 | Base scene fine; garbage (uninitialised-memory patterns, cyan smears) in rectangular regions and horizontal bands where glow/fog blends; one green checkered character | Quarter-resolution post buffer (1280/4): glow / fog / blur |
| 1280 | 133 | Black screen, game keeps running (audio) | Final composite: post, UI, scan-out resolve |
| 560 | 114 | Washed-out, overexposed image with a ghost of a stale frame | Half-resolution scene downsample (1120/2) for bloom / HDR glow |
| 280 | 112 | Looks almost normal; a purple outline glow on one NPC and a semi-transparent crate near the camera (not yet compared against a baseline) | Quarter-resolution step (1120/4) of the bloom chain or a small glow/highlight mask; minor |

Reading:
- The scene chain 1120 -> 560 -> 280 halves cleanly, so 1120 is most likely the real width of the 3D scene (a sub-720p world, scaled up in the 1280 composite), not only a tile surface pitch. Confirm from the 1120 render-target height when the tiling counter lands.
- Horizontal banding in the 1120 and 320 runs fits a horizontally split tiled scene.
- Native replacement order follows: 1120 (world) first, 1040 (shadows) next; keep 1280 emulated (`native_render_keep_pitches=1280`) so UI and post still work, and leave the post chain (560/320/280) emulated until sub-project 7.

F3 in the world (vsync on; each run had one pitch suppressed): guest work 5.8-6.5 ms per frame, GPU-emulation busy 13.9-21.2 ms. The GPU emulation does about 3x the guest's work, so uncapped gameplay is GPU-emulation-bound (roughly 50-70 FPS ceiling), which is the case a native world pass addresses.

## 5. CPU vs GPU emulation

What the capture supports (3D segment, frames 482-600; the static screen is similar but irrelevant):

| Quantity | Median | Mean |
|---|---|---|
| guest frame time (`guest_ms`) | 33.32 ms | 33.32 ms |
| swap interval (`swap_interval_ms`) | 33.25 ms | 33.34 ms |
| emulated IssueDraw CP time (`draw_cpu_ms`) | 0.308 ms (max 5.27 ms) | 0.436 ms |
| resolve CP time (`copy_cpu_ms`) | 0.077 ms | 0.079 ms |

Scope of the timing counters: `draw_cpu_ms` is command-processor CPU time inside `IssueDraw` only. It excludes PM4 packet parsing, the present path and GPU execution time, so it is not a measure of total command-processor or GPU cost. The data therefore supports only this claim: the menu and early 3D scene are vsync-locked (guest frame time tracks the 33.3 ms swap interval at both median and mean, a 30 Hz lock) and not emulation-bound there. It does not say how expensive gameplay emulation is: the 3D scene has 281 draws per frame, gameplay will have more, and `draw_cpu_ms` scales with draws (frame 481 spiked to 19.5 ms).

**Gameplay (section 2c).** Median `guest_ms` 33.31 ms tracks the 33.26 ms swap interval, so gameplay is still held at the 30 Hz lock most of the time. But 261 of 899 frames (29%) exceed 34 ms (p90 35.83 ms, max 38.99 ms), so gameplay misses the lock where the menu did not. IssueDraw plus resolve CP time is about 8 ms per frame (median 6.65 + 1.27 ms), roughly 25x the menu's and well under the 33 ms budget, but it is only part of the CP's work. The census does not carry the full CP busy/wait/idle split; the F3 overlay does (GPU emu line and Verdict). Read F3 in the world with `--vsync=false --guest_vblank_pacing=false` to attribute the over-budget frames (Pending, item 3).

Pending gameplay rule (narrowed): compare `guest_ms` to `swap_interval_ms` in gameplay. If `guest_ms` stays at the swap interval, the frame is vsync-locked; if `guest_ms` is well above the swap interval, the guest or emulation cannot keep up, but the census cannot tell which. A full command-processor busy-time counter (PM4 parse plus draw plus present plus GPU) is a sub-project 3 prerequisite before claiming emulation-bound or GPU-bound.

## 6. Engine submission sites

Top callers of the hooked functions, mapped to the containing `.pdata` function (largest start <= LR):

| Hooked function | Top LR | Calls | Containing function | Notes |
|---|---|---|---|---|
| DrawVertices? 0x8221C518 | 0x821CB18C | 4401 | 0x821CA7E8 (626 instrs) | only observed draw site; the same function binds stream 0 (SetStreamSource, LR `0x821CB120`) before the draw and unbinds it (LR `0x821CB1A8`) after |
| SetStreamSource 0x821B6DA0 | 0x821CB1A8, 0x821CB120 | 4401 each | 0x821CA7E8 | one bind and one unbind per draw |
| SetStreamSource | 0x821B6F44/F28/F0C/EF0 | 385 each | 0x821B6EC0 (43 instrs) | state-reset function, null binds |
| SetIndices 0x8219CCD8 | 0x821B6F50 | 385 | 0x821B6EC0 | the only SetIndices site in the capture |
| SetRenderState 0x8221C908 | 0x8221C60C | 4398 | 0x8221C518 | inside DrawVertices? |
| BeginVertices? 0x822060A0 | 0x821EBFD4 | 1342 | 0x821EBDA0 (154 instrs) | not a draw confirmation |
| SetPixelShader? 0x822324E0 | 0x8222C2FC, 0x822324C8 | 2528, 2056 | 0x8222C268, 0x82232468 | |
| SetVertexShader? 0x82208CE8 | 0x82208CDC, 0x82220A00 | 3928, 2061 | 0x82208C48, 0x822209A0 | |

`0x821CA7E8` is the first engine submission function to capture for sub-project 3 (observed draw path).

### 6b. PM4 emitters (static scan, gameplay-relevant)

From `python tools\xdk_sigmatch\pm4_emitters.py out\xdk\fable2`. Six of the seven `DRAW_INDX` functions (all but `0x82B9C068`) also build a `SET_BIN_MASK_LO` header (predicated per tile).

| Packet | Function | Instrs | Hooked as | Static callers |
|---|---|---|---|---|
| DRAW_INDX | 0x8221E0F0 | 276 | DrawIndexedVertices? | 8 (`0x8221BCC8`, `0x82B21448`, ...) |
| DRAW_INDX | 0x8221C518 | 252 | DrawVertices? | 8 (`0x82219870`, `0x8222EA68`, `0x821CA7E8`, ...) |
| DRAW_INDX | 0x822060A0 | 294 | BeginVertices? | 5 (`0x821EBDA0`, `0x82242C18`, ...) |
| DRAW_INDX | 0x8221C9C8 | 259 | no | 1 (`0x8221FD60`) |
| DRAW_INDX | 0x82217EE8 | 327 | no | 4 (`0x821D71E8`, `0x8220A528`, `0x8222E168`, `0x82A9B3F8`) |
| DRAW_INDX | 0x82207C30 | 330 | no | 1 function (`0x82A82F10`, two sites) |
| DRAW_INDX | 0x82B9C068 | 96 | no | 1 (`0x82BA0220`, XDK internal; also calls `0x822A6318` and `0x82B928A8`) |
| DRAW_INDX_2 | 0x821EF988 | 188 | no | `0x821EF6F8` (3 sites) |
| DRAW_INDX_2 | 0x82B928A8 | 102 | no | `0x82BA0220` |
| DRAW_INDX_2 | 0x82B98770 | 85 | no | `0x821968B8`, `0x821969E0` |
| DRAW_INDX_2 | 0x82B988C8 | 226 | no | `0x821969E0` |
| DRAW_INDX_2 | 0x82BA29B8 | 580 | no | `0x82BA32C8` |
| DRAW_INDX_2 | 0x82BA5448 | 552 | no | `0x82BA6250` |
| DRAW_INDX_2 | 0x82BA5CE8 | 345 | no | `0x82BA6820` |
| INDIRECT_BUFFER | 0x82286248 | 121 | no | `0x82286430`, `0x82B9E610`, `0x82B9ED28` (2 sites) |
| SET_BIN_SELECT_LO/HI | 0x822A6318 | 61 | no | `0x82B9CD68` (XDK Swap), `0x82BA0220`, `0x822A5F80`, `0x82193120`, `0x821C64E8`, `0x8227F0B0` |
| SET_BIN_SELECT_LO | 0x82B9E848 | 56 | no | `0x82B9ED28` (likely the tile replay loop) |

Limits: the scan only sees headers built with `lis 0xC0xx` + `ori/addi` within 16 instructions. A header loaded from a table or a precompiled command buffer in a data section is not listed; the data sections hold one `SET_BIN_MASK`, one `SET_BIN_SELECT` and seven `DRAW_INDX_2`-shaped words, which have not been inspected. Note that `0x82286430`, which calls the `INDIRECT_BUFFER` builder, is the structurally named `SetRenderState.callee1.callee1.callee2` row in section 1; that name is unverified.

## 7. Recommendation for sub-project 3

Based on the gameplay capture (sections 2c, 3, 5) and the static emitter scan (section 6b). Pitch assignments stay provisional until the ablation (Pending, item 1).

1. **Draw coverage: hook the 14 emitters, not more correlation.** The static scan gives the complete list of functions that build draw packets (seven `DRAW_INDX`, seven `DRAW_INDX_2`). Three are hooked today. Add census hooks for `0x8221C9C8`, `0x82217EE8`, `0x82207C30`, `0x82B9C068` and the seven `DRAW_INDX_2` builders, and re-capture: hooked guest draws times the tile count should then account for `gpu.draws`. The census already holds the strong symbols sub-project 3 needs; plan a shared dispatch so the census becomes an observer called from the capture hooks rather than a second set of overrides on the same functions.
2. **Indexed draw to capture first:** `D3DDevice_DrawIndexedVertices` at `0x8221E0F0` (975 calls per frame, about two thirds of guest draws), reached mainly from `0x8221BCC8` (822/frame). `0x8221C518` (DrawVertices, 110/frame) is second.
3. **Tiling is the main performance lever.** If the tiling counter (Pending, item 2) confirms two tiles, the emulator issues the main scene twice per frame. A native pass that draws the tiled scene once (no EDRAM limit on PC) removes that duplication. Sub-project 3 must therefore capture at the guest draw call (once per call), not at `IssueDraw` (once per tile), and suppress the emulated draws for every tile of the replaced pass.
4. **Pitches:** gameplay's main scene draws to pitches 1120 (1779 draws/frame) and 1040 (340); 1280 (133), 320 (139), 560 (114) and 280 (112) are secondary. Do not reuse the menu's pitch-1280 conclusion. Name the passes by ablation before choosing `native_render_suppress_pitches` / `native_render_keep_pitches`.
5. **Performance case:** gameplay misses the 30 Hz lock in 29% of frames (section 5). Whether the guest CPU or the GPU emulation is responsible comes from the F3 overlay (Pending, item 3), not from the census.

## 8. Guest object layouts

Discovery D2 + D3 (2026-10-02). Constants live in `src/native/capture/xdk_layout.h`, each with its evidence. Static evidence is `python tools\xdk_sigmatch\sigmatch.py disasm out\xdk\fable2 <addr> <n>`; runtime evidence is two captures cross-checked with `tools\xdk_sigmatch\check_discovery.py` against `[vbind]` lines from `--native_render_log_vertex_bindings=true`:

| Capture | Kind | Settings | check_discovery |
|---|---|---|---|
| `native_discovery_20261002_101117` + `fable_2_076.log` | menu (no input) | 60 frames, delay 25 s, every 8th draw | 197 draw rows, checked 197, 197 matched |
| `native_discovery_20261002_101258` + `fable_2_077.log` | gameplay (`tools\drive_game.ps1` autoplay) | 40 frames, delay 55 s, every 8th draw | 9945 draw rows, checked 9841, 9841 matched (104 rows have no position element: shader `0x19A01C01290E20A7` has no vertex fetch in `[vbind]` either) |

The XDK converts a CPU virtual address to a GPU physical one inline at every fetch-constant and `IM_LOAD` site (e.g. `0x821B6DD8..0x821B6DF4`): `(va & 0x1FFFFFFF) + (((va >> 20) + 0x200) & 0x1000)` (the 4 KB-page view at `0xE0000000` is offset by 0x1000). Object dwords that hold addresses hold the CPU virtual address; `xdk::GpuAddress` applies the conversion.

### Hook register mappings (corrected)

| Hook | Mapping | Evidence |
|---|---|---|
| `D3DDevice_SetStreamSource` `0x821B6DA0` | r4 stream index, r5 vertex-buffer object, r6 byte offset, r7 stride in bytes, r8 dirty mask OR'ed into device+0x18 | `0x821B6DAC..0x821B6E0C`, `0x821B6E90` (`rlwinm r9,r26,30,24,31`: stride >> 2) |
| `D3DDevice_SetIndices` `0x8219CCD8` | r4 index-buffer object (stored at device+0x3094) | `0x8219CCE8`, `0x8219CD5C`; gameplay: the hook value equals the device field in every sampled indexed draw |
| `D3DDevice_SetPixelShader?` `0x822324E0` | **the real SetVertexShader**: r4 stored at device+0x3198 | `0x82232580`; the shader flush `0x8221B140` loads +0x3198 (`0x8221B184`) and emits it with `IM_LOAD` type 0 (vertex, `0x8221B7C0`) |
| `D3DDevice_SetVertexShader?` `0x82208CE8` | **the real SetPixelShader**: r4 stored at device+0x3194 | `0x82208D6C`; emitted with `IM_LOAD` type 1 (`0x8221B354 ori r11,r11,1`) |
| `D3DDevice_DrawIndexedVertices` `0x8221E0F0` | r4 primitive type, r5 base vertex, r6 start index, r7 index count | Task 9 (index buffer rows: `(start + count) * 2 <= size` in 9335/9335 gameplay draws); caller `0x8221BCC8` builds r6/r7 from the primitive group (`0x8221BDD4..0x8221BE18`) |
| `D3DDevice_DrawVertices` `0x8221C518` | r4 primitive type, r5 start vertex, r6 vertex count | Task 11, gameplay discovery `native_discovery_20261002_104938`: start + count <= vertices of the position stream in 204/204 draw rows with a vertex buffer; prim 4 rows advance r5 by r6 between consecutive calls (r5 864, r6 48, next r7 912); prim 1 (point list) rows have r5 = 0, r6 = vertices. Census `d3d_census_20261002_104938` args agree (r4 1, r5 0, r6 0x10/0x14) |
| `DrawIndx:8221C9C8` (Task 11b) | tessellated quad patches, auto-indexed: r5 first patch (written to VGT_INDX_OFFSET), r6 patch count (`num_indices`). r4 = 0x12 (`D3DTPT_QUADPATCH`) is not read: the draw initiator is built as the constant `0x192` (prim 0x12 `kQuadPatch`, source select 2 auto-index, explicit major mode). No index buffer, no streams: the terrain shaders fetch no vertices (section 9, "Tessellated terrain") | `0x8221CA28`/`0x8221CA30` (r5 -> r24, r6 -> r25; r4 is overwritten at `0x8221CA50` before any use); `0x8221CC80..0x8221CC90` type-0 write of register 0x2102 (VGT_INDX_OFFSET) = r24; `0x8221CCD0..0x8221CCEC` DRAW_INDX `0xC0012201` with initiator `0x192 \| r25 << 16` (chunks of 0xFFFE, `0x8221CC8C..0x8221CCA0`); `0x8221C9D8..0x8221C9FC` sets VGT_OUTPUT_PATH_CNTL (shadow +0x2974) to 1 (tessellation). Single caller `0x8221FD60` draws sub-patches 0-3 of a tile, one each: `0x8222057C..0x8222058C` (r4 0x12, r5 = loop index, r6 1). Runtime (temporary SDK `[vtess]` register-file log, `fable_2_108.log`, against capture `native_discovery_20261002_130248`): all 1275 sampled rows match a GPU draw with prim 18, `num_indices` 1 = r6, VGT_INDX_OFFSET = r5 and the same constants and texture fetch constants; for the 475 rows whose tile draws a strict subset of its four sub-patches, r5 is in the GPU's set in 475/475 |
| `DrawIndx:82207C30` (Task 11b, adaptive only) | tessellated quad patches when VGT_HOS_CNTL (device shadow +0x2978) is 2 (adaptive): r7 patches, numbered from 0 (VGT_INDX_OFFSET 0); the bound index buffer (+0x3094) at r6 holds r7 * 4 tessellation factors (32-bit). Other tessellation modes (DMA patch indices) are not mapped | `0x82207C78..0x82207C84` (mode test), `0x82207F6C..0x82207F80` (VGT_INDX_OFFSET = 0), `0x82207FD0..0x82207FE8` (adaptive branch: `mullw r8,r27,r21` with r21 = 4 from `0x82207CC0`, so `num_indices` = 4 * r7, initiator `0x912`; index address = IB dword 6 + r6 * 4, converted at `0x82208030..0x82208044`). Runtime: 290/290 sampled rows match a GPU draw with VGT_HOS_CNTL 2, `num_indices` = 4 * r7 and the same constants (control `num_indices` = r7: 0/290) |
| `DrawIndx:82217EE8`, `BeginVertices?`, the seven `DrawIndx2` builders | not confirmed | The register meaning of r4-r8 was not traced; draw records give them `kUnsupportedPrim` (section 9). Together 17 main-scene draws per frame in gameplay |

Task 8's empty fields had two causes. `ib.obj` was 0 because the menu capture held no `DrawIndexedVertices` at all (its draws are `DrawIndx:82217EE8`, `DrawIndx2:821EF988`, `BeginVertices` and 224 `DrawVertices`); `SetIndices` r4 was right. Stream `obj_dwords` were null outside `DrawVertices` for the same reason: the engine's own emitters do not bind streams through `SetStreamSource`. The real mistake was the shader mapping above. Draw rows now read the bindings from the device fields at draw time (covers inlined binds); the hook view is kept under `"hook"` in raw rows and agrees with the device fields in every sampled D3D draw.

### Layout table

| Constant | Value | Evidence |
|---|---|---|
| `kDeviceVertexFetchOffset` | 0x480 | SetStreamSource stores fc dword 0 at `(0xEF - i) * 8` (`0x821B6DFC`) and dword 1 at `0x6F4 + (0x11 - i) * 8` (`0x821B6E00`): slot `95 - i` at +0x480 + 8*slot |
| `kStreamFetchSlotBase` | 95 | same; stream 0 draws use `[vbind] fetch=95` |
| `kDeviceStreamObjectOffset` | 0x30AC | `0x821B6E10`/`0x821B6E8C` (`addi r11,r29,0xC2B; slwi; stwx r30`) |
| `kDeviceStreamStrideOffset` | 0x30F0 | `0x821B6E98` (`stb r9,0x30F0(r11)`, one byte per stream, dwords). 0 in gameplay indexed draws; the decoded shader stride is used instead |
| `kDeviceIndexBufferOffset` | 0x3094 | `0x8219CD5C` store, `0x8221E330` load in DrawIndexedVertices |
| `kDeviceVsConstantsOffset` | 0x780 | DrawIndexedVertices `0x8221E130` / DrawVertices `0x8221C554` pass r6 = device+0x780, r5 = 0x4000 to SetPending_AluConstants |
| `kVbFetchDword` | 6 | `0x821B6DC4 lwz r10,0x18(r30)`, `0x821B6DCC lwz r5,0x1C(r30)`: fc0 = GpuAddress(dword 6 + offset), fc1 = dword 7 - offset. Dword 6 is a CPU virtual address with type 3 in bits 0-1. `[vbind] fc=0x1A63C4E3 0x10012422` from object `0x401288C8` (dword 6 `0xFA63AD03`, dword 7 `0x10012C02`, offset 0x7E0). `fc_match` true in all 9841 gameplay + 197 menu draw rows |
| `kIbAddressDword` | 6 | `0x8221E39C lwz r10,0x18(r24)` + start * 2, GpuAddress (`0x8221E3AC..0x8221E3E4`). Gameplay: aligned and < 0x20000000 in 9335/9335; indices read there stay inside the position stream in 9318 (6589 reach exactly the last vertex); the other 17 take their first fetch per instance from stream 1 (5 elements) |
| `kIbSizeDword` | 7 (bytes) | gameplay: (start + count) * 2 <= dword 7 in 9335/9335, equal in 6598 (object `0x4062A148`: 28 bytes, start 0, count 14) |
| `kIbFormatDword` / `kIbFormatMask` | 0 / 0x80000000 | `0x8221E3A8 lwz r6,0(r24)`, `0x8221E3D8 rlwinm r6,r6,0,0,0`: bit 31 selects 32-bit indices (start * 4, `0x8221E3F4`). Disassembly only: every sampled index buffer is Common `0x20400002` (16-bit) |
| `kIbEndianShift` | 29 | `0x8221E3D0 rlwinm r11,r6,1,0,1`: Common bits 29-30 become the DRAW_INDX endian field. Sampled value 1 (8in16); 0xFFFF reset indices appear in 8325 sampled strips |
| `kVsSource` / `kVsDeviceFieldOffset` | `kDeviceField` / 0x3198 | real SetVertexShader store `0x82232580`, flush load `0x8221B184`. All 610 gameplay and 197 menu `DrawVertices` shaders hash to a `[vbind] vs=` |
| `kVsFallbackSource` / `kVsFallbackHookAddress` | `kHookR4` / 0x82221978 | gameplay indexed draws run with device+0x3198 = 0 (9335/9335); the engine loads shaders with `GpuLoadShaders` `0x82221978` (r3 device, r4 vertex shader, r5 pixel shader), which emits the vertex microcode with `IM_LOAD` straight from the object record (`0x82221AA4..0x82221B1C`). The last r4 on the drawing thread hashes to the draw's `[vbind] vs=` in 9335/9335 |
| `kVsUcodeBaseDword` | 8 | `0x8221B7CC` / `0x82221AE4 lwz r8,0x20(r31)` |
| `kVsHeaderOffset`, `kVsRecordOffsetField` | 0x368, 0x18 | `0x8221B4BC addi r27,r31,0x368`; record = header + obj[0x380 + 8*variant] (`0x8221B7B0..0x8221B7D4`, `0x82221A88..0x82221AB8`) |
| `kVsVariantFlagMask` | 0x20 | `0x8221B20C rlwinm. r11,r11,0,26,26` then obj[0x388] (`0x8221B218`); `GpuLoadShaders` uses it only with no pixel shader (`0x822219C8`, `0x82221A10`). Only variant 0 was seen at runtime |
| `kVsUcodeAddressDword` | 0 (of the record) | `0x8221B7E0 lwz r11,0x368(r11)` + base: microcode = GpuAddress(obj dword 8 + record dword 0) |
| `kVsUcodeSizeDword` / `kVsUcodeSizeShift` | 1 / 0 (bytes) | `0x8221B80C lwz r11,0x36C(r11); srwi r11,r11,2` into the `IM_LOAD` size field. Gameplay: XXH3_64bits over those bytes equals `[vbind] vs=` with `[vbind] dwords` = size / 4 for 9751 draws (9335 `gpu_load`, 416 `object`) |
| `kDeviceCommandWriteOffset`, `kImLoadImmediateHeader` | 0x30, 0xC0002B00 | patched-copy path below |
| `kDevicePsConstantsOffset` | 0x1780 | DrawIndexedVertices `0x8221E148`/`0x8221E14C` and DrawVertices `0x8221C56C`/`0x8221C570` pass r6 = device+0x1780, r5 = 0x4400 to SetPending_AluConstants (`0x8221DF98`): the pixel bank follows the vertex bank (0x780 + 256 * 16). Gameplay: r6 = device+0x1780 in all 15792 raw rows (`ps_bank_ptr`) |
| `kDeviceBoolConstantsOffset` | 0x2780 | DrawIndexedVertices `0x8221E314..0x8221E324` (`addi r6,r31,0x2780; li r5,0x4900; lis r4,-0x100; bl 0x8221C908`): the register-run writer takes a 40-bit mask here, so registers `0x4900..0x4927` (8 dwords of bool constants, then the loop constants) are shadowed from +0x2780, right after the pixel bank; the other five draw builders make the same call (`0x8221C738`, `0x82206304`, `0x82207F38`, `0x82218164`, `0x8221CC4C`). Runtime (section 12, "The `cexec b0` block"): bit 0 of the first dword, vertex `b0`, is set in 32 of the 19,256 draw rows of `native_discovery_20261006_110021`, all of two shaders with a `cexec b0` block. The loop constants read 0 in every row, also for shaders with a loop; that part is not established |
| `kPsDeviceFieldOffset` | 0x3194 | real SetPixelShader store `0x82208D6C`, flush load `0x8221B18C`. Used only when device+0x3198 (vertex) is set; see "Pixel shader microcode" |
| `kPsHeaderOffset`, `kPsRecordOffsetField` | 0x28, 0x18 | record = obj + obj[0x40] + 0x28 (`0x8221B32C lwz r11,0x40(r29)`, `0x8221B334 add`, `0x8221B338 lwz r11,0x28(r11)`; `0x82221A24..0x82221A30` in GpuLoadShaders); header = obj+0x28 also at `0x8221B2F8`/`0x82221BEC` (`addi r4,rN,0x28`) and `0x82208D90`. One record, no variants |
| `kPsUcodeBaseDword` | 6 | `0x8221B330 lwz r10,0x18(r29)`, `0x82221A28 lwz r10,0x18(r30)` |
| `kPsUcodeAddressDword` / `kPsUcodeSizeDword` / `kPsUcodeSizeShift` | 0 / 1 / 0 (bytes) | microcode = GpuAddress(obj dword 6 + record dword 0) (`0x8221B33C..0x8221B354`, `ori r11,r11,1` = `IM_LOAD` type 1); size `0x8221B364 lwz r11,0x2C(r11); srwi r11,r11,2` (`0x82221A5C`/`0x82221A60`). Same record shape as the vertex shader. Gameplay: the hash names a dump for every nonzero `ps_hash` (see below) |

### Vertex shader microcode: three sources

The object's microcode is a template whose vertex fetches the XDK rewrites for the bound declaration and strides (`0x821D3318`). A draw row's `vs.source` records which bytes the GPU ran:

- `gpu_load` (gameplay `DrawIndexedVertices`): the template, loaded by `GpuLoadShaders`; hashes match `[vbind]` directly.
- `object` (`DrawVertices` with the device field set, patched in place): the object's microcode; hashes match.
- `immediate` (menu, and whenever device byte +0x2ABC has bit 0x80, `0x8221B4F0..0x8221B504`): `0x821DFDE0` (r3 device, r5 object, r10 variant) copies the template into the command buffer as `IM_LOAD_IMMEDIATE` (header `0xC0002B00 | (dwords + 1) << 16`, then 0, then the dword count; `0x821DFE34..0x821DFE6C`) and patches the copy; on return device+0x30 (`0x821DFF30`) points at the copy's last dword. The copy differs from the template in 14 dwords (menu, all 197) or 14/30 (gameplay), and its hash is the `[vbind]` one (197/197 menu, 194/194 gameplay).

### Pixel shader microcode

Discovery D1 of sub-project 4 (2026-10-02, textures Task 7). Constants `kPs*` and `kDevicePsConstantsOffset` in `xdk_layout.h`; reader `ReadPsUcode`, choice `ChoosePs`, per-object cache `LookupPs` (hash + `TextureFetchSlots`), pixel bank `ReadPsBankRegisters` in `capture.cpp`.

**Static evidence.**

| What | Instructions |
|---|---|
| The flush emits the pixel shader with `IM_LOAD` type 1 | `0x8221B18C lwz r29,0x3194(r30)`; only with dirty bit 0x00100000 (`0x8221B2AC rlwinm r8,r20,0,11,11`, set by SetPixelShader `0x82208D78 oris r11,r11,0x10`); `0x8221B32C..0x8221B36C`: address = GpuAddress(obj[0x18] + record dword 0) \| 1, size = record dword 1 >> 2 |
| GpuLoadShaders `0x82221978` emits r5 the same way | `0x82221A24..0x82221A64`, before the vertex shader (`0x82221AA4..0x82221B1C`). It never stores device+0x3194 |
| Microcode loaded unpatched | the only other uses of the header upload the shader's literal constants from the table at header dword 5: `0x8222BFF8` (called by the flush at `0x8221B304`) and, in GpuLoadShaders, `0x82221CB0` (called at `0x82221BF4`; it emits `LOAD_ALU_CONSTANT` at `0x82221D30..0x82221D4C`); nothing rewrites the pixel microcode |
| The flush is skipped without a device vertex shader | `0x8221B190 cmplwi cr6,r31,0` (r31 = device+0x3198), `0x8221B194 beq 0x8221B9C4`: no `IM_LOAD` of either shader, so the GPU keeps what GpuLoadShaders loaded |
| No pixel shader = depth only | GpuLoadShaders writes RB_MODECONTROL with `SET_CONSTANT 0x00040208` (`0x82221B34..0x82221B3C`): 5 (depth) without r5 (`0x822219C0`), 4 (color + depth) with it (`0x82221A18`). The flush puts the same 5 / 4 in the low bits of device+0x2954 (`0x8221B228..0x8221B248` without, `0x8221B370..0x8221B3A4` with a pixel shader) |

So the pixel shader a draw runs is: device+0x3198 null (vertex shader from GpuLoadShaders) -> the last GpuLoadShaders r5 on the drawing thread; device+0x3198 set -> device+0x3194. Zero means depth only: `ps_hash` 0, no shader. The brief's first hypothesis (device field first, else the GpuLoadShaders r5) is wrong: in gameplay device+0x3194 is set but stale in about 2700 of every 9300 sampled draws.

**Runtime evidence.** Gameplay, `.\tools\drive_game.ps1 -Total 120 -GameArgs "--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump" -Env @{FABLE2_NATIVE_DISCOVERY="120"; FABLE2_NATIVE_DISCOVERY_DELAY="55"; FABLE2_NATIVE_DISCOVERY_EVERY="16"}`, capture **`native_discovery_20261002_192837`** (120 frames), dumps in `out\shader_dump` (111 `shader_<HASH>.ucode.frag`). Draw and tess rows carry `"ps":{"obj","source","dev_obj","gpu_obj","phys","bytes","tex_slots",["alt_hash"]}` and `"ps_hash"`; raw rows carry `ps_bank_ptr`.

| Rows | Total | In scene | Nonzero `ps_hash` | In scene with nonzero `ps_hash` | Distinct PS hashes | With a `.ucode.frag` dump |
|---|---|---|---|---|---|---|
| `draw` (DrawVertices, DrawIndexedVertices) | 9327 | 2890 | 5211 | 2890 (100%) | 22 | 22 of 22 (5211 of 5211 rows) |
| `tess` (DrawIndx 8221C9C8 / 82207C30) | 4676 | 4253 | 4676 | 4253 (100%) | 8 | 8 of 8 |

The hash is XXH3_64bits over the record's microcode bytes as stored (big-endian guest bytes), the emulator's `ucode_data_hash`. The 4116 zero rows are all outside the scene and all depth only (`ps.obj` 0: 3089 with device+0x3198 set and device+0x3194 null, 1027 GpuLoadShaders with r5 null).

Which candidate the GPU ran: dumps cannot tell (both candidates are real shaders with dumps), so the rows' (vertex hash, pixel hash) pairs were checked against the emulator's pipeline storage (`cache\shaders\shareable\4D5307F1.rtv.d3d12.xpso`: a 12-byte header, then 72-byte records of hash + `PipelineDescription`, `vertex_shader_hash` at +8 and `pixel_shader_hash` at +24; 181 distinct pairs). With the rule above, 8421 of 9327 draw rows and 4676 of 4676 tess rows form a stored pair. That includes all 2890 + 4253 in-scene rows and all 2708 draws where the device field and the GpuLoadShaders r5 differ under a GpuLoadShaders vertex shader. Under the brief's rule (first capture `native_discovery_20261002_192055`) the 2722 such rows paired with the GpuLoadShaders shader and never with the device field. The other 906 rows (out of scene, immediate vertex shaders, pixel shader `0xA4A965C189287B99`) pair only with pixel hash 0 in storage. The emulator drops a pixel shader that writes no unmasked color target (`IsPixelShaderNeededWithRasterization`), so these are color-masked passes and do not discriminate.

`ps_bank_ptr - device` = 0x1780 in all 15792 raw rows that saw a `0x4400` upload.

### Limits

- Draw rows cover `D3DDevice_DrawVertices` and `D3DDevice_DrawIndexedVertices` only; `"kind":"tess"` rows (Task 11b) cover `DrawIndx:8221C9C8` and `DrawIndx:82207C30`. The menu's 3D scene is drawn by the engine emitter `DrawIndx:82217EE8` (1260 of 1470 sampled menu draws in `native_discovery_20261002_094059`), which binds neither streams nor shaders through the D3D calls; how many `DrawVertices` a menu capture sees depends on timing (0 or 197 in two runs with the same settings).
- `SelectPosition(..., -1, ...)` takes the shader's first full vertex fetch. For the 17 instanced gameplay draws above that is the per-instance stream, not the per-vertex position. Draw rows that read past the selected stream (indexed: max index + base vertex >= vertices; non-indexed: start + count > vertices) are written with `"pos_suspect": true` and no `positions`; `matrix_finder.py` skips them and `check_discovery.py` also requires `vb.fc_match` (13 such rows in gameplay capture `native_discovery_20261002_102615`).
- 32-bit indices and shader variant 1 were not sampled; those two fields rest on the disassembly.

### Main-scene bracket (D1)

Discovery D1 (2026-10-02, Task 10). Method 1 of the brief (device tiling flag) gives the bracket, so no new hook is needed. Constants: `kDeviceTilingFlagOffset` = 0x2ABC (a byte), `kDeviceTilingFlagMask` = 0x20 in `xdk_layout.h`. The pure state machine is in `src/native/capture/main_scene.h` (`ObserveDraw`, `EndFrame`; test `tests/native/test_main_scene.cpp`). `capture.cpp` reads the byte at every hooked guest draw (`OnXdkReturn`; the draw functions only read it). It calls `FrameBuilder::Open()` when the bit turns on and `Close()` when it turns off, and closes any bracket still open at Swap.

**Static evidence.** All addresses below are on the device in `r3`/`r31`.

| What | Instructions |
|---|---|
| The `SET_BIN_MASK_LO` guard in DrawIndexedVertices | `0x8221E41C lbz r11,0x2ABC(r31)`; `0x8221E420 clrlwi. r11,r11,31`; `0x8221E424 bne 0x8221E44C`. Bit 0x01 clear: plain `DRAW_INDX` (`0xC0032201`, `0x8221E428..0x8221E448`). Bit 0x01 set: `SET_BIN_MASK_LO 0xFFFFFFFF`, `DRAW_INDX`, `SET_BIN_MASK_LO 0x80000000`, plus a per-draw record at device+0x33C0 (`0x8221E44C..0x8221E50C`) |
| The same test in the other five builders | `0x8221C7B4` (DrawVertices), `0x822063EC` (BeginVertices), `0x82208048` (`0x82207C30`), `0x822182BC` (`0x82217EE8`), `0x8221CCA8` (`0x8221C9C8`) |
| BeginTiling `0x822A5F80` sets bit 0x20 | `0x822A61C0 lbz r9,0x2ABC(r31)`; `0x822A61D0 ori r9,r9,0x20`; `0x822A61FC stb r9,0x2ABC(r31)`. Bit 0x01 is set only when bits 0x08/0x04 and device byte +0x2F9B are clear (`0x822A6204..0x822A626C`). Only static caller: `0x821A19B0` in engine function `0x821A17A8` |
| EndTiling `0x8227F0B0` clears bit 0x20 | `0x8227F418 lbz`; `0x8227F41C andi. r11,r11,0xDF`; `0x8227F43C stb`. Bit 0x01 is then recomputed; with 0x20 gone, it stays set only if bit 0x10 is set. Static callers: `0x821A2ED0`, `0x82B69EFC`, `0x830EF588` |
| `0x822655B0`, SetPredication-like, clears bit 0x01 inside the pass | Called with r4 != 0 (an explicit bin mask): `0x822656C0 rlwinm r11,r11,0,0,30`. Called with r4 = 0: restores bit 0x01 only if bit 0x10 is set, or if bit 0x20 is set and the bound surfaces still match the ones captured at BeginTiling (`0x822655F4..0x82265684`) |

So bit 0x01 (the literal `SET_BIN_MASK_LO` guard) means "per-draw bin masks", which is a subset of the pass. Bit 0x20 is the BeginTiling/EndTiling bracket itself, and it is the one used.

**Runtime evidence.** Frame rows are written while discovery is armed: `{"kind":"frame","frame":F,"captured":N,"in_bracket":M,"main_scene":{"opens","closes","ib_in","ib_out","flag_unread","flag_bytes":{byte:draws},"pitch_in":{pitch:draws},"pitch_out":{...}}}`. Here `captured` is the number of hooked guest draw calls in the frame. The pitch comes from the device's RB_SURFACE_INFO shadow at +0x2880 (`kDeviceSurfaceInfoOffset`: `0x8221E200..0x8221E210` passes device+0x2880 for register 0x2000 to the register-run writer `0x8221C908`). That pitch is used only as evidence.

| Run | Captures | in_bracket per frame | opens / closes per frame | Bracket pitches | Pitches outside the bracket | Census (same run) |
|---|---|---|---|---|---|---|
| Menu, no input, discovery 60 frames + census 60, delay 25 s | `native_discovery_20261002_104818`, `d3d_census_20261002_104818` | 2 in all 60 frames (captured 87-88) | 1 / 1 in all 60 | 1120: 120 (2 per frame) | 1280: 3660, 320: 900, 560: 540 | draws 303, pitch 1280 181, pitch 1120 0, pred_draws 90 (/2.4 = 37.5) |
| Gameplay, `tools\drive_game.ps1 -Total 150`, discovery 120 + census 120, delay 50 s | `native_discovery_20261002_104938`, `d3d_census_20261002_104938` | 1460-1477, median 1477 (captured 2410-2427) | 1 / 1 in all 120 | 1120: 176407 (all of them) | 1040: 85560, 320: 10680, 280: 8640, 1280: 4680, 560: 2640, 640: 1440, 80: 240, 160: 120 (no 1120) | draws 5217, pitch 1120 3927, pred_draws 4783 (/2.4 = 1993) |

Flag bytes seen in gameplay (all frames): 0x00 114000, 0x61 132600, 0x20 30600, 0x21 10927, 0x60 2280. So 274 of the 1477 bracket draws per frame have bit 0x01 clear and would be missed by the bit-0x01 guard. The menu shows only 0x00 and 0x60.

Reading:
- Gameplay: the bracket is exactly the pitch-1120 main scene. Every bracket draw is on pitch 1120 and no draw outside it is. The census has 3927 emulated draws on pitch 1120, which is 2.66 executions per bracket draw (at most 3 tiles).
- The `pred_draws / 2.4` estimate does not hold. `pred_draws` counts every executed predicated draw packet. Every hooked `DRAW_INDX` header has the predicate bit set (`0xC0032201`), so the draws outside the bracket also count, once each (bin select `0xFFFFFFFF`). The relation that holds is pred_draws ≈ (captured - in_bracket) + k * in_bracket with 1 <= k <= 3:
  - Gameplay: 4783 - 950 = 3833, so k = 2.60.
  - Menu: 90 - 85 = 5, so k = 2.5.
- Gameplay in_bracket is 0.74 of `pred_draws / 2.4`, outside the brief's 20%. The 3b figure of about 2740 tiled guest draws used the same estimate in a denser area (6970 emulated draws, against 5217 here).
- The menu's tiled pass is also pitch 1120, but it holds only 2 hooked guest draws per frame, and the census shows no emulated draw on pitch 1120. Pitch 1280, the menu's 3D scene, is drawn outside BeginTiling: 61-62 hooked draws per frame with flag byte 0x00. So "pitch 1280 is the menu's tiled target" does not hold. The 1280 and 560 emulated counts (181 and 79) exceed the hooked draws there (62 and 9) because the menu also runs command buffers through `IndirectBuffer:82286248` (13-15 inserts per frame outside the bracket, 0-3 inside), not because of tiling.

## 9. Transforms (D4) and draw records

Discovery D4 (2026-10-02, Task 11). Every capture below is gameplay, driven by `tools\drive_game.ps1` (autoplay), the player standing in Bowerstone after the save loads.

### Position element: fetch swizzle and endian

The first gameplay matrix-finder pass (on `native_discovery_20261002_104938`) left the two shaders behind 80% of the sampled draws without a window. The decoded positions had a fourth component of 0, 4 or 8. The shader dumps (`fable_2.exe --dump_shaders=<dir>`, SDK disassembly) show why. A typical position fetch is `vfetch_full r5.yxw1, r0.x, vf0, DataFormat=FMT_16_16_16_16_FLOAT` (shader `0xECD66A10092E6562`, instr 5), and every sampled stream has fetch-constant endian 8in32 (`fc[1] & 3 == 2` in all 3715 rows of that capture). Under 8in32 the GPU sees each dword's 16-bit halves swapped: its source components are `(m1, m0, m3, m2)` of the big-endian memory order `m`. The destination swizzle `yxw1` restores `(m0, m1, m2)` and forces `w = 1`; the memory `w` holds other data.

- `PosLayout` gains `swizzle` (the fetch's destination swizzle: 0-3 source, 4 = 0, 5 = 1, 7 = not written, read as 0 for x/y/z and 1 for w) and `swap16` (pairs swapped). `SelectPosition` copies the swizzle, and `ApplyFetchEndian(layout, fc1 & 3)` sets `swap16`. It accepts 16-bit components under 8in16/8in32 and 32-bit components under 8in32 only. `DecodePositions` applies both, so Task 12 decodes what the shader sees. Tests: `test_position_decode.cpp` (cases 22-34), `test_vfetch_decode.cpp` (13-14).
- Discovery draw rows now carry `pos.swizzle`, `pos.swap16`, `pos.endian_ok` and `in_scene` (the draw is inside the main-scene bracket). Their `positions` are the draw's own first vertices (up to 64): first indices plus base vertex for indexed draws, start vertex onward for `DrawVertices`. Before, they were the first 64 vertices of the stream, which often belong to another mesh in a shared buffer.

### Matrix finder results

| Capture | Settings | Frames / draw rows | Notes |
|---|---|---|---|
| `native_discovery_20261002_111154` | 150 frames, delay 50 s, every 16th draw | 52 / 6544 | run ended first (discovery wrote ~1 frame/s with the old reads, see "Read cost"); last, truncated row dropped |
| `native_discovery_20261002_111917` | 120 frames, delay 55 s, every 16th, `--dump_shaders` | 85 / 10636 | first capture with `in_scene`; last row dropped |
| `native_discovery_20261002_115507` | 60 frames, delay 55 s, every 16th | 60 / 7542 | after the read fix (60 frames in 2.3 s) |

`matrix_finder.py` (single window) on 111154 + 111917 resolves 15 of the 21 sampled shaders and on 115507 resolves 15 of 22, so `--products` was not needed by the half-resolved rule. The single-window results are not usable as they stand:

- The two dominant shaders are rejected. Those are `0xECD66A10092E6562` (79% of main-scene samples) and `0xF160B4DA459A6D40` (mostly outside the bracket: 3535 of 3569 samples in 111917). The finder requires every sample to have at least half of its vertices inside the clip volume. In gameplay many draws are large meshes that are only partly on screen. With `c0..c3` as rows, 45% of the ECD66A10 main-scene samples pass that test, but 95% put every vertex in front of the camera with depth in [0, 1].
- Windows the finder does pick are often spurious. A window whose fourth row has a large translation (for example `c1..c4`, where `c4` = (-0.53, -0.85, 0, 126)) maps every vertex near the screen centre and scores 1.0. The picks also change between captures: `0x5F4416192E87005F` came out as window 0 combine, then 14 dot; `0x432563420047C96C` as 0, then 1; `0xFBD39C64463E180B` as 1 dot, then 70 combine. `0xBEAD84BD72072E0E` came out as window 2, but its dump uses `c0..c3`.

**Static evidence.** 21 of the 22 sampled vertex shaders end in `dp4 oPos.{x,y,z,w}, c0..c3, rN` (or `dp4` into a temporary followed by `max oPos, rT, rT`). The 22nd, `0x695413A9831D88DA`, is a memexport particle pass (`mad eA, ...`, instr 319) with a dummy `oPos`. Fable 2's world-view-projection is always `c0..c3`, dot layout. The shaders differ only in what `rN` is:

- the fetched position itself ("exact");
- a bone blend of it (skinned: 3x4 bone rows fetched from `vf3` by blend index, weighted, then `dp4`);
- a procedural displacement (`sin`/`frc`, foliage);
- a computed position (particles, permuted components, relative-addressed constants).

**GPU check (`0xECD66A10092E6562`).** A temporary SDK diagnostic (not committed) logged the GPU register file `c0..c3` at `IssueDraw` together with the fetch constant for every 50th draw of that shader (`fable_2_089.log`, 20000 lines). In discovery capture `native_discovery_20261002_112734`, 1191 of the 1308 main-scene draw rows whose vertex buffer appears in the GPU sample have device-bank `c0..c3` exactly equal to a GPU `c0..c3` for the same fetch constant (max difference < 1e-3). The other 117 draws use buffers shared by instances whose matrices were not in the 1-in-50 GPU sample. So the bank read at device+0x780 is what the GPU uses. `SetPending_AluConstants` (`0x8221DF98`) uploads from it under a 64-bit dirty mask, one bit per four registers, most significant bit first (`cntlzd`, `0x8221DFC8..0x8221DFF0`).

**Table policy (`docs/native-renderer/vs-transforms.json`, all entries `"manual": true` so the finder never overwrites them).** Each entry records the evidence and both finder runs.

- Accepted, base 0, dot, `pos_fetch` -1:
  - exact shaders, on the disassembly alone: `0xECD66A10092E6562` (also the GPU check), `0xF160B4DA459A6D40`, `0x65834A8405D40E53`, `0x57818A7FD1C4F026`, `0xBEAD84BD72072E0E`, `0x1E6798C9D0F65784`.
  - derived positions (skinned or displaced), where at least 90% of samples keep half their vertices inside the clip volume with `c0..c3`: `0x5F4416192E87005F` (0.938), `0x3A0F9098B839DDBC`, `0x82F6433A69263C75`, `0x9ED0BA440DBD51D4`, `0x432563420047C96C`, `0xFBD39C64463E180B` (1.0 each). These entries carry `"deformed": true`: skinned meshes are drawn in their bind pose and foliage without wind, which sub-project 3 accepts. `gen_transform_table.py` emits the flag as the fifth `FABLE2_VS_TRANSFORM` argument; it reaches `TransformInfo::deformed` and `DrawRecord::deformed`, and the coverage line counts deformed drawable records separately.
- Added in Task 11b (section "Task 11b" below): `0x79EAC49585797037` (position swizzle), `0xA1F7E9885EC466DF` (rigid skin), the terrain shaders `0xC30A97D946FA2BE4`, `0xFB68A7F2301210E1` and `0x5003700B7C9B1C16`. Their Task 11 rejections are kept as `"task11_rejected"`.
- Character bodies, `0xD4D558DA6A82BDC8` (four-bone skinning, hero and NPCs): accepted as base 0, dot, `deformed`, `pos_swizzle` `yxw1`, bind pose (character fix). The fetch's own swizzle 0x4C1 takes w from the bone-index dword (0, 4 or 8), which exploded the clay mesh into a screen-wide fan; the override `yxw1` (0xAC1, the same override `0x79EA...` and `0xA1F7...` carry) gives w = 1. The Task 11 rejection (0.534) came from early captures that read the wrong vertices. The microcode has `oPos = dp4(c0..c3, ...)` at instructions 71-74. The base-0 check on `native_discovery_20261002_155634.jsonl` scores 1.000 on 480 samples, but that is weak evidence: discovery rows decode positions without the table's `pos_swizzle`, and a bind-pose mesh sits near its model origin, so most matrices that place the object on screen pass the half-inside test. The confirmation is visual: with the entry in place the bodies line up with the emulated image in split and overlay views (user check, section 10). Before this, all 16 body draws per frame were skipped as no-transform, so only the eyes and sword (`0x79EA...`) and glow-pass characters were drawn. Weighted four-bone skinning is a later sub-project.
- Rejected (`"rejected"` gives the reason): `0x87D4404FB36AF71D` (position from three fetches, 0.0), `0x695413A9831D88DA` (memexport particle pass, point lists), `0x475EC9F795E5EDBB` (0.719), `0xA5846836C90E1192` (0.773), `0x29B6506FBACEB93A` (0.600), `0x775C6085FBB9D676` (1 sample), `0x563E3BE17857DB59` (computed index and relative constants, 0.429).

`gen_transform_table.py` writes 18 `FABLE2_VS_TRANSFORM` entries to `src/native/capture/vs_transform_table.inc`, plus `FABLE2_VS_POS_SWIZZLE`, `FABLE2_VS_SKIN` and `FABLE2_VS_TERRAIN` lines for the entries that have them (test `tests/test_gen_transform_table.py`).

### Task 11b: tessellated terrain, position swizzle, rigid skin

Evidence for the three additions comes from one gameplay discovery run (`tools\drive_game.ps1 -Total 110 -GameArgs "--native_render_log_vertex_bindings=true" -Env @{FABLE2_NATIVE_DISCOVERY="40"; FABLE2_NATIVE_DISCOVERY_DELAY="55"; FABLE2_NATIVE_DISCOVERY_EVERY="8"}`, capture `native_discovery_20261002_130248`, log `fable_2_108.log`). Three temporary diagnostics were used and then reverted (not committed):

- an SDK `[vtess]` line at `IssueDraw` (beside `[vbind]`): the draw initiator, VGT_INDX_OFFSET, VGT_OUTPUT_PATH_CNTL, VGT_HOS_CNTL, the tessellation levels, `c0..c3`, `c8`, `c11`, `c46`, `c47`, `c72`, `c113..c115`, `c252..c255` from the GPU register file, texture fetch constants 16-19 and the shader's vertex fetch constants (bool constants 0-31 were added for a second run, `fable_2_109.log`). It logged every 8th tessellated draw and every 16th draw of `0x79EA...` / `0xA1F7...`;
- in discovery, a one-time dump of the textures behind tf16-19;
- in discovery, raw vertex bytes and the other streams' first 2 KB in `0xA1F7...` draw rows.

The `"kind":"tess"` discovery row (shader, the constants above, tf16-19 from the device shadow) is committed.

**Tessellated terrain (`DrawIndx:8221C9C8`, 255 main-scene draws per frame; adaptive `DrawIndx:82207C30`, 31).** The builders draw tessellated quad patches (section 8). Their shaders, `0xC30A97D946FA2BE4` / `0xFB68A7F2301210E1` (8221C9C8, continuous tessellation, VGT_HOS_CNTL 1, maximum level 15) and `0x5003700B7C9B1C16` (82207C30, adaptive), have no vertex fetch at all. The SDK passes the patch index in `r0.x` and the domain point in `r0.yz` (xenia `kQuadDomainPatchIndexed`). The shader places the point on a grid and lifts it by a height read from tf16 (instruction numbers in `vs-transforms.json`):

- `row = floor(p * c11.y)`, `col = p - row * c11.x` (gameplay: 2 x 2 sub-patches per tile for 8221C9C8, 4 or 5 columns for 82207C30, where instr 5 adds `c113.x` to `p`);
- `world.xy = (col + u, row + v) * c46.xy + c113.xy` (cell 8 x 8 world units);
- `h = tfetch2D(tf16, (world.xy - c47.zw) * c47.xy).x * c46.z` (scale 1/288, height scale 88.98);
- `oPos = dp4(c0..c3 .zxyw, cndeq(c254.xxxy, (h, world.x, world.y), c254.yyyy))`. The GPU register file holds `c254 = (0, 1, 3, 2)` (`c255` for `0x5003...`; the device bank holds zeros there, these are shader literals), so `oPos = dp4(c0..c3, (world.x, world.y, h, 1))`.

The heightmap is tf16 = `84C04802 1BD0C058 00480240 01001400 00000000 00000218`: tiled, 577 x 577, pitch 608, `k_16` unsigned normalized, endian 8in16, clamp to edge, point magnification filter. Untiled with xenia's `GetTiledOffset2D` and read big-endian, the dumped texture is a smooth heightmap of Bowerstone (mean neighbour step 0.0016, against 0.004 / 0.076 read linearly). The capture's constants and tf16-19 (device shadow at +0x480 + 24 * t) equal the GPU's in all 1275 sampled `8221C9C8` rows, with VGT_INDX_OFFSET = r5. The 290 `82207C30` rows all match a GPU draw with `num_indices` = 4 * r7.

Rebuilding a 16 x 16 grid per patch from the dumped heightmap and projecting it with the row's `c0..c3` puts 83% of the points inside the clip volume, with 1215 of the 1275 patches having a visible point. With x and y swapped the figure is 34%; with zero height it is 15%. For the `82207C30` main-scene draws it is 80%; the control without the `c113.x` patch offset gives 79%, so that offset rests on the disassembly alone.

Not modelled: the hole mask. When `tf18.w < c114.x` the shader writes `c255`/NaN as the position (instr 37-47), cutting holes in the ground. tf18 is a `k_DXT5A` atlas addressed through `c115`; the first decode attempt did not produce a recognisable image. Clay ground is therefore drawn where the game may cut holes. Next action: decode tf18 (DXT5A, tiled per 4 x 4 block) and drop the grid triangles whose corners are holes.

**Position swizzle (`0x79EAC49585797037`, about 95 draws per frame).** This shader is the static-mesh shader `0xECD66A10...` with a different register allocation:

- `vfetch_full r2.zxwy` (instr 5);
- `r6 = cndeq(c255.xxxy, r2.zwyy, c255.yyyy)` (instr 10), with GPU `c255 = (0, 1, 3, 2)`;
- `oPos = dp4(c0..c3 .zxyw, r6)`.

Together these are `dp4(c0..c3, (src.y, src.x, src.w, 1))`, the fetch read with swizzle `yxw1`. `vs-transforms.json` gives `"pos_swizzle": "yxw1"`. The capture applies it through `SelectPosition`'s swizzle override (test `test_vfetch_decode.cpp` 15-16).

Checks on 475 main-scene rows:

- GPU `c0..c3` equal the device bank in 474 of them;
- the remapped positions put 93.6% of vertices inside the clip volume, against 85.3% as fetched;
- every remapped vertex is in front of the camera with depth in [0, 1];
- bool constant 0 is 0 in all 21881 logged draws (`fable_2_109.log`).

**Rigid skin (`0xA1F7E9885EC466DF`, about 92 draws per frame).** Task 11 rejected this shader on the theory that the bones carry the object placement. They do not.

- Each vertex has one bone, in byte x of an integer 8_8_8_8 at dword 3 (fetch 2, read as `r5.z`). Under 8in32 that is the dword's last byte.
- The bone's three half4 rows are full fetches of slot 92 (stream 3), stride 24 bytes, indexed by that bone (fetches 6-8, instr 13-15).
- `world_k = dot(row_k, (p.xyz, 1))` (instr 22-25), then `oPos = dp4(c0..c3, (world, 1))` (instr 46-50).

Palettes hold 4-8 bones. Their rotation part deviates from identity by at most 0.05 at p99 (max 1.27, translation p99 0.18), consistent with sway and small animation. The device bank `c0..c3` equals the GPU's in 460/460 main-scene rows. 310 of the 460 rows are entirely in front of the camera and 150 entirely behind; `0xECD66A10...` also has 100 fully-behind rows of 4480, so drawing objects behind the camera is normal here. Bool constant 0, which selects a uv-space position at instr 45, is 0 in all 21176 logged draws (`fable_2_109.log`).

`"skin": {"index_fetch": 2, "index_component": "z", "row_fetches": [6, 7, 8]}` plus `"pos_swizzle": "yxw1"` give the exact skinned position (`rigid_skin.h`, test `test_rigid_skin.cpp`). A vertex whose bone lies past its palette becomes NaN and is culled.

**Renderer cost.** Animated palettes (about 10 skinned streams per frame) re-decode every frame. Creating a new upload buffer for each one took the clay pass's decode time from 0.3 ms to 4.5 ms per frame and gameplay from 30 to 26.9 fps. Replaced buffers now go to a `RetirePool` (32 MB, `geometry_cache_index.h`, test `test_geometry_cache_index.cpp`). A buffer is reused for the same size once its retiring submission has completed. Decode is back to 0.2-0.4 ms.

### Draw records

`capture.cpp` builds one `DrawRecord` per outermost hooked draw inside the main-scene bracket while `fable2_native_render` is true and a consumer wants records: `PollFrame` sets the flag each guest frame to "composite view (overlay, split, native) and failure latch clear" (`RecordsWanted`, `native_render_state.h`), and the capture latches it at each Swap, so records start and stop at frame boundaries. Without a consumer the bracket is still observed and main-scene draws are only counted (`FrameBuilder::CountUnrecorded`), so the coverage line still reports `captured`; such frames publish no scene, and the clay pass shows the emulated frame until the first frame with records. Immediate shader loads made without a consumer are not hashed; a draw that still runs such a load when records resume gets no shader (`kUnknownShader`) until the shader is reloaded. At each Swap, `capture::Publisher()` publishes `FrameBuilder::Finish(frame)`. Inputs:

- **Shader.** The device field, or the `GpuLoadShaders` fallback, or the immediate copy (section 8). A per-thread cache is keyed by shader object; it is refreshed when the microcode address, size or variant changes, when object dword 10 (the patched-declaration id) changes, or (immediate path) when the copy's hash changes. A steady-state draw does five small reads and no hashing.
- **Position.** `SelectPosition` with the table's `pos_fetch` and `pos_swizzle`, then `ApplyFetchEndian`.
- **Rigid skin** (table `"skin"`). `SelectSkin` takes the bone-index element and the three bone-row fetches from the shader's fetch list. At draw time the bone palette is the stream behind the rows' fetch slot (needs `fc_match`); the index endian comes from the position stream's fetch constant, the rows' from the palette's. Missing palette: `kNoStream`; an unhandled layout or endian: `kUnknownPosFormat`. The renderer transforms each vertex by its bone (`rigid_skin.h`) before the rows.
- **Terrain** (table `"terrain"`). `FillTerrainInputs` reads the spec's registers and the heightmap's texture fetch constant from the device shadow and builds a `TerrainPatch` (`terrain_patch.h`). A patch draw whose shader has no terrain entry stays `kUnsupportedPrim`; an unhandled heightmap is `kUnknownPosFormat`. The renderer builds the grid (`kTerrainGrid` = 16 quads per patch edge) from the heightmap, keyed by the map's bytes and every patch parameter.
- **Vertex and index buffers.** The vertex buffer comes from the position slot's stream object (dwords 6/7) plus the stream offset, and needs `fc_match`. The index buffer comes from object dwords 0/6/7. Records carry big-endian indices (`index_convert.h`), so a 16-bit buffer must be 8in16 and a 32-bit one 8in32; any other endian is `kBadIndex`.
- **Bank.** Only the transform's four registers are converted, into a per-thread 1024-float buffer.
- **Argument mapping (section 8).** `DrawIndexedVertices`, `DrawVertices`, `DrawIndx:8221C9C8` and adaptive `DrawIndx:82207C30` as mapped. All other draw hooks get prim 0, which becomes `kUnsupportedPrim`.

Capture-side skip reasons replace `AssembleRecord`'s reason, unless that reason is unsupported-prim (`ResolveSkip`, `CountSkip` and `kMaxDrawCount` in `draw_record.h`, tested in `test_frame_scene.cpp`). The capture stops filling inputs at the first problem, so `AssembleRecord` alone would report, for example, an oversized draw as unknown-shader. The capture-side reasons are:

- index or vertex count above 4,194,304: `kBadMemory`, before any index read;
- indices unreadable: `kBadMemory`;
- largest index plus base vertex at or past the position stream's vertex count, or a negative base vertex: `kBadIndex` (this covers the `pos_suspect` instanced draws);
- non-indexed start + count past the stream: `kBadIndex`.

**Nesting.** A per-thread `DrawNesting` (`draw_nesting.h`, test `test_draw_nesting.cpp`) is entered in `OnXdkCall` and left in `OnXdkReturn` for draw hooks. Only the outermost draw is recorded. Statically, `DrawIndx2:82BA5CE8` calls `0x821969E0`, which calls `DrawIndx2:82B988C8` and `DrawIndx2:82B98770`; the `DRAW_INDX` builders call no other draw hook. At runtime the coverage log's `nested_total` stayed 0 through menu, loading and gameplay, so no nesting occurred in these runs.

**Inactive path.** `OnSwap` returns after one relaxed atomic load when neither the renderer nor discovery is on.

### Read cost

`guest_read.h` checked every page with `VirtualQuery`. On the guest arena's file-mapped views that took about 0.47 ms per call (25k calls = 11.6 s in a timed run). Per-draw records then ran the game at 2.5 frames/s (census `guest_ms` 390-410 ms), and discovery wrote about one frame per second. Reads now check the guest heap page tables instead. `BaseHeap::QueryRegionInfo` reports committed and readable for a whole region, through `Memory::LookupHeap` for virtual addresses and `Memory::GetPhysicalHeap` for physical ones. Those two SDK symbols were added to `rexruntime.def`. A per-thread, per-frame `RegionReadCache` (`page_cache.h`, test `test_page_cache.cpp`) holds 64 regions and checks the most recently hit one first. Measured cost: records 1.2 ms per gameplay frame (1477 records); discovery 60 frames every 16th draw in 2.3 s.

**Capture timer.** The guest-thread capture work (hook returns, the discovery-only binding state, the swap) is timed with the TSC (calibrated against `steady_clock` at startup) and accumulated per guest frame into `FrameScene::capture_ms`. The `[native] capture:` line prints it every 300 frames with any view (`capture X ms (median, p90, max over N frames)`, plus `records on/off`), and F3 shows `Capture: X ms guest time per frame` in composite views. The per-hook argument copy in `OnXdkCall` (eight stores) is not timed. With `fable2_native_render` or discovery on, or `FABLE2_GUEST_WORK_LOG=1`, a `[frame] guest` line every 300 frames gives the fps and the medians of the raw per-frame frame, work (frame minus swap and limiter waits, as F3), swap and wait times.

**Gameplay A/B (2026-10-02, `.\tools\drive_game.ps1 -Total 120`, Bowerstone after loading the save, 30 fps lock).** Seven 300-frame windows per run from frame 1500 (about 50 s in):

| Run | Guest work, median per window (mean of 7) | Capture time per frame, median | fps |
|---|---|---|---|
| `--fable2_native_render=false` (`FABLE2_GUEST_WORK_LOG=1`, `fable_2_118.log`) | 5.36-5.43 ms (5.39) | - | 30.0 |
| `--fable2_native_render=true --fable2_native_view=off` (`fable_2_119.log`) | 5.48-5.57 ms (5.52) | 0.051 ms (p90 0.053-0.055, max 0.137) | 30.0 |
| `--fable2_native_render=true --fable2_native_view=split` (`fable_2_120.log`) | 6.87-7.47 ms (7.11) | 1.05-1.18 ms (max 2.71) | 30.0 |

With the view off the capture costs 0.05 ms by the timer and about 0.12 ms of guest work against the renderer disabled, under the 0.5 ms criterion. The remaining view-off cost is the bracket observation per guest draw (tiling flag read and the scene mutex, about 2400 draws per frame) and the untimed hook argument copies. With records (split) the timer reads about 1.1 ms (record building: device snapshot, shader lookup, index scan per draw) and guest work rises by about 1.7 ms; the difference is timer overhead, cache effects and untimed hook work.

### Coverage (gameplay)

Task 11 (`.\tools\drive_game.ps1 -Total 120 -GameArgs "--fable2_native_render=true"`, `fable_2_097.log`, `fable_2_099.log`, `fable_2_100.log`):

```
[native] capture: frame 1200 captured 1477 drawable 921 (deformed 5) skipped {no-transform: 229, unsupported-prim: 324, bad-index: 3} nested_total 0
[native] capture: frame 1200 unsupported by hook {D3DDevice_DrawVertices?: 21, D3DDevice_BeginVertices?: 3, DrawIndx:8221C9C8: 255, DrawIndx:82217EE8: 10, DrawIndx:82207C30: 31, DrawIndx2:821EF988: 4}
```

D / C = 921 / 1477 = 0.62.

Task 11b (`.\tools\drive_game.ps1 -Total 125 -Shots "75,95" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"`, `fable_2_112.log`, release build without diagnostics). Frames 1200 to 3600 all report:

```
[native] capture: frame 3300 captured 1477 drawable 1394 (deformed 5) skipped {no-transform: 42, unsupported-prim: 38, bad-index: 3} nested_total 0
[native] capture: frame 3300 unsupported by hook {D3DDevice_DrawVertices?: 21, D3DDevice_BeginVertices?: 3, DrawIndx:82217EE8: 10, DrawIndx2:821EF988: 4}
[native] clay: drawn 1394 (deformed 5) of 1394 drawable, skipped_bad_index 0 other 0 | 10 uploads, 2778 hits, 10.8 MB resident | hash 0.62 ms, decode 0.21 ms, record 0.26 ms (max total 1.55 ms over 300) | scene frame 3313
```

D / C = 1394 / 1477 = 0.944, above the 0.9 target. Frames 1200 to 3300 took 69.97 s, which is 30.0 fps. The split view's clay half now shows the street surface and snow drifts under the buildings and the character; the Task 12 clay dump had empty background there. The remaining skips:

| Reason | Draws per frame | Share of C | Cause | Next action |
|---|---|---|---|---|
| no-transform | 42 | 2.8% | Rejected shaders. `0xD4D558DA6A82BDC8` (16): four-bone blended skinning. Bone indices are `r2.xyzw` (integer 8_8_8_8 at dword 3), weights `r4` (8_8_8_8 at dword 4), and the rows come from slot 92 as in `0xA1F7...` (instr 9-35, `oPos` at 71-74). `0x29B6506FBACEB93A` (14, `DrawVertices` quad lists): the fetch index is computed from `r0.x` (instr 9) and the position is derived from two 32-bit fetches. `0x475EC9F795E5EDBB` (7, `max oPos, r2, r2` at instr 97) and `0xA5846836C90E1192` (2, `max oPos, r1, r1` at instr 147): positions computed by long ALU sequences. `0x563E3BE17857DB59` (2): computed index and relative constants. `0x775C6085FBB9D676` (1) | `0xD4D5...`: extend `RigidSkin` to weighted four-bone blends (same palette layout as `0xA1F7...`). The others need per-shader position derivation from their dumps; at 25 draws together they are below the target's margin |
| unsupported-prim | 38 | 2.6% | `DrawVertices` 21: shader `0x19A01C01290E20A7`, point lists with no vertex fetch. `DrawIndx:82217EE8` 10, `DrawIndx2:821EF988` 4, `BeginVertices` 3: argument mapping not traced | Trace `82217EE8` (the menu's main emitter, section 8 "Limits") the same way as `8221C9C8`. Point lists stay skipped (no geometry to draw as clay) |
| bad-index | 3 | 0.2% | `0x8123C16DBF583F92`: instanced draws whose first fetch is per-instance data (`pos_suspect`) | Instancing support, later |

Only Bowerstone was sampled here; section 10 covers other areas.

## 10. Validation (plan Task 14, 2026-10-02)

Build: `native-renderer` 3fc54d9 with SDK `renderer` 0c6d1de, release. The user played by hand from `out\build\win-amd64-release` with `--fable2_native_render=true --fable2_native_view=split` and used F6 to cycle views. Logs: `fable_2_133.log` (4 min), `fable_2_134.log` (5 min) and `fable_2_135.log` (25 min).

| Spec criterion | Result | Evidence |
|---|---|---|
| 1. Alignment | Pass | User: split, overlay and native views line up in town (Bowerstone), in an open field and in an interior. |
| 2. Coverage >= 0.9 | Pass on aggregate; single frames dip lower | Bowerstone: 1066 / 1117 = 0.954 (`fable_2_133.log`), 1940 / 2008 = 0.966 (F3, `fable_2_134.log`). `fable_2_135.log`: 34 logged windows with records on, aggregate 0.949, 3 below 0.9, minimum 289 / 350 = 0.826 (frame 40200). An F3 screenshot at the lakeside jetty (quest "The Birth of a Hero") read 749 / 985 = 0.76 with 128 bad-index draws. That frame fell between the 300-frame log samples, so the log shows at most 22 bad-index draws. |
| 3. Stability | Pass, with one unexplained event | `fable_2_135.log`: 17:09:47-17:34:54, in the world from 17:11:10, several loading boundaries (captured count drops to 0-36 at 17:15:00, 17:20:20, 17:28:51 and 17:33:57) and many F6 switches. No warning or error lines apart from the start-up `BaseHeap::AllocFixed` ones; every 300-frame window after loading ran at 28.7-30.0 fps. F3 showed a worst frame of 381.6 ms at a transition. |
| 4. Capture cost, view off < 0.5 ms | Pass | `fable_2_135.log`: median of the per-window capture medians with records off is 0.040 ms (115 windows). Final fix wave A/B (autoplay, 120 s per run): 0.051 ms median, guest work +0.12 ms against the renderer disabled. With a clay view on, records are built and capture costs 0.5-2.0 ms per frame. |
| 5. View off looks normal | Pass | User check. |
| Performance | Pass | 30 fps held with the renderer on in every run. |

**The unexplained event** (`fable_2_133.log`): at 16:58:50, about 35 s into the world, the world draw list froze. Every frame drew the same 1117 draws for 2 minutes, until the user closed the game, and guest work rose from about 6 ms to 38.8 ms per frame with no swap wait. That is the main menu's signature, so a menu or prompt over a frozen world is the likely explanation; the screenshot shows text starting "Pre" behind the F3 window. There were no errors and no capture warnings, and it did not recur in about 28 minutes of later play. If it recurs, take a screenshot with F3 hidden and compare against a run with `--fable2_native_render=false`.

**Skip reasons outside Bowerstone** (the per-shader and per-hook log lines of the sampled windows). Section 9 lists Bowerstone's.
- no-transform: `0x29B6506FBACEB93A` (up to 20 per frame), `0x7C5710DEF3EE33C4` (15, new), `0x475EC9F795E5EDBB` (13), `0x83569F66D81A3549` (6, new), `0xA5846836C90E1192` (5), `0x8123C16DBF583F92` (2), `0x2D40B53C926109BE` (1, new).
- bad-index: `0x8123C16DBF583F92`, instanced (up to 22 in the log, 128 in the F3 screenshot).
- unsupported-prim: `DrawVertices` 14-27, `BeginVertices` 2-16, `DrawIndx:82217EE8` 6-12, `DrawIndx2:821EF988` 4.
- Next action: run a gameplay discovery capture at the lake and add table entries for the new shaders; instancing for `0x8123...`.

**Known limitations:**
- Characters are drawn in their bind pose. Rigid attachments are drawn by `0x79EA...` with their own per-object `c0..c3`, which places them at the animated bone, so the eyes and the sword on the back sit apart from the body. Weighted four-bone skinning (`0xD4D5...`) is sub-project 5.
- Terrain hole masks are ignored.
- Shaders loaded while records are off are counted as unknown-shader after F6 until they are loaded again.
- A view change from the console no longer applies at runtime; use F6 or the start-up flags.

## 11. Albedo table (D3, D4)

Discovery D3/D4 of sub-project 4 (2026-10-02, textures Task 9). Per pixel shader, `docs/native-renderer/ps-albedo.json` names the albedo texture fetch slot and the UV path back to an interpolator (`src/native/capture/ps_albedo_table.inc`, `gen_albedo_table.py`); per vertex shader, the `"uv"` key of `vs-transforms.json` traces that interpolator back to a vertex fetch element (`FABLE2_VS_UV` rows in `vs_transform_table.inc`, `gen_transform_table.py`). Proposals come from `tools\xdk_sigmatch\albedo_finder.py`; every entry was confirmed by hand from a thumbnail (`texture_thumb.py`, colour content judged, the thumbnail ignores the fetch swizzle) and the shader disassembly, and carries `"manual": true` and an evidence string. A traced albedo fetch is followed into the shader body: in the lit object shaders (`8D900846...` and the same shape) tf0 is squared into the diffuse term and tf1 is scalar-squared as the specular colour, tf2 is a DXN normal map, tf8 a screen-sized `k_8` mask, tf4/tf5 1D lookups, tf14 a cube map. A VS `"uv"` entry carries its own `"uv_manual"` and `"uv_evidence"`; the VS entries' other fields are unchanged.

### Bowerstone (autoplay)

Capture: `.\tools\drive_game.ps1 -Total 180 -GameArgs "--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump" -Env @{FABLE2_NATIVE_DISCOVERY="300"; FABLE2_NATIVE_DISCOVERY_DELAY="55"; FABLE2_NATIVE_DISCOVERY_EVERY="8"}`, **`native_discovery_20261002_194918`** (300 frames, every 8th draw; 140 textures, 20 MB in `native_tex_20261002_194918`). Finder: `python tools\xdk_sigmatch\albedo_finder.py out\build\win-amd64-release\logs\native_discovery_20261002_194918.jsonl --dumps out\shader_dump --out out\albedo_proposals_bowerstone.json --thumbs out\albedo_thumbs_bowerstone`. 14391 in-scene `draw` rows (terrain is `tess`, excluded), 17 pixel shaders, all dumped.

| Pixel shader | Draws | Cumulative | Decision | Vertex shaders (draws, table state) |
|---|---|---|---|---|
| `0x8D900846800943C8` | 4500 | 31.3% | albedo tf0 | `ECD6` 3761 (uv), `D4D5` 639 (uv), `A1F7` 100 (uv) |
| `0xA7E45D07F5CF6627` | 2506 | 48.7% | albedo tf0 | `8123` 2472 (no entry), `FC4F` 34 (no entry) |
| `0x165EDCD2CB963868` | 1446 | 58.7% | albedo tf0 (finder said tf1) | `B636` 1164, `6AD4` 216, `33C0` 34, `36B5` 32 (no entries) |
| `0x00E09D1BC5295D52` | 1149 | 66.7% | albedo tf0 | `475E` 1149 (rejected) |
| `0x7CD57B81550F19E3` | 834 | 72.5% | no_albedo | `A584` 834 (rejected) |
| `0x014F8A02DB7B19CA` | 571 | 76.5% | no_albedo | `7C57` 571 (no entry) |
| `0xE99F4ACC7A3C78B4` | 549 | 80.3% | albedo tf0 | `8123` 485, `FC4F` 64 (no entries) |
| `0x401A01FD4E5F8757` | 454 | 83.4% | albedo tf0 | `D4D5` 379 (uv), `ECD6` 75 (uv) |
| `0xA17D8AEC3A817D45` | 450 | 86.6% | no_albedo | `BEAD` 450 (transform, no uv) |
| `0xF6D98C7B4D98438B` | 447 | 89.7% | no_albedo | `BEAD` 447 (transform, no uv) |
| `0x648B965C200A5113` | 405 | 92.5% | albedo tf0 | `ECD6` 332 (uv), `79EA` 73 (uv) |
| `0xF525E023E08AC9BB` | 389 | 95.2% | albedo tf0 | `B636` 260, `6AD4` 83, `33C0` 46 (no entries) |
| `0x789266C3E42D7E59` | 377 | 97.8% | albedo tf0 | `ECD6` 377 (uv) |
| `0x26052DEFBA688AF2` | 157 | 98.9% | albedo tf0 | `48D3` 157 (no entry) |
| `0x2FD24989AB04A475` | 75 | 99.4% | albedo tf0 | `79EA` 75 (uv) |
| `0xDBFD88A80EDBCE36` | 42 | 99.7% | albedo tf0 | `29B6` 42 (rejected) |
| `0xC300519EC8915346` | 40 | 100.0% | albedo tf0 (finder said tf1) | `775C` 35, `29B6` 5 (rejected) |

**Table state since sub-project 5 (added 2026-10-06; section 12).** The last column and the "Coverage" paragraph below are as measured on 2026-10-02. Every vertex shader marked "no entry" or "rejected" above now has a transform entry, except the billboards `29B6` and `775C`: `8123`, `B636`, `6AD4`, `48D3`, `FC4F`, `33C0` and `36B5` (instanced, with `"uv"`), `475E` and `7C57` (with `"uv"`), `A584` (no `"uv"`). So the draws of `0x7CD5...` and `0x014F...` are drawn, as flat clay counted `no-albedo`: 22 and 15 per frame in the bridge scene (`untextured by ps` lines of `fable_2_184.log`). `ps-albedo.json` said of both that their draws "are not drawn anyway" until this date. `0x014F...` is not water, as the next paragraph guesses: in the bridge capture its draws are the dog's fur shells (section 12, "Wind and displacement").

Thumbnails (tf0): building-trim and wood-plank atlases, a character-part atlas (faces, eyes, cloth), leaves, ferns, a grass/bark/stone atlas, a feather/fur atlas, an eye iris, a glow sprite and a flame sprite. `no_albedo`: `7CD5...` (only colour fetch tf0 untraceable, scalar co-issue `mulsc`), `014F...` (tf14/tf15 8_8_8_8 all-zero at first use, tf13 8_8 two-channel, likely water), `A17D...`/`F6D9...` (only traced fetch is a single-component `k_8` mask; their 8_8_8_8 fetches are predicated and all-zero at first use). All 8_8_8_8 256x256/128x128 textures of those shaders read as zero bytes when first dumped, so they look like render targets.

**Coverage.** Every in-scene draw has a table decision (17 of 17 shaders, 100%); 12089 of 14391 draws (84.0%) are on albedo shaders (80% was reached at `E99F...`). The draws drawn today are those whose vertex shader has a transform: 6708 (46.6%); of these, 5811 (86.6%) have an albedo entry and a VS `"uv"` entry, the other 897 are `BEAD...` draws with `no_albedo` shaders. The other 7683 draws are on vertex shaders without a transform (characters and instanced foliage `8123`/`B636`, `pos_suspect`; `475E`, `A584`, `7C57`, `48D3`, `29B6`, `775C`, `FC4F`, `6AD4`, `33C0`, `36B5`), so they are not drawn and got no `"uv"`.

**VS `"uv"` entries.** All four read `o0.xy` from one `FMT_16_16_FLOAT` element with the dest swizzle `yx__` (so `o0.x` = element `y`), no stages:

| Vertex shader | Fetch ordinal | Offset (dwords) | Trace | Albedo draws |
|---|---|---|---|---|
| `0xECD66A10092E6562` | 2 | 3 | `shader_trace.py` | 4545 |
| `0xD4D558DA6A82BDC8` | 4 | 5 | by hand (tool: `control flow: cexec`, since the final-review fix `control flow before export: cexec`); `o0.xy = max(r5.xy, r5.xy)` at instr 79 in an unconditional exec, `r5` written only by `vfetch_mini r5.yx__` (instr 13); the cexec at 5.0 writes `r4` | 1018 |
| `0x79EAC49585797037` | 2 | 3 | `shader_trace.py` | 148 |
| `0xA1F7E9885EC466DF` | 3 | 5 | by hand (tool: `control flow: cexec`, since the final-review fix `control flow before export: cexec`); `o0.xy` at instr 55, `r5.xy` written only by `vfetch_mini r5.yx__` (instr 10; ordinal 2 is `r5.__x_`, the skin index); the cexec at 3.1 writes `r4` | 100 |

Not added: `475E...` (rejected) and `7C57...` (no entry) had finder proposals; the finder's `BEAD...` proposal (`o3.xy`, 32_32_FLOAT) serves only `no_albedo` shaders.

**Format census (D4).** Formats of the albedo slot's fetch constant over the albedo shaders' draws (textures = distinct base address and size):

| Format | All albedo-shader draws: textures | Draws | Share | Drawn today (transform + uv): textures | Draws | Share |
|---|---|---|---|---|---|---|
| DXT1 (18) | 36 | 11479 | 95.0% | 26 | 5283 | 90.9% |
| DXT4_5 (20) | 6 | 463 | 3.8% | 3 | 381 | 6.6% |
| 8_8_8_8 (6) | 1 | 74 | 0.6% | 1 | 74 | 1.3% |
| DXT2_3 (19) | 1 | 73 | 0.6% | 1 | 73 | 1.3% |

No other format occurs in an albedo slot (the 8_8_8_8 one is a 16x16 linear texture in `8D90...` tf0), so no decoder is missing. UV element formats of the confirmed VS entries: `FMT_16_16_FLOAT` (31) only, 5811 draws.

### Bower Lake

Bower Lake: pending (user capture).

### Validation (plan Task 13)

Autoplay validation, 2026-10-02/03, release build, `tools\drive_game.ps1` 120 s runs (logs `fable_2_145` view off, `fable_2_146` split view with shot at 95 s, `fable_2_148` renderer off with `FABLE2_GUEST_WORK_LOG=1`; gameplay coverage and upload burst from Task 12). Note: the autoplay save now loads the outdoor bridge scene (quest "The Birth of a Hero"), not Bowerstone's streets, so the Bowerstone share below is measured in the bridge scene and the town, field and interior checks are left to the user.

| Success criterion (spec) | Autoplay measurement | User check |
|---|---|---|
| 1. Clay textures match the emulated image in split/overlay (town, open field, interior) | Split screenshot at 95 s (bridge scene): planks, rope, hero clothing and dog textured, correctly oriented, no tiling garbage; terrain is untextured clay by design | pending (user check): town, open field, interior alignment |
| 2. At least 80% of drawn non-terrain draws textured (Bowerstone, Bower Lake) | Bridge scene: 156-160 of ~182 drawn non-terrain draws textured (about 87%); Task 12 gameplay 160 of 184. Overall share 20-21% because terrain is 567 of ~749 drawn. Since the final-review fix the F3 and `[native] clay:` texture line also print this share: `Textures: textured 160 of 751 drawn (21%, 86% non-terrain)` (`fable_2_149`, split view; integer percent, 160/184 = 86.96% truncates to 86) | pending (user check): Bowerstone streets and Bower Lake |
| 3a. 30 fps with a clay view on | 30.0 guest fps in both view-off and split windows; work median 7.1 ms in split | none needed |
| 3b. View-off capture overhead under 0.5 ms | View-off capture median 0.044 ms (max 0.16 ms), about 11x margin. Guest work median: renderer off 5.92 ms (`fable_2_148`), view off ~6.0 ms (+0.08 ms), split ~7.1 ms (+1.1 ms, matching the 1.09 ms capture median) | none needed |
| 4a. Texture memory within budget | 46 textures resident, 12.3 MB (budget 512 MB); 0 uploads per frame steady, about 1650 cache hits per frame; upload burst at world load is spread by the per-frame budget and done in 0.84 s; no `[native] textures:` latch or "texture path off" line in logs fable_2_145/146 | none needed |
| 4b. 10-minute run with an area transition, no crash or latch | Only 120 s runs measured, no area transition | pending (user check) |

Other split-view costs (300-frame windows): texture decode ~0.4 ms, hash 0.3-0.4 ms, record build 0.12-0.16 ms, clay pass max total 1.2-1.3 ms (one 4.57 ms spike). Run 144/147 (renderer off, no `[frame] guest` lines) was not a stall: that line needs `FABLE2_GUEST_WORK_LOG=1` and the unflushed tail was lost when the run was stopped; `fable_2_148` is the valid baseline.

**Remaining untextured draws (bridge scene, split view), by reason**

| Reason | Draws | Why | Next action |
|---|---|---|---|
| `terrain` | 567 | Tessellated terrain stays clay by design (non-goal) | Terrain colour in a later sub-project (blended layers) |
| `no-albedo` | 24 | Two pixel shaders, `0xA17D8AEC3A817D45` x12 and `0xF6D98C7B4D98438B` x12, whose only traced fetch is a single-component `k_8` mask; the 8_8_8_8 fetches are predicated and zero at first use (possibly render targets; unverified) | Capture a frame where those textures are populated and re-run `albedo_finder.py`; otherwise leave as clay |

All other reasons (`ps-unknown`, `uv-unsupported`, `format-unsupported`, `texture-pending`, `texture-dynamic`, `texture-bad`) were not seen in the logged untextured lists of these windows (the lists show the top three reasons).

**Known limitations (non-goals in practice)**

- Terrain stays untextured clay.
- Alpha test and blending are ignored (foliage and sprite cut-outs draw opaque). This is a design consequence (the clay pass has no alpha path), not something checked in the screenshots.
- Gamma is ignored (albedo shown as stored, the shaders square it).
- Characters are drawn in bind pose (animation is sub-project 5).
- Mirror clamp addressing is approximated.
- Textures written by the GPU (render-target resolves into texture memory) may never change in CPU-visible guest memory, so they show stale or black contents instead of becoming `texture-dynamic` (the change check samples guest memory). The all-zero 8_8_8_8 fetches of `A17D...`/`F6D9...` fit this (unverified).
- Linear base levels use the SDK's row pitch (`GetGuestTextureLayout`: the fetch pitch aligned to 32 blocks, no 256-byte row padding; mips are 256-byte aligned) in the texture cache, the discovery dump extent and `texture_thumb.py`. An earlier ruling padded linear base rows to 256 bytes from "terrain evidence"; that was wrong (the terrain heightmap is tiled, its linear branch never ran). The only linear albedo textures in the Bowerstone capture (five 16x16 8_8_8_8, `8D90...` tf0) have pitch 64 texels, 256-byte rows under both rules, so nothing observed changes; the rules differ only for 8_8_8_8 pitches that are an odd multiple of 32 texels and DXT2_3/DXT4_5 pitches whose block count rounds up to an odd multiple of 16 (DXT1 rows agree always).

**Deviation.** Shader tracing is done in Python over the SDK's shader disassembly dumps (`tools/xdk_sigmatch/shader_trace.py`, `--dump_shaders`) instead of the C++ headers `tfetch_decode.h` and `uv_trace.h` listed in the spec; the runtime only needs the texture fetch slots a pixel shader uses (`TextureFetchSlots` in `vfetch_decode.h`).

## 12. Coverage and skinning

Sub-project 5 (spec `docs/superpowers/specs/2026-10-05-native-renderer-coverage-skinning-design.md`, 2026-10-05). Evidence capture for every entry of this section: **`native_discovery_20261005_103213`** (autoplay, bridge scene, 120 frames, every 4th draw, 11523 in-scene draw rows, 17 vertex shaders), with the vertex streams of its rows in `native_geo_20261005_103213`. Entries are replayed offline by `tools\xdk_sigmatch\position_check.py <capture> --json docs\native-renderer\vs-transforms.json [--entry <candidates.json>]`. A draw passes when at least half of its sampled vertices are inside the clip volume under its own `c0..c3`; a shader is accepted when its share of passing draws reaches the capture's baseline (the share of the trusted `0xECD66A10092E6562`) minus 0.10, floor 0.60. This capture: baseline 0.810, accept at 0.710 or more. Two rules judge what leaves that share and reject an entry whatever its share, with the reason printed after `REJECT` (final fix wave): a sampled row the runtime would skip (`unsupported` or `bad-index`) rejects the entry, and the draws judged must be at least 20 and at least a tenth of the sampled rows; rows whose stream dump is missing (`unreadable`) enter neither rule. No table entry is affected in this capture (no skipped row; the fewest judged draws are 35, of `0x36B5...`). The floor does bite in the two smaller evidence captures, on which no entry's recorded verdict rests: in `native_discovery_20261005_123635` (2,308 in-scene rows) `0xA1F7...` has 18 sampled draws and its line reads REJECT for that reason alone (ACCEPT before the rule, share 1.000); there `0x33C0...`, `0xFC4F...` (12 draws each) and `0x36B5...` (6), and in `native_discovery_20261006_110021` `0x36B5...` (16), carry the reason beside a share that already rejected them. A shader with fewer than 20 sampled rows in a capture cannot be accepted on that capture.

The captures this section rests on were written to the build directory's `logs` folder (`out\build\win-amd64-release\logs`), which a recreated build directory loses. They are also copied to `out\native-evidence\`: `native_discovery_20261005_103213.jsonl`, `native_discovery_20261005_123635.jsonl` and `native_discovery_20261006_110021.jsonl`, each with its `native_geo_<stamp>` folder (792 MB, git-ignored like all of `out`). A recreated build directory does not lose them; a deleted `out` does.

### Wind and displacement

| Vertex shader | Rows | Draws per frame | Pixel shader (albedo table) | Entry | In-clip share (200 draws) | In-clip verdict | Table |
|---|---|---|---|---|---|---|---|
| `0x7C5710DEF3EE33C4` | 452 | 15 | `0x014F8A02DB7B19CA` (no_albedo) | base 0, dot, `pos_fetch` 0, deformed | 1.000 | ACCEPT | entry, with `"uv"` |
| `0x475EC9F795E5EDBB` | 910 | 30 | `0x00E09D1BC5295D52` (albedo tf0) | base 0, dot, `pos_fetch` 0, `pos_swizzle` `yxw1`, deformed | 0.455 | REJECT | entry as a documented exception, with `"uv"` |
| `0xA5846836C90E1192` | 649 | 22 | `0x7CD57B81550F19E3` (no_albedo) | base 0, dot, `pos_fetch` 0, `pos_swizzle` `yxw1`, deformed | 0.530 | REJECT | entry as a documented exception, no `"uv"` |
| `0x2D40B53C926109BE` | 0 | - | - | none | - | not in the capture | rejected, disassembly only |

The two tree shaders are in the table although `position_check.py` prints REJECT for them, by a controller ruling (Task 5 fix round 1, 2026-10-05). The in-clip rule exists to catch a wrong matrix. For world-space meshes spread around the camera, as trees are, the in-clip share measures the scene, not the entry: it counts how many of the drawn parts happen to be on screen. Their matrix and their component order are established by the other checks below, and the entries' evidence strings say so. `position_check.py` keeps printing REJECT for both; that line is expected.

**Readings** (`out\shader_dump\shader_<HASH>.ucode.vert`, instruction numbers as printed). All four end in `dp4` with `c0..c3` into a temporary that `max oPos, rT, rT` copies out. Where the two operands of the `dp4` carry different swizzles (`c0.zxyw` against `r9.xzyw`), the product is `dot(c0, (r9.z, r9.y, r9.x, r9.w))`.

- **`0x7C57...` (fur shells).**
  - Position: `vfetch_full r2.xyz_` of vf0, `FMT_32_32_32_FLOAT`, stride 5 dwords (instr 5, fetch ordinal 0). `w` is not fetched and reads as 1; 32-bit components need no pair swap.
  - `r5.xyz = r2.xyz + r2.w * r1.yzw` (instr 16). `r1.yzw` is the 2_10_10_10 normal (instr 7, fetch 2). `r2.w = (c46.x + r0.z - c255.x) * c8.x` (instr 12, 14, 15), where `r0.z` is `w` of the vf1 8_8_8_8 (instr 8, fetch 3). `r5.w = 1` (`sges`, instr 15).
  - `r3 = dp4(c0..c3 .zxyw, r5.zxyw) = dp4(c0..c3, r5)` (instr 17-20), `oPos = r3` (instr 34).
  - In the capture one vertex buffer (4337 vertices, 9576 indices) is drawn 15 times per frame with `c46.x` = 1/16 to 15/16 and `c8.x` = 0.0254: shells pushed out along the normal. The buffer holds posed positions and its bytes change every frame (120 contents in 120 frames). The entry draws it without the push (`deformed`), a few hundredths of a unit (`c8.x` times a factor of order 1; `c255.x` is a shader literal the dump does not show).
- **`0x475E...` (tree trunks and branches, wind).**
  - Position: `vfetch_full r1.wxyz` of vf0, `FMT_16_16_16_16_FLOAT`, stride 9 (instr 11, fetch ordinal 0). Under 8in32 the swizzle `wxyz` gives `r1 = (m2, m1, m0, m3)` of the memory order `m` (section 9).
  - `r9.xyz = r5.xyz + r1.xyz` (instr 65). `r5` is the wind offset: `sin`/`frc` chains over `c15..c17` weighted by the 8_8_8_8 of fetch 4 (instr 17-58). `r9.w = 1` (`sges`, instr 68).
  - `r2 = dp4(c0..c3 .zxyw, r9.xzyw) = dp4(c0..c3, (r9.z, r9.y, r9.x, 1))` (instr 69-72), `oPos = r2` (instr 97). The undeformed position is `(m0, m1, m2, 1)`: the fetch read as `yxw1`, the override `0x79EA...`, `0xA1F7...` and `0xD4D5...` carry.
  - The fetch's `w` (`m3`; `truncs r0.w` at instr 31, `maxas` at instr 65) only selects `c[28+a0]..c[31+a0]` for `o8` (instr 66-68, 77).
- **`0xA584...` (leaf clumps, wind about a pivot).**
  - Position `P`: `vfetch_full r6.yxwz` of vf0, `FMT_16_16_16_16_FLOAT`, stride 9 (instr 15, fetch ordinal 0): `(m0, m1, m2, m3)`. Pivot `Q`: `vfetch_mini r5.wxy_`, offset 5, same format (instr 18, fetch ordinal 3), held reversed.
  - `r6.xyz = P - Q` (instr 26); `r4.z = |P - Q|` (instr 35, 40); `r5.xyz = Q + wind offset` (instr 74); `r0` = `P - Q` with a wind term added to z, renormalised (instr 93-97).
  - `r8.xyz = r0.zyx * r4.z + r5.xyz` (instr 98), `r8.w = 1` (`sges`, instr 92); `r1 = dp4(c0..c3 .zxyw, r8.xzyw)` (instr 99, 100, 106, 109), `oPos = r1` (instr 147).
  - The position is rebuilt as pivot plus length times direction. With the wind terms at zero that is `P`, the fetch read as `yxw1` (the direction is then `(P - Q) / |P - Q|`; this takes the literal `c252.z` as 0, the value the shader's own `sgt`/`sge` 0-and-1 idioms at instr 42, 145 and 146 need). The fetch's `w` (instr 31, 113) again only indexes `c[28+a0]`.
  - The entry draws the plain `P`, not the rebuilt position. The wind turns each vertex about its pivot at a fixed distance and shifts the pivot; over the 8890 sampled vertices of the frame's 22 draws `|P - Q|` is 1.1 units at the median, 7.7 at p99 and 12.4 at most, so the pivots are not the vertices themselves; that they are branch attachments is an inference from those distances (no pivot was matched to a branch). What makes the plain `P` acceptable is observation, not a bound: `P` projected over the view-off screenshot, which has the wind applied, lands on the leaf clumps of the emulated trees, and native view shows the leaf cards on the emulated crowns. That was judged by eye; the sway itself was not measured.
- **`0x2D40...`** has the fetch layout and the position of `0xA584...` (fetches at instr 17 and 20, `P - Q` at 28, rebuilt position at 102, `dp4` at 103, 104, 110, 113). It is not in this capture and not in `native_discovery_20261002_194918`; it was seen once per frame in the user's play log `fable_2_135.log` (section 10). A rebuilt position is not "the fetched position plus a displacement", so the disassembly alone gives no entry. `user-checks.md` asks for a capture that contains it.

**Controls** (`--entry`, same capture, 200 draws each). The share separates a wrong matrix but not a wrong component order for these shaders:

| Shader | Entry | Share |
|---|---|---|
| `0x7C57...` | candidate | 1.000 |
| | base 4 | 0.000 |
| | layout combine | 1.000 |
| | `pos_swizzle` `yxz1` (x and y swapped) | 1.000 |
| `0x475E...` | candidate (`yxw1`) | 0.455 |
| | the fetch's own swizzle `wxyz` | 0.420 |
| | `wxy1` (x and z swapped) | 0.525 |
| `0xA584...` | candidate (`yxw1`) | 0.530 |
| | the fetch's own swizzle `yxwz` (`w` = the constant index) | 0.440 |
| | `wxy1` | 0.495 |
| | the pivot element (`pos_fetch` 3, `yxw1`) | 0.530 |

**Why the two tree shaders score low, and why they are accepted.** The scene, not the transform:

- `c4..c6` is the world matrix in these shaders and in `0xECD6...`: each computes `dp4(c4..c6, position)` (`0xECD6...` exports it as `o2`, instr 24-26; the three shaders here subtract it from `c9`, which holds one value, the camera position, in every row). `c0..c3 * inverse(c4..c6)` is then the view-projection, which must be the same for every draw of a frame. Against the per-frame median of 3000 `0xECD6...` rows, the largest element difference over 200 sampled rows is 2.7e-05 (`0x475E...`), 2.9e-05 (`0xA584...`) and 7.1e-06 (`0x7C57...`); `0xECD6...` rows differ from their own median by up to 3.6e-05. So `c0..c3` is the draw's world-view-projection, base 0, dot.
- Every world matrix of the three shaders is a rotation about z with a uniform scale (off-axis terms under 1e-06, scale 0.65 to 1.48). Read as `yxw1`, the meshes are upright: z spans -2.0 to 18.4 (`0x475E...`) and 3.8 to 22.0 (`0xA584...`), x and y stay within 12.3 of the axis.
- The failing draws are meshes outside the view or mostly outside it. `0x475E...`: 109 of 200 fail, 54 wholly beside the view, 41 mostly behind the camera, 14 across an edge. `0xA584...`: 94 fail, 27 beside, 33 behind, 34 across an edge; so 34 of its 94 failing draws are partly inside the view, with fewer than half of their sampled vertices in it, and "whole meshes outside the view" holds for the other 60. The frame's 30 `0x475E...` draws are static (the autoplay camera does not move): 13 pass and 15 have no sampled vertex on screen. The game draws every part of a tree whose bound touches the view, and trees behind the camera (section 9 found the same for `0xA1F7...`: 150 of 460 rows behind).
- Projection onto the emulated frame. The replayed positions of every distinct draw, projected with the row's `c0..c3` and plotted over a view-off screenshot of the same autoplay scene (window shot at 95 s): with `yxw1` the `0x475E...` points are vertical lines on the birch trunks left of the bridge and on the sapling behind the hero, and the `0xA584...` points sit on the leaf clumps of the same trees. One group of stems and leaves projects onto the bridge deck beside the left post; it is read as a shrub under the bridge (an inference from the next sentence, not measured). The deck hides its stems in the emulated frame and in the clay view alike, and its top shows through the railing in the emulated frame. With `wxy1` the trunks lie horizontal across the bridge; with the fetch's own `w` the `0xA584...` meshes collapse into dots. The 4337 vertices of the `0x7C57...` buffer cover the sitting dog.

What settles each question:

- The matrix: the view-projection identity above (200/200 rows within 2.9e-05). This is what the in-clip rule is for, checked directly.
- The component order: the dump reading, with the projection over the view-off screenshot and the native-view screenshot below agreeing with it. The in-clip share cannot settle it (controls table).
- What remains for a person: walking around the trees in overlay view (`user-checks.md` check 8). A wrong component order would show trunks lying sideways or leaf clumps collapsed to dots.

**Measured.** `.\tools\drive_game.ps1 -Total 120 -Shots "70,95" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"`: before `fable_2_150.log`; with `0x7C57...` only `fable_2_157.log` (first commit of the task); with all three `fable_2_158.log`:

```
before        [native] capture: frame 2700 captured 987 drawable 749 (deformed 29) skipped {no-transform: 80, unsupported-prim: 30, bad-index: 128} nested_total 0
before        [native] capture: frame 2700 no-transform by vs {0x475EC9F795E5EDBB: 30, 0xA5846836C90E1192: 22, 0x7C5710DEF3EE33C4: 15, 0x8123C16DBF583F92: 7, 0x29B6506FBACEB93A: 5}
0x7C57 only   [native] capture: frame 2700 captured 986 drawable 764 (deformed 44) skipped {no-transform: 64, unsupported-prim: 30, bad-index: 128} nested_total 0
0x7C57 only   [native] capture: frame 2700 no-transform by vs {0x475EC9F795E5EDBB: 30, 0xA5846836C90E1192: 22, 0x8123C16DBF583F92: 7, 0x29B6506FBACEB93A: 4, 0x775C6085FBB9D676: 1}
all three     [native] capture: frame 2700 captured 987 drawable 816 (deformed 96) skipped {no-transform: 13, unsupported-prim: 30, bad-index: 128} nested_total 0
all three     [native] capture: frame 2700 no-transform by vs {0x8123C16DBF583F92: 7, 0x29B6506FBACEB93A: 5, 0x775C6085FBB9D676: 1}
all three     [native] capture: frame 2700 drawable by vs {0xC30A97D946FA2BE4(terrain): 340, 0xFB68A7F2301210E1(terrain): 152, 0xECD66A10092E6562: 122, 0x5003700B7C9B1C16(terrain): 75, 0x475EC9F795E5EDBB(deformed): 30, 0xD4D558DA6A82BDC8(deformed): 29, 0xBEAD84BD72072E0E: 24, 0xA5846836C90E1192(deformed): 22, 0x7C5710DEF3EE33C4(deformed): 15, 0x79EAC49585797037: 4}
```

D / C goes from 749 / 987 = 0.759 to 816 / 987 = 0.827 (0.775 with the shells alone). The 67 new draws are counted `(deformed)`: 96 against 29. Textured goes from 158 to 188, the 30 `0x475E...` draws through their `"uv"`; the other 37 are `no-albedo` (61 against 24). 30.0 fps in the last six 300-frame windows of both runs with all three; capture median 1.13 ms before, 1.33 ms in the split run and 1.17 ms in the native-view run (`fable_2_159.log`: captured 985, drawable 814, no-transform 13).

Screenshots:

- **Native view, 95 s** (`.\tools\drive_game.ps1 -Total 110 -Shots "95" -GameArgs "--fable2_native_render=true","--fable2_native_view=native"`), against a view-off shot of the same autoplay scene at 95 s. The two birches left of the bridge stand where the emulated frame has them: thin pale trunks at the same three places, leaf cards on the same crowns. The large leaves that hang into the top left of the emulated frame are large clay cards in the same place. Nothing new lies on the bridge or floats in the sky. Not confirmed in the clay view: the small tree beside the fence post behind the hero's head (49 units away, about 40 by 100 pixels). The offline projection puts its trunk and leaf points exactly on it in the emulated frame, and its draws are among the 816 drawn, but in the native shot it cannot be told from the pale terrain behind it; whether it only lacks contrast (flat clay cards on clay ground) or the clay terrain covers it is open. The leaf cards are flat opaque clay, larger than the leaves they carry: `0x7CD5...` is no_albedo and the clay pass has no alpha test. The ferns, the grass and the far forest of the emulated frame are still missing; they are instanced shaders, not this group.
- **Split, 70 s and 95 s.** The clay half is the right half of the frame and every tree of this scene is in the left half, so the split shots show only the dog: its coat (`0x7C57...`) is a flat clay shape in the dog's real pose (sitting, as in the emulated frame), behind the bind-pose body that `0xD4D5...` draws standing. This is why the trees are judged in native view.

**UV.**

- `0x7C57...`: `shader_trace.py` gives `o0.xy = r4.xy` (instr 38) from `vfetch_mini r4.yx__`, offset 3, `FMT_16_16_FLOAT` (instr 6, fetch ordinal 1); added as `"uv"`. `o0.zw` (instr 11, a `mad` with a fetched addend) is refused and left out. Its only pixel shader is no_albedo, so the entry is not used yet.
- `0x475E...`: `shader_trace.py` gives `o0.xy` from `vfetch_mini r4.yx__`, offset 3, `FMT_16_16_FLOAT` (instr 13, fetch ordinal 2; `max o0.xy__, r4.xyyy` at instr 98), for the albedo fetch tf0 of `0x00E0...` (which reads `r0.xy` through its `c8` scale and offset stage); added as `"uv"`. No other instruction writes `r4.xy` (instr 24, 26, 35, 36, 87 and 89 write `r4.z` or `r4.w`). The 30 draws per frame are textured with it.
- `0xA584...`: `0x7CD5...` is no_albedo (its tf0 coordinate is refused: `scalar co-issue mulsc writes component`); the UV leaves the vertex shader in `o1.w` and `o2.w` (instr 148, 153, 154). No entry.

### Instancing

Seven vertex shaders draw many copies of a small mesh (grass, ferns, low plants) in one indexed draw: 135 draws per frame in the bridge scene, 4,039 of the capture's 11,523 in-scene rows. All seven follow one scheme and all seven are in the table (Task 7, 2026-10-05), with the shaders' distance cut modelled since Task 7b (2026-10-06). One passes the in-clip rule; six are in on the rule's exception path, with the evidence that path asks for.

| Vertex shader | Rows | Distinct draws | Pixel shaders (albedo table) | Moves the position | Bias literal | In-clip share | In-clip verdict | Table |
|---|---|---|---|---|---|---|---|---|
| `0x8123C16DBF583F92` | 2354 | 79 | `0xA7E4...`, `0xE99F...` (tf0) | sway | `c254.z` | 0.802 (116 draws, 84 cut) | ACCEPT | entry, deformed, `"uv"` |
| `0xB636821F95DC9D8E` | 1128 | 38 | `0x165E...`, `0xF525...` (tf0) | sway | `c254.w` | 0.689 (135, 65 cut) | REJECT | entry (exception), deformed, `"uv"` |
| `0x6AD4108C3FF07966` | 247 | 8 | `0x165E...`, `0xF525...` (tf0) | nothing | `c254.z` | 0.422 (102, 98 cut) | REJECT | entry (exception), `"uv"` |
| `0x48D30ECCF684F488` | 109 | 4 | `0x2605...` (tf0) | sway and push | `c254.z` | 0.229 (109) | REJECT | entry (exception), deformed, `"uv"` |
| `0xFC4F2EF6930768BE` | 105 | 3 | `0xE99F...`, `0xA7E4...` (tf0) | sway and push | `c254.z` | 0.000 (105) | REJECT | entry (exception), deformed, `"uv"` |
| `0x33C00226C154C1F2` | 61 | 2 | `0x165E...`, `0xF525...` (tf0) | sway and push | `c254.z` | 0.426 (61) | REJECT | entry (exception), deformed, `"uv"` |
| `0x36B543CECAE5C781` | 35 | 1 | `0x165E...` (tf0) | push | `c254.w` | 0.000 (35) | REJECT | entry (exception), deformed, `"uv"` |

Every entry is `base 0`, `dot`, `"pos_swizzle": "yxw1"` and `"instance": {"mesh_fetch": 4, "row_fetches": [0, 1, 2], "row_swizzles": ["yxwz", "yxwz", "yxwz"], "inv_count": "c12.x", "count": "c12.y", "first": "c12.z", "bias": 0.5, "offset": "c7.xyz", "cut": {"eye": "c9.xyz", "dist2": "c13.z"}}`. `position_check.py` on `native_discovery_20261005_103213.jsonl` (baseline 0.810, accept at 0.710 or more) reports `bad-index 0`, `unsupported 0`, `unreadable 0` for all seven; the full lines are in each entry's evidence string. The shares in the table are over the draws that keep a vertex after the distance cut; before the cut was modelled every sampled vertex counted and the first three read 0.835, 0.720 and 0.705 over 200 draws each.

**The scheme** (`out\shader_dump\shader_<HASH>.ucode.vert`; instruction numbers of `0x8123...`, the other six are in their evidence strings).

- Index (instr 10-13, 18, 19): `t = trunc((index + bias) * c12.x)`, `copy = trunc(c12.z) + t`, `vertex = index + trunc(-(c12.y * t))`. `c12 = (1 / count, count, first copy, 1 / copies drawn)`.
- Per copy, three half4 rows from stream 1 (slot 94) at `[copy]`, stride 7 dwords, offsets 0, 2, 4 (fetch ordinals 0-2; ordinal 3 is an 8_8_8_8 at offset 6; from its use, `log` and `exp` scaled by `c18` and exported, probably a per-copy colour, which was not verified). Per vertex, a half4 position from stream 0 (slot 95) at `[vertex]`, stride 6 dwords (ordinal 4), and a half4 normal at offset 2 (ordinal 5). `0xB636...`, `0x33C0...` and `0x36B5...` also fetch a half4 tangent at offset 4 (ordinal 6).
- `r11 = cndeq(c254.xxxy, r0.xzww, c254.yyyy)` (instr 25) is the position with `w = 1`; `r10 = (dp4(r4.zxyw, r11), dp4(r2.yxwz, r11), dp4(r5.wxyz, r11))` (26-28); `r5.xyz = r10.xyz + c7.xyz` (31).
- Component order. Under 8in32 the GPU's source of a half4 is `(m1, m0, m3, m2)` of the memory order `m` (section 9). The position fetch `r0.wzyx` gives `r0 = (m2, m3, m0, m1)`, so `r11 = (m2, m0, m1, 1)`. The row fetches `r4.yxwz`, `r2.ywzx`, `r5.yxzw` give `(A0, A1, A2, A3)`, `(B0, B2, B3, B1)`, `(C0, C1, C3, C2)` for the stored rows `A`, `B`, `C`. Each `dp4` pairs the components back: `dp4(r2.yxwz, r11) = B2 m2 + B0 m0 + B1 m1 + B3`. So the position before `c7` is `(A.p, B.p, C.p)` with `p = (m0, m1, m2, 1)`: every row read as `yxwz` and the position as `yxw1`. The shaders differ in which rows they fetch in another order (that is why the table overrides all three row swizzles), never in the result. The position's `m3` is a texture coordinate, so the fetch's own `w` must not be used.
- Distance cut (32, 34-37): `r10 = c9 - r5` (32), `r3.y = |r10|^2` (34), `p0 = r3.y > c13.z` (35, 36), and with `p0` the position becomes `c255.xyz` (37). The test is per vertex, strict, and on `r5`, the world position before sway and push. `c9` is the camera position, as in the wind shaders. The other six shaders test the same two constants the same way (instruction numbers in their evidence strings).
- Otherwise (38-61) a sway is added: `r2.xyz = r5.xyz + r1.y * sway` (58). `0x48D3...`, `0xFC4F...`, `0x33C0...` and `0x36B5...` then loop over `c[56+a0]..c[59+a0]` and push `xy` away from up to four points with a radius each (by its shape, plants bending away from characters; that is an inference, the points were not identified); `0x36B5...` has the push without the sway and `0x6AD4...` has neither.
- `r4 = cndeq(c254.xxxy, r2.zxyy, c254.yyyy)` (64), `r0 = dp4(c0..c3 .zxyw, r4) = dp4(c0..c3, (r2.xyz, 1))` (65-68), `oPos = r0` (69).

The spec's reading of the predicate ("an optional sway") was wrong: the sway is the normal path and the predicate is the distance cut.

**Literals.** `c252..c255` are shader literals, zero in the device bank. They were read from each shader object's own constant table, the one `GpuLoadShaders` uploads (`0x82221CB0`, section 8), by a temporary discovery diagnostic (reverted, not committed) in capture `native_discovery_20261005_123635` (6 frames, every draw). The table is at `header + *(header + 0x14 + 8 * variant)`: dword `+0x10` is its size, then from `+0x14` entries of `{u16 first register, u16 dword count, u32 offset}` up to a zero count (`0x82221CD4..0x82221D6C`); the values are big-endian floats at `GpuAddress(object dword 8 + offset)`, uploaded with `LOAD_ALU_CONSTANT`. All seven shaders have one entry, registers 252-255.

| Shader | `c253` | `c254` | `c255` | Bias |
|---|---|---|---|---|
| `0x8123...` | (-pi, 2 pi, 1 / (2 pi), 0) | (0, 1, 0.5, 0.1) | (NaN, NaN, NaN, 5) | `c254.z` = 0.5 |
| `0xB636...` | (-pi, 5, 1 / (2 pi), 0) | (0, 1, 2 pi, 0.5) | (NaN, NaN, NaN, 0.1) | `c254.w` = 0.5 |
| `0x6AD4...` | 0 | (0, 1, 0.5, 0) | (NaN, NaN, NaN, 0) | `c254.z` = 0.5 |
| `0x48D3...`, `0xFC4F...` | (-pi, 4, 2 pi, 1 / (2 pi)) | (0, 1, 0.5, 0.1) | (NaN, NaN, NaN, 5) | `c254.z` = 0.5 |
| `0x33C0...` | (-pi, 4, 1 / (2 pi), 2 pi) | (0, 1, 0.5, 0.1) | (NaN, NaN, NaN, 5) | `c254.z` = 0.5 |
| `0x36B5...` | 0 | (0, 1, 4, 0.5) | (NaN, NaN, NaN, 0) | `c254.w` = 0.5 |

The capture and the diagnostic are not committed, so the rows the bias comes from are quoted raw (big-endian dwords, x first):

| Shader | `c254` | `c255` |
|---|---|---|
| `0x8123...` | `00000000 3F800000 3F000000 3DCCCCCD` | `7FE00000 7FE00000 7FE00000 40A00000` |
| `0xB636...` | `00000000 3F800000 40C90FDB 3F000000` | `7FE00000 7FE00000 7FE00000 3DCCCCCD` |
| `0x6AD4...` | `00000000 3F800000 3F000000 00000000` | `7FE00000 7FE00000 7FE00000 00000000` |
| `0x48D3...` | `00000000 3F800000 3F000000 3DCCCCCD` | `7FE00000 7FE00000 7FE00000 40A00000` |
| `0xFC4F...` | `00000000 3F800000 3F000000 3DCCCCCD` | `7FE00000 7FE00000 7FE00000 40A00000` |
| `0x33C0...` | `00000000 3F800000 3F000000 3DCCCCCD` | `7FE00000 7FE00000 7FE00000 40A00000` |
| `0x36B5...` | `00000000 3F800000 40800000 3F000000` | `7FE00000 7FE00000 7FE00000 00000000` |

`3F000000` is 0.5 and `7FE00000` a quiet NaN. So the bias is 0.5 in all seven and `c254.xy = (0, 1)`, as the `cndeq` idiom needs. The offline check agrees with 0.5 independently: with bias 0.0, 24 of the 200 sampled `0xB636...` draws and 105 of the 200 `0x6AD4...` draws become `bad-index` (an index maps outside its copy; their 86 to 692 vertices per copy have no exact reciprocal), with 0.5 none. No sampled draw hits the bounds rule with 0.5: the largest index of any draw is 6,839. The same dump gives literals other sections left open: `c255 = (1, 0.3, 0.01, 0)` for `0x7C57...`, and `c255 = (0, 1, 0.5, 0)` for `0x79EA...`, `0xA1F7...` and `0xD4D5...`. Their raw `c255` row is `00000000 3F800000 3F000000 00000000`. Open: section 9 quotes `(0, 1, 3, 2)` for `0x79EA...` from the `[vtess]` register log. The two agree in the `x` and `y` that shader uses; why `z` and `w` differ (a stale register-file row from a terrain draw, or a table the upload does not use as read here) was not looked into, and until it is, this section's reading of the literal table has no second source for those shaders. For the seven instancing shaders the second source is the bias control above.

**The distance cut** (Task 7b). `c255.xyz` is NaN, so a vertex farther than `sqrt(c13.z)` from the camera gets a NaN position and its triangles are dropped (the terrain shaders cut holes the same way, section 9). `c13.z` is set per draw: 26.6 to 66.3 units in this capture. The game submits whole batches and lets the shader cut them. Over the 135 distinct draws (257,804 flat positions per frame) 123,396 positions, 48%, are past their draw's distance, and 40 of the draws lie wholly past it (rows: 37% of `0x8123...`, 18% of `0xB636...`, 48% of `0x6AD4...`; none of the four near-field shaders).

- Table: `"cut": {"eye": "c9.xyz", "dist2": "c13.z"}` inside `"instance"`; `gen_transform_table.py` writes the two references as the last two arguments of `FABLE2_VS_INSTANCE` (`36, 54`; `-1, -1` for an entry without a cut).
- Capture: `FillInstanceSet` reads the two constants per draw into the draw's inputs; `AssembleRecord` puts them into `DrawRecord::cut` (eye x, y, z, squared distance) for an instanced record only. Every other record, an entry without `"cut"`, and constants that are not a finite eye and a finite squared distance of at least zero carry `cut[3] = +inf` (`SanitizeCut`, `draw_record.h`): garbage never removes geometry.
- Renderer: the cut is the last float4 of the clay constants (32 dwords). `kClayVs` tests the flat position it reads, the same point the game tests: `if (dot(cut.xyz - p.xyz, cut.xyz - p.xyz) > cut.w)` the clip position becomes four NaNs, so every triangle with a cut vertex is dropped, as in the game. No comparison is true against +inf, so other draws are untouched. `test_native_shaders.cpp` checks that the compiled vertex shader still holds the four NaN dwords.
- Cache: the cut is in no `GeometryCache` key and no content hash (`test_clay_logic.cpp` 132, 133), so the camera moving rebuilds nothing; drawable, instanced and textured counts do not change, because the cut drops triangles on the GPU, not records.
- Checker: `position_check.py` applies the same test in float32 to the replayed flat positions. A draw passes when at least half of its kept vertices are inside the clip volume; a draw with no vertex kept is counted `cut` and leaves the share. The line ends with `cut N, kept vertices K of S`. The in-clip share is no evidence for the cut constants: with `"eye": "c7.xyz"` (the offset register) the three entries score exactly as with no cut at all (0.835, 0.720 and 0.705 on 200 draws each, nothing cut), so a wrong eye register would pass unnoticed. The cut rests on the dump reading and on the screenshot difference below. Because wholly cut draws leave the share, an entry is rejected when the draws that remain are fewer than 20 or fewer than a tenth of the sampled rows (`MIN_JUDGED_DRAWS`, `MIN_JUDGED_SHARE`; `0x8123...` is judged on 116 of 200, `0xB636...` on 135, `0x6AD4...` on 102), and when a sampled row is one the runtime would skip (`unsupported`, `bad-index`); the line then reads `REJECT (<reason>)`.

What it changes on screen in the bridge scene is small, and less than Task 7 expected. Between the Task 7 native-view shot and the same shot with the cut, 2,451 pixels differ (76 between two shots without the cut, from the animated hero and dog). 96.6% of them lie within 6 pixels of a projected cut vertex, which holds for 5.6% of the screen; none of the 76 control pixels do. So the renderer removes what the checker says the shader cuts and nothing else. The removed geometry is small and far: mostly grass tufts of `0x8123...` 33 to 63 units away, much of it hidden behind nearer cards and the bridge. The row of leafy cards behind the right-hand fence, which Task 7 took for cut geometry, mostly stays: 7,676 of the vertices projecting into it are kept (5,040 of `0xB636...` and `0x6AD4...` plants at 33 to 58 units, inside their draws' distances of up to 66 units, and 2,636 of `0x8123...`) against 27,317 cut ones, 25,146 of them `0x8123...` grass. The emulated frame has low bushes along that bank; the clay cards look larger because they are opaque rectangles.

**Near and far shaders.** Observed: every vertex the four shaders with the push draw is 5.6 to 18.5 units from the camera, and three of them share instance streams with a shader without it. `0xFC4F...` draws copies 0-35, 0-31 and 14-23 of three instance streams whose later copies `0x8123...` draws with the same mesh streams in the same frame (first copy 72, 108, 156; 32, 64; 24). `0x33C0...` shares two streams with `0xB636...` the same way and `0x36B5...` one with `0x6AD4...`. The four instance streams of `0x48D3...` are drawn by no other shader in this capture; only its four meshes are also drawn by `0x8123...`. Inferred from that: the engine sorts the copies of a batch by distance and gives the nearest batches to the shader with the push. It holds per batch, not strictly per copy: over the six shared streams the near shader's copy is the closer one in 4,708 of the 4,832 (near copy, far copy) pairs, 97.4%. The camera stands on a bridge above that near ground, which is why these four score low in clip.

**Why the shares are low, and what settles each question.**

- The matrix. `c0..c3` of every instanced row equals the view-projection of `0xECD6...` in the same frame (its `c0..c3` times `inverse(c4..c6)`, per-frame median) within 5.1e-06 (`0x8123...`, `0xB636...`, `0x6AD4...`, `0x48D3...`, `0x33C0...`) and 3.8e-06 (`0xFC4F...`, `0x36B5...`), in all 4,039 rows. So the rows and `c7` produce world positions and `c0..c3` is base 0, dot. A wrong offset register is caught by the share: `c14.xyz` instead of `c7.xyz` gives 0.000 for all seven.
- Where the failing draws are (the checker's own sample, first 512 indices of each draw, judged on the vertices kept; draws wholly cut are not in it):

  | Shader | Failing | Mostly behind the camera | Wholly below the view | Wholly beside the view | Across an edge |
  |---|---|---|---|---|---|
  | `0x8123...` | 23 of 116 | 0 | 10 | 4 | 9 |
  | `0xB636...` | 42 of 135 | 0 | 20 | 13 | 9 |
  | `0x6AD4...` | 59 of 102 | 0 | 0 | 26 | 33 |
  | `0x48D3...` | 84 of 109 | 32 | 26 | 26 | 0 |
  | `0xFC4F...` | 105 of 105 | 35 | 0 | 35 | 35 |
  | `0x33C0...` | 35 of 61 | 0 | 35 | 0 | 0 |
  | `0x36B5...` | 35 of 35 | 35 | 0 | 0 | 0 |

  No failing draw has a NaN or a vertex past the far plane. The near-field rows are few draws repeated every frame: three for `0xFC4F...`, two for `0x33C0...`, one for `0x36B5...`. The classes describe the checker's sample, the first 512 indices of a draw: the failing `0x33C0...` draw is wholly below the view in that sample, and over all its 663 indices 38% of the vertices are behind the camera.
- Before the cut was modelled the share also counted draws the game does not show: of the sampled draws, 84 (`0x8123...`), 65 (`0xB636...`) and 98 (`0x6AD4...`) have every sampled vertex past the distance cut. They are now counted `cut`, and the shares are 93 of 116 (0.802), 93 of 135 (0.689) and 43 of 102 (0.422). `0xB636...` passed at 0.720 only with those draws, so it is on the exception path like the other five; its evidence records the same three conditions.
- The component order. The share does not settle it: the rows hold small offsets inside a batch (the batch origin is `c7`), so a wrong order moves a mesh by a few units and often keeps it on screen.

  | Control (`--entry`, 200 draws, run before the cut was modelled: every sampled vertex counted) | `0x8123...` | `0xB636...` | `0x6AD4...` |
  |---|---|---|---|
  | candidate | 0.835 | 0.720 | 0.705 |
  | row fetches `[1, 0, 2]` | 0.690 | 0.580 | 1.000 |
  | row fetches `[0, 2, 1]` | 0.840 | 0.670 | 0.705 |
  | row swizzles `yxzw` (z and w swapped) | 0.755 | 0.665 | 0.870 |
  | the row fetches' own swizzles | 0.890 | 0.730 | 0.760 |
  | the position fetch's own swizzle | 0.740 | 0.590 | 0.870 |
  | `pos_swizzle` `xyw1` | 0.835 | 0.720 | 0.870 |
  | offset `c14.xyz` | 0.000 | 0.000 | 0.000 |
  | bias 0.0 | 0.835 | 24 bad-index | 105 bad-index |
  | bias 1.0 | 200 bad-index | 187 bad-index | 175 bad-index |

  What settles it is the dump reading, and two checks that do not use it, run on every distinct draw of the capture:

  | Shader | Copies | Largest cosine between two rows, `yxwz` | Row lengths, max / min | Determinant | Mesh z on world z, median | Largest cosine, the fetches' own swizzles (median) | Face normals, `yxw1`, distinct triangles | Best other order | Face normals, `yxw1`, per copy | Best other order |
  |---|---|---|---|---|---|---|---|---|---|---|
  | `0x8123...` | 2129 | 0.0007 | 1.0009 | +1.000 | 1.000 | 0.905 | 0.978 (530 triangles, 7 meshes) | 0.701 | 0.988 (1317) | 0.493 |
  | `0xB636...` | 218 | 0.0005 | 1.0009 | +1.000 | 0.982 | 0.919 | 0.960 (1884, 9) | 0.669 | 0.965 (2476) | 0.682 |
  | `0x6AD4...` | 33 | 0.0003 | 1.0007 | +1.000 | 0.962 | 0.979 | 0.966 (873, 3) | 0.629 | 0.966 (873) | 0.629 |
  | `0x48D3...` | 112 | 0.0003 | 1.0008 | +1.000 | 0.999 | 0.901 | 0.999 (32, 4) | 0.462 | 0.999 (671) | 0.468 |
  | `0xFC4F...` | 78 | 0.0002 | 1.0007 | +1.000 | 1.000 | 0.900 | 0.985 (258, 3) | 0.749 | 0.988 (634) | 0.684 |
  | `0x33C0...` | 23 | 0.0003 | 1.0006 | +1.000 | 0.984 | 0.806 | 0.953 (406, 2) | 0.623 | 0.962 (598) | 0.661 |
  | `0x36B5...` | 5 | 0.0004 | 1.0004 | +1.000 | 0.942 | as fetched | 0.937 (361, 1) | 0.621 | 0.937 (361) | 0.621 |

  Read as `yxwz`, the three rows of every copy are a rotation with one scale (0.6 to 11.7) that keeps the mesh upright: the mesh's z axis, the one the shaders weight the sway by, lands on world z. A swapped pair of rows would give determinant -1 and a rotated order would tip the plants over. The face-normal figure is the mean |cos| between the triangle normals of the mesh positions and the stored vertex normals (fetch ordinal 5, read `yxwz`), for the candidate and the five other orders of x, y, z. The triangles are those of a draw's first 512 indices. A small mesh repeats there once per copy, so the figure is given twice: over the distinct triangles of each mesh, and with every repeat counted ("per copy"). On distinct triangles the candidate scores 0.937 to 0.999 and no other order more than 0.749; the 32 distinct triangles of `0x48D3...` are a thin sample.
- On screen: the native-view screenshot below.

**UV.** `shader_trace.py` refuses all seven (`control flow before export: jmp`), so the entries are hand traces of both predicate branches, recorded per shader in `uv_evidence`. In each, `o0.x` is source z of the mesh position fetch (ordinal 4, format 32, offset 0) and `o0.y` is source z of the normal fetch (ordinal 5, format 32, offset 2): the texture coordinate rides in the fourth halves of the position and of the normal. Over the capture's 19 distinct mesh dumps u runs from -0.001 to 1.016 and v from -0.001 to 1.006 (a grass card: (0, 1), (0.312, 0), (0.624, 1)); the tangent's fourth half is +1 or -1. The five pixel shaders were already in the albedo table (tf0 through `c8`).

The runtime could not read this: `ResolveUvFetch` required one vertex fetch for both axes and `UvLayout` described one element, so the 135 draws came out `uv-unsupported` (flat clay; `fable_2_165.log`). `UvLayout` now carries `v_element_delta`, the byte step from the element holding u to the one holding v, and `UvLayoutFromFetches` accepts two fetches of one stream with the same stride and element format (`uv_decode.h`, `material.h`; `clay_logic.h` adds the step to the UV cache key and counts vertices by the later element). Two fetches of different streams or formats are still `uv-unsupported`. Tests: `test_uv_decode.cpp` 19-36 (real grass vertices), `test_material.cpp` 30-33, `test_clay_logic.cpp` 127-129, `test_instance_expand.cpp` 65-68.

**Regression case.** `test_instance_expand.cpp` `RealDraw` (50-68): a `0x8123...` draw of the capture (12 vertices per copy, copies 32 and 33 rebased to 0 and 1, `c12.x` = 0.0833333358), printed by `position_check.py`'s `cpp_fixture()` for that draw. The CLI's `--cpp-fixture` picks the first passing draw of the sample, a 190-vertex mesh that stays inside copy 0; this one crosses a copy boundary with an inexact reciprocal in 344 bytes. The test selects the rows and the position with the generated table's arguments, expands 16 indices and compares with the checker's positions within 1e-3, and checks that the rows' own swizzles give other positions. `RealDraw` does not pin the bias: the float nearest 1/12 is above 1/12, so bias 0.0 maps its indices the same. Cases 70-75 do, with the constants of a `0xB636...` draw of the capture whose reciprocal rounds down (`c12 = (0x3C064B8A, 122, 2)`, largest index 365): index 122 is copy 3, vertex 0 with 0.5, and copy 2, vertex 122 with 0.0.

**Measured.** `.\tools\drive_game.ps1 -Total 120 -Shots "70,95" -GameArgs "--fable2_native_render=true","--fable2_native_view=<view>"`: before `fable_2_164.log` (split); entries without `"uv"` `fable_2_165.log` (native); final `fable_2_166.log` (native), `fable_2_167.log` (split) and `fable_2_168.log` (native).

```
before   [native] capture: frame 2700 captured 987 drawable 816 (deformed 96) skipped {no-transform: 13, unsupported-prim: 30, bad-index: 128} nested_total 0 | ... | textured 188 untextured by reason {terrain: 567, no-albedo: 61}
no uv    [native] capture: frame 2700 captured 987 drawable 951 (deformed 223) skipped {no-transform: 6, unsupported-prim: 30} nested_total 0 | ... | textured 188 untextured by reason {terrain: 567, no-albedo: 61, uv-unsupported: 135}
final    [native] capture: frame 2700 captured 987 drawable 951 (deformed 223) skipped {no-transform: 6, unsupported-prim: 30} nested_total 0 | ... | textured 323 untextured by reason {terrain: 567, no-albedo: 61}
final    [native] capture: frame 2700 no-transform by vs {0x29B6506FBACEB93A: 5, 0x775C6085FBB9D676: 1}
final    [native] capture: frame 2700 drawable by vs {0xC30A97D946FA2BE4(terrain): 340, 0xFB68A7F2301210E1(terrain): 152, 0xECD66A10092E6562: 122, 0x5003700B7C9B1C16(terrain): 75, 0x8123C16DBF583F92(instanced): 73, 0xB636821F95DC9D8E(instanced): 38, 0x475EC9F795E5EDBB(deformed): 30, 0xD4D558DA6A82BDC8(deformed): 29, 0xBEAD84BD72072E0E: 24, 0xA5846836C90E1192(deformed): 22}
final    [native] clay: drawn 951 (deformed 223) of 951 drawable, skipped_bad_index 0 other 0 | textured 323 of 951 | 2 uploads, 2223 hits, 24.4 MB resident, instanced 135, skinned 3 | hash 0.65 ms, decode 0.03 ms, record 0.24 ms (max total 1.94 ms over 300)
```

D / C goes from 816 / 987 = 0.827 to 951 / 987 = 0.964. The 128 `bad-index` draws and 7 of the 13 `no-transform` draws were the instanced ones: 135 per frame, none `bad-index` or `instance-unsupported` in any of the four runs with the entries. 127 of them count as deformed (223 against 96); the 8 of `0x6AD4...` do not. Textured goes from 188 to 323, 84% of the non-terrain draws. What is still skipped is the two billboard shaders (6) and the unmapped draw builders (30).

Cost: 30.0 fps in every 300-frame window after the world is up, in all four runs with the entries. Clay pass in the steady state: hash 0.39 to 0.68 ms (0.34 to 0.49 before; two more streams are hashed per instanced draw), decode 0.02 to 0.04 ms (0.02 to 0.03), slowest frame of a window 1.8 to 2.2 ms (1.4 to 1.6). Each of the three final runs also has one frame of 4.8 to 5.5 ms in the window that ends at scene frame 3016 (about 100 s); the run before and the run without `"uv"` do not. Its cause was not found. The frame that first builds the scene takes 246 to 306 ms against 177 ms before: the 135 flat streams (257,804 positions, and as many UVs) are built once. Geometry resident 22.4 to 24.4 MB (16.9). Capture median 1.15 ms (1.11); guest work median 7.5 to 8.0 ms (7.4 to 7.5). The autoplay camera does not move. In play the engine re-sorts the copies by distance, which changes the instance streams and `c12.z` and rebuilds flat streams; that cost was not measured (`user-checks.md` check 9).

With the distance cut (Task 7b; `fable_2_169.log` native, `fable_2_170.log` split, `fable_2_171.log` view off):

```
cut      [native] capture: frame 2700 captured 987 drawable 951 (deformed 223) skipped {no-transform: 6, unsupported-prim: 30} nested_total 0 | ... | textured 323 untextured by reason {terrain: 567, no-albedo: 61}
cut      [native] clay: drawn 951 (deformed 223) of 951 drawable, skipped_bad_index 0 other 0 | textured 323 of 951 | 2 uploads, 2223 hits, 22.4 MB resident, instanced 135, skinned 3 | hash 0.49 ms, decode 0.03 ms, record 0.15 ms (max total 1.79 ms over 300)
view off [native] capture: frame 2700 captured 987 drawable 0 (deformed 0) skipped {} nested_total 0 | records off | capture 0.042 ms (median 0.043, p90 0.047, max 0.102 over 300 frames)
```

Captured, drawable, instanced and textured are unchanged (987, 951, 135, 323). Clay hash 0.36 to 0.63 ms, decode 0.02 to 0.05 ms, slowest frame of a window 1.7 to 2.1 ms, geometry resident 22.4 to 22.5 MB, capture median 1.13 to 1.14 ms: all inside the Task 7 ranges. 30.0 fps, with one or two 300-frame windows per run at 29.8, in the view-off run too. The first scene build took 250 and 351 ms, and the one frame of about 5 ms near scene frame 3016 is still there (4.7 and 5.5 ms). View off: capture median 0.043 ms.

Screenshots (window shots, native view at 95 s, against the view-off shot of the same autoplay scene):

- **Left of the bridge.** The ferns stand where the emulated frame has them, between the fence posts beside the big trunk, with the fronds' outline and texture. The band of grass along the edge of the path runs under the same posts, from the trunk to the bridge post, at the height of the emulated band. Grass also shows through the gaps between the bridge planks, where the kept vertices project.
- **Right bank.** A row of leafy cards runs behind the right-hand fence. The emulated frame has low bushes along that bank, denser at the row's right end and beside the fence post. Task 7 read the stretch in between as bare ground and the cards there as plants past the distance cut; Task 7b showed that most of the row is kept geometry (see "The distance cut"). With the cut the row loses a few small tufts and flecks and otherwise looks the same.
- Every card is an opaque rectangle with the texture's black cut-out background: the clay pass has no alpha test. The plants stand still (no sway, no push).
- Before the UV change the same meshes were pale clay on pale clay ground and could hardly be told apart from the terrain (`fable_2_165.log` run).
- **Split, 70 s and 95 s.** The clay half is the right half: it shows the right-bank row and the grass under the planks; the ferns and the grass band are in the emulated half.

### Skinning

Task 9 (2026-10-06). Five vertex shaders blend bone rows from the palette on fetch slot 92: `0xD4D558DA6A82BDC8` (four bones per vertex; 23 to 31 draws per frame in the bridge scene, 826 of the capture's in-scene rows), `0x3A0F9098B839DDBC`, `0x82F6433A69263C75`, `0x9ED0BA440DBD51D4` (four bones) and `0x5F4416192E87005F` (one bone).

**Status: all five are in the table as skin entries.** `0xD4D5...` is no longer `deformed`: its draws are posed on the CPU in the geometry cache, like `0xA1F7...`'s. `position_check.py` accepts it under the orthonormality rule as amended twice in this task ("The checker and its rule" below), and its reading is confirmed vertex for vertex against the game's own skinning. The other four are entered on their dump readings alone ("layout identical to `0xD4D5...`; not sampled in scene"). The bind-pose form is the documented fallback: remove `"skin"` and set `"deformed": true`; each entry keeps its old text as `bind_pose_evidence`.

What `0xD4D5...` draws in the bridge scene (capture rows by vertex buffer, with the entry's positions projected over the view-off screenshot): the hero (body `0x1BF18DC0`, 5163 vertices, 87-bone palette, five index ranges; head `0x1CD2C040`, 3482 vertices, 90 bones; hair `0x1BED81C0`, 5224 vertices, 36 bones; five smaller meshes with palettes of 38 to 90 bones, by their place on screen the boots, the belt, the hands and one at the eyes), the dog (`0x1C0F0040`, 4337 vertices, 90 bones, 80 used), the crows on the right-hand railing (`0x1AA74600`, 2334 vertices, 22 bones, two draws per frame), a flock of small birds among the birches (`0x1C13A380`, 412 vertices, 3 bones, about six draws per frame), and one mesh below the view that was not identified (`0x1A972940`, 3347 vertices, 4 bones, three draws per frame). Pixel shaders `0x8D90...` (521 rows) and `0x401A...` (305 rows: the hair, the small mesh at the eyes, the small birds).

**Reading of `0xD4D5...`** (`out\shader_dump\shader_D4D558DA6A82BDC8.ucode.vert`; fetch ordinals in `DecodeVertexFetches` order).

- Fetches from vf0 (slot 95, 7 dwords per vertex): position `vfetch_full r7.yxwz`, half4 (instr 9, fetch 0); bone indices `vfetch_mini r2`, integer 8_8_8_8 at dword 3 (instr 11, fetch 2); weights `vfetch_mini r4.zyxw`, normalized 8_8_8_8 at dword 4 (instr 12, fetch 3).
- Rows from vf3 (slot 92, 6 dwords per bone, half4 at dwords 0, 2, 4), every one fetched `yxwz`:

  | Bone index | Row 0, 1, 2 | Instr | Fetches | Weight | Instr of the three `mul`/`mad` |
  |---|---|---|---|---|---|
  | `r2.x` | `r10`, `r11`, `r12` | 16-18 | 7-9 | `r4.z` | 40, 39, 38 |
  | `r2.y` | `r15`, `r14`, `r13` | 19-21 | 10-12 | `r4.y` | 35, 36, 37 |
  | `r2.z` | `r16`, `r17`, `r18` | 22-24 | 13-15 | `r4.x` | 34, 33, 32 |
  | `r2.w` | `r1`, `r9`, `r6` | 25-27 | 16-18 | `r4.w` | 29, 30, 31 |

  So the (index, weight) register components pair as (x, z), (y, y), (z, x), (w, w). The weight fetch is `zyxw`, so that is byte k of the index word with byte k of the weight word.
- `r2 = cndeq(c255.xxxy, r7.zxyy, c255.yyyy) = (p.z, p.x, p.y, 1)` (instr 28), with `p = r7` as fetched. `c255 = (0, 1, 0.5, 0)`: the `vs_literals` field of this shader's draw row in `native_discovery_20261005_123635.jsonl` (file line 1606; registers 252-255, sixteen dwords, the last four `00000000 3F800000 3F000000 00000000`), written by the temporary diagnostic that "Literals" under "Instancing" describes; that section lists only the instancing shaders in its tables and quotes this row in its last paragraph. `c255` of the four shaders not drawn in scene ("The other four shaders" below) was not read: that capture has `vs_literals` only on in-scene draw rows (15 shaders). `(0, 1, ...)` is assumed for them from the identical `cndeq(c255.xxxy, ..., c255.yyyy)` idiom in their dumps.
- The three sums are held permuted and the `dp4` operands undo it. Row 0 in `r1`: as fetched (29), `yxzw` (34, 35), as fetched again (40: `mad r1, r10, r4.zzzz, r1.yxzw`). Row 1 in `r9`: `xzyw` (30, 33, 36), then `wxzy` (39: `mad r9, r11.wxzy, r4.zzzz, r9.wxyz`). Row 2 in `r6`: `xzyw` throughout (31, 32, 37, 38). Then `r4.x = dp4(r1.zxyw, r2)`, `r4.y = dp4(r9.zywx, r2)`, `r4.z = dp4(r6.yxzw, r2)` (41-43), and each is `A.z p.z + A.x p.x + A.y p.y + A.w` for the summed row `A` in the order it was fetched.
- `r4 = cndeq(c255.xxxy, r4.zxyy, c255.yyyy)` (70), `oPos = dp4(c0..c3 .zxyw, r4)` (71-74): `c0..c3` (dot) on `(dot(M0, p), dot(M1, p), dot(M2, p), 1)`, `M_k = sum_j w_j row_k(bone_j)`.
- Under 8in32 a half4 fetched `yxwz` is the memory order `(m0, m1, m2, m3)` (section 9). So the rows are read `yxwz` (the fetches' own swizzle) and the position `yxw1` (the entry's existing `pos_swizzle`; the fetch's own `w` is replaced by 1 at instr 28).

**Entries** (`vs-transforms.json`; `"deformed"` removed from all five):

| Vertex shader | `"skin"` | Generated line |
|---|---|---|
| `0xD4D5...` | `{"index_fetch": 2, "weight_fetch": 3, "row_fetches": [7, 8, 9], "row_swizzles": ["yxwz", "yxwz", "yxwz"], "pairs": [["x", "z"], ["y", "y"], ["z", "x"], ["w", "w"]]}`, `pos_swizzle` `yxw1` kept | `FABLE2_VS_SKIN(0xD4D558DA6A82BDC8ull, 2, 3, 7, 8, 9, 0x4C1, 0x4C1, 0x4C1, 4, 0, 2, 1, 1, 2, 0, 3, 3)` |
| `0x3A0F...` | the same with `index_fetch` 1, `weight_fetch` 2, `row_fetches` `[4, 5, 6]` | `(..., 1, 2, 4, 5, 6, 0x4C1, 0x4C1, 0x4C1, 4, 0, 2, 1, 1, 2, 0, 3, 3)` |
| `0x82F6...`, `0x9ED0...` | the same with `index_fetch` 1, `weight_fetch` 2, `row_fetches` `[3, 4, 5]` | `(..., 1, 2, 3, 4, 5, 0x4C1, 0x4C1, 0x4C1, 4, 0, 2, 1, 1, 2, 0, 3, 3)` |
| `0x5F44...` | `{"index_fetch": 1, "index_component": "x", "row_fetches": [2, 3, 4], "row_swizzles": ["yxwz", "yxwz", "yxwz"]}` | `(..., 1, -1, 2, 3, 4, 0x4C1, 0x4C1, 0x4C1, 1, 0, 0, 0, 0, 0, 0, 0, 0)` |

**The other four shaders.** Each fetches the position `yxw1` from vf0 (so no `pos_swizzle`), the index word at dword 3, and its rows `yxwz` from vf3; none is drawn in the main scene of the bridge capture, so none has stream dumps or a `position_check.py` line.

| Vertex shader | Fetches (instr; ordinals) | Blend (instr) | Final `dp4` (instr) | `cexec b0` | Where it is drawn |
|---|---|---|---|---|---|
| `0x3A0F...` | position `r2` (6; 0), indices `r3` (7; 1), weights `r5.zyxw` (8; 2); rows by `r3.x` `r6`/`r7`/`r8` (10-12; 4-6), `r3.y` `r11`/`r10`/`r9` (13-15), `r3.z` `r12`/`r13`/`r14` (16-18), `r3.w` `r1`/`r4`/`r3` (19-21) | `r3.w` with `r5.w` (22-24), `r3.z` with `r5.x` (25-27), `r3.y` with `r5.y` (28-30), `r3.x` with `r5.z` (31-33); row 0 held `xzyw`, `yzxw` (27, 28), `xzyw` (33), rows 1 and 2 `xzyw` | `dp4(r1/r4/r3 .yxzw, r2.zxyw)` (34-36); `oPos` 38-42 | 37: position replaced by the texture coordinate | 1303 rows, all outside the scene, pixel shader `0x3347...`; its 4 vertex buffers are all also drawn by `0xD4D5...` in scene |
| `0x82F6...` | position `r1` (5; 0), indices `r2` (6; 1), weights `r4.zyxw` (7; 2); rows by `r2.x` `r5`/`r6`/`r7` (8-10; 3-5), `r2.y` `r10`/`r9`/`r8` (11-13), `r2.z` `r11`/`r12`/`r13` (14-16), `r2.w` `r0`/`r3`/`r2` (17-19) | `r2.w` with `r4.w` (20-22), `r2.z` with `r4.x` (23-25), `r2.y` with `r4.y` (26-28), `r2.x` with `r4.z` (29-31); row 0 held `xzyw`, `yzxw` (25, 26), `xzyw` (31), rows 1 and 2 `xzyw` | `dp4(r0/r3/r2 .yxzw, r1.zxyw)` (32-34); `oPos` 36-40 | 35: position set to zero | 489 rows, all outside the scene, depth only; its 8 vertex buffers are all also drawn by `0xD4D5...` in scene |
| `0x9ED0...` | as `0x82F6...` (5-19) | the same pairs (20-31); row 0 held as fetched, `yxzw` (25, 26), as fetched (31), rows 1 and 2 `xzyw` | `dp4(r0.zxyw, r1.zxyw)`, `dp4(r3/r2 .yxzw, r1.zxyw)` (32-34); `r0` 36-40, `oPos = r0` (41) | 35: position set to zero | not in this capture or in `native_discovery_20261002_194918` |
| `0x5F44...` | position `r1` (4; 0), index `r2.x___` (5; 1); rows `r0`/`r3`/`r2` (6-8; 2-4) | none: one bone, weight 1 | `dp4(r0/r3/r2 .zxyw, r1.zxyw)` (9-11); `oPos` 13-17 | 12: position set to zero | 25 rows, all outside the scene, depth only; its one vertex buffer (`0x1C138900`, 240 vertices, the dog's eyes) is drawn in scene by `0xA1F7...` |

All four give `dot(row_k as fetched, (p, 1))` with the pairs of `0xD4D5...`; `0x5F44...` is the depth-only form of `0xA1F7...`. In the older capture `native_discovery_20261002_155634` `0x3A0F...` and `0x82F6...` had in-scene rows (90 of 270 and 60 of 150), so they can reach the clay pass; `user-checks.md` check 10 asks for a capture with stream dumps, and check 11 says what a wrong pose of theirs would look like.

**The `cexec b0` block.** With `b0` set, `0xD4D5...` replaces the blended position by the texture coordinate: `r4.xy = r5.xy` (the `16_16_FLOAT` of instr 13), `r4.z = 0` (instr 69, `sgts` of `-|r0.x|`), so the mesh is drawn flat in its UV space.

- Where the constant is. The device shadows registers `0x4900..0x4927` (8 dwords of bool constants, then the loop constants) at +0x2780, right after the pixel constant bank. `DrawIndexedVertices` flushes it with the register-run writer: `0x8221E314 addi r6,r31,0x2780; li r5,0x4900; lis r4,-0x100; bl 0x8221C908` (a 40-bit mask); the other five draw builders do the same (`0x8221C738`, `0x82206304`, `0x82207F38`, `0x82218164`, `0x8221CC4C`). `xdk_layout.h` `kDeviceBoolConstantsOffset`; vertex `b0` is bit 0 of the first dword.
- Discovery draw rows now carry `"vbool"`, that dword. Capture **`native_discovery_20261006_110021`** (autoplay, bridge scene, 16 frames, every draw): 19,256 draw rows, `vbool` 1 in 32 of them and 0 in all others. The 32 are two consecutive draws per frame outside the main scene, both of the dog's body mesh (`0x1C0F0040`, 9576 indices): one of vertex shader `0xEBA6CC6389B0548D` and one of `0xD4D5...` (pixel shader `0x401A...`). Both shaders have the block; no other row has the bit.
- In scene: `b0` is 0 in 400 of 400 `0xD4D5...` rows (23, 25 or 27 per frame) and in 48 of 48 `0xA1F7...` rows. Outside the scene it is 0 in all 560 `0x3A0F...`, 208 `0x82F6...` and 16 `0x5F44...` rows.
- So the block is not taken by any draw the clay pass records, and the record needs no flag for it. Limits: this is the device shadow, not the GPU register file (the SDK is not changed by this plan). That the bit shows exactly on draws of shaders that test it, in a pass that draws one mesh, is the evidence that the shadow is live; section 9's GPU-side log read 0 for `0xA1F7...` as well. The loop constants in the same shadow read 0 in every row, also for the four shaders with a `loop i0`; that part is not established and is not in the rows.

**The game's own skinning** (the independent check of pairs, row order and component order). The stream the dog's fur shells draw (`0x7C57...`, slot 95, float3, 20 bytes per vertex, 4337 vertices; "Wind and displacement" above found that it holds posed positions and changes every frame) is the game's posed copy of the dog's body mesh: same vertex count and order as the `0xD4D5...` draw of `0x1C0F0040`, and both draws have the same `c0..c3` (32 of 32 frames). How the game fills it was not traced. For each of the 32 frames in which the capture sampled the dog's `0xD4D5...` draw, the entry's positions of all 4337 vertices were compared with that stream:

| Compared with the stream of | Vertices | Distance: median | p99 | Largest |
|---|---|---|---|---|
| the next frame | 138,784 | 0.0000 | 0.0000 | 0.0000 (largest component difference 2.4e-07) |
| the same frame | 138,784 | 0.0009 | 0.0126 | 0.290 |
| the frame before | 138,784 | 0.0018 | 0.0313 | 0.427 |
| two frames later | 138,784 | 0.0008 | 0.0223 | 0.350 |

The stream drawn in frame N + 1 is what the entry computes in frame N, to float rounding, in every vertex (80 of the palette's 90 bones are used). The mesh is 1.4 units long. Controls against the next frame's stream, each a single change to the entry:

| Control | Median | p99 |
|---|---|---|
| bind pose (no skin) | 0.593 | 0.883 |
| pairs (x, x), (y, y), (z, z), (w, w) | 0.324 | 0.747 |
| pairs (y, z), (x, y), (z, x), (w, w) | 0.013 | 0.747 |
| the fourth pair left out | 0.000 | 0.060 |
| row fetches `[7, 9, 8]` / `[8, 7, 9]` / `[8, 9, 7]` / `[9, 7, 8]` / `[9, 8, 7]` | 0.351 / 0.914 / 0.818 / 0.818 / 0.604 | 0.829 / 1.498 / 1.301 / 1.301 / 0.936 |
| row swizzles `xywz` / `yxzw` / `wxyz` / `xwyz` / `wyxz` / `ywxz` | 0.719 / 0.284 / 1.160 / 1.065 / 1.067 / 0.471 | 1.024 / 1.689 / 1.489 / 1.680 / 1.658 / 1.920 |
| `pos_swizzle` `xyw1` / `wxy1` / `yxz1` / `wyx1` / `xwy1` / `ywx1` | 0.719 / 1.160 / 3.591 / 1.065 / 1.067 / 0.471 | 1.024 / 1.489 / 7.988 / 1.680 / 1.658 / 1.920 |

This covers what the offline metrics cannot see: every other row order and component order tried is off by 0.28 units or more at the median.

A second check with a rigid attachment: the dog's eyes are a mesh of `0xA1F7...` (`0x1C138900`, 240 vertices, 0.10 units wide), placed by its own bone and `c0..c3`. In the 16 frames of `native_discovery_20261006_110021`, in world space (`c4..c6` of each draw), an eye vertex is 0.011 units (median; 0.021 at most) from the nearest vertex of the dog's body skinned with the entry, and 0.18 (0.20 at most) from the bind-pose body. The hero has no `0xA1F7...` part in this scene: the save's hero carries no weapon, and the scene's other two `0xA1F7...` draws are parts of one mesh 141 units away, at the hut on the island. So the check the task was written around, the hero's eyes and sword, has no subject here; the dog's eyes are its equivalent.

**The checker and its rule.** `python tools\xdk_sigmatch\position_check.py out\build\win-amd64-release\logs\native_discovery_20261005_103213.jsonl --json docs\native-renderer\vs-transforms.json --vs D4D558DA6A82BDC8 --bones 3`:

```
baseline 0xECD66A10092E6562: share 0.810, accept >= 0.710
VS 0xD4D558DA6A82BDC8 skin: draws 200, in-clip share 0.825 (accept >= 0.710) ACCEPT, passed 165, unreadable 0, unsupported 0, bad-index 0, rows 826, bone ortho share 0.9755 (1392 of 1427, >= 0.90), max 0.764, edge ok share 0.998 (>= 0.98) of 95474 edges, edge nan 0, edge draws 200, weight sum min/median/max 1.000/1.000/1.000, bone index max 87 / palette bones 90
  bone use: deviation 0.764, frame 15, vb 0x1C13A380, indices 0+1200, bone 2 of 3, row lengths 0.236 0.999 0.998
  bone use: deviation 0.752, frame 6, vb 0x1C13A380, indices 0+1200, bone 2 of 3, row lengths 0.248 0.998 0.997
  bone use: deviation 0.618, frame 50, vb 0x1C13A380, indices 0+1200, bone 2 of 3, row lengths 0.382 0.997 0.993
```

Over all 826 rows (`--max-draws 100000`) the line reads `in-clip share 0.821 (accept >= 0.702) ACCEPT`, `bone ortho share 0.9723 (5571 of 5730, >= 0.90), max 0.764`, `edge ok share 0.998 (>= 0.98) of 387350 edges`. The in-clip share is the bind-pose entry's (0.825).

A bone use is one palette bone that one sampled draw's vertices reference with a nonzero weight: a bone counts once per draw and again in every other draw that uses it. A bone is orthonormal when the largest of `| |row| - 1 |` and `|row_i . row_j|` over its 3x3 part, under the entry's row swizzles, is at most 0.05.

- Over all 826 rows there are 5730 bone uses. 5539 of them, every bone but one, deviate by at most 0.00072 (median 0.0004).
- The other 191 are bone 2 of the small birds' mesh (`0x1C13A380`, 3-bone palette), in every one of its rows. Its row 0 has length 0.24 to 1.00 (median 0.63) while rows 1 and 2 have length 0.98 to 1.00: the game scales that bone along one axis. The same bone (row lengths 0.630, 0.998, 0.998) is in 71 rows from frame 14 to frame 119, probably a perched bird; in other rows the factor changes from frame to frame. 159 of the 191 deviate by more than 0.05. The mesh is drawn about six times a frame, which is why one bone makes 159 of the 5730 uses fail (2.8%).
- No bone has a negative determinant. Of the 139 distinct (vertex buffer, bone) pairs, 138 are orthonormal in every use.

The rule went through three forms while this entry was checked, and `0xD4D5...` is what changed it:

| Form | Gate | `0xD4D5...` | Why it was replaced |
|---|---|---|---|
| Task 4 | the largest deviation over every bone used, at most 0.05 | 0.764: REJECT | no reading can make a bone the game scales orthonormal |
| first amendment | at least 0.98 of the bone uses within 0.05 | 0.9755 (0.9723 over all rows): REJECT | it was set on a misread count, as if the scaled bone were one use; it is one bone of a mesh drawn six times a frame |
| in force (`ORTHO_SHARE_MIN`) | at least 0.90 of the bone uses within 0.05; the maximum is printed, not judged | ACCEPT | |

What the gate has to separate is far apart: a correct skin scores 0.972 to 0.976 here and 1.0000 for `0xA1F7...` (334 of 334), and every reading with a wrong row swizzle or row set that was measured scores exactly 0 (`0xD4D5...` rows read `yxzw`: 0 of 1427; `0xA1F7...` rows read `yxzw` or `yzxw`, or row fetches `[6, 7, 7]`: 0 of 334). The share depends on the scene, on how many draws use a scaled bone; 0.90 is a judgement about scenes not yet captured.

| Control (`--entry`, 200 draws) | In-clip share | Bone ortho share | Max | Edge ok share | Verdict |
|---|---|---|---|---|---|
| the entry | 0.825 | 0.9755 (1392 of 1427) | 0.764 | 0.998 | ACCEPT |
| pairs (x, x), (y, y), (z, z), (w, w) | 0.825 | 0.9761 (1429 of 1464) | 0.764 | 0.840 | REJECT (edges) |
| pairs (x, w), (y, x), (z, y), (w, z) | 0.825 | 1.0000 (1086 of 1086) | 0.001 | 0.935 | REJECT (edges) |
| row fetches `[8, 7, 9]` | 0.825 | 0.9755 | 0.764 | 0.998 | ACCEPT |
| row fetches `[9, 7, 8]` | 0.825 | 0.9755 | 0.764 | 0.998 | ACCEPT |
| row swizzles `xywz` | 0.825 | 0.9755 | 0.764 | 0.925 | REJECT (edges) |
| row swizzles `yxzw` | 0.830 | 0.0000 (0 of 1427) | 1.186 | 0.759 | REJECT (bones, edges) |
| `pos_swizzle` `xyw1` | 0.825 | 0.9755 | 0.764 | 0.925 | REJECT (edges) |
| `pos_swizzle` `wxy1` | 0.825 | 0.9755 | 0.764 | 0.851 | REJECT (edges) |

The checker accepts the two permutations of the rows: they keep every bone orthonormal and every edge rigid, as Task 4 found. They are ruled out by the dump reading and by the game's posed stream above, where they are 0.914 and 0.818 units off at the median.

The figures above pool every mesh, and the posed-stream check is on the dog. For the hero the Task 9 review gives per-mesh numbers (the reviewer's, not reproduced in Task 10: `position_check.py` has no per-buffer option): the edge share is 0.994 to 1.000 for each of the twelve vertex buffers `0xD4D5...` draws, and on the hero's body buffer `0x1BF18DC0` the wrong pairings score 0.677 to 0.886.

**Regression case.** `test_bone_skin.cpp` `RealDraw` (cases 90-108): eight vertices of the dog's draw in frame 3 of the capture with one, two, three and four influences (224 bytes of vertices, the palette cut after bone 26), printed by `position_check.py`'s `cpp_fixture()`. The test selects the skin from the shader's 19 fetches with the table's arguments, skins the vertices and compares them within 1e-5 with the checker's positions and with the game's posed stream of the next frame; it also checks that the bind pose, the straight pairing, the first two rows exchanged and the rows read `yxzw` give other positions.

**Runtime.** The weighted path of Task 2 and Task 8 (`SelectSkin` with pairs, `SkinPositions` with four influences, the index and weight words under 8in32) needed no change: it reproduces the fixture at once and the runs below show no skip. One fix folded in from Task 8's review: a palette that holds no whole bone made `SkinPositions` fail in the renderer, counted `render-other`. `PaletteBones` (`bone_skin.h`, tests 80-86) counts the whole bones of a palette, and `FillDrawInputs` skips the draw as `skin-unsupported` when there is none.

**Measured.** `.\tools\drive_game.ps1 -Total 120 -Shots "70,95" -GameArgs "--fable2_native_render=true","--fable2_native_view=split"`: before the entries `fable_2_176.log`; with them `fable_2_177.log` (split) and `fable_2_178.log` (native view, `-Total 110 -Shots "95"`).

```
before [native] capture: frame 2700 captured 987 drawable 951 (deformed 223) skipped {no-transform: 6, unsupported-prim: 30} nested_total 0 | ... | textured 323 untextured by reason {terrain: 567, no-albedo: 61}
before [native] clay: drawn 951 (deformed 223) of 951 drawable, skipped_bad_index 0 other 0 | textured 323 of 951 | 2 uploads, 2223 hits, 24.4 MB resident, instanced 135, skinned 3 | hash 0.63 ms, decode 0.03 ms, record 0.25 ms (max total 1.83 ms over 300)
with   [native] capture: frame 2700 captured 987 drawable 951 (deformed 194) skipped {no-transform: 6, unsupported-prim: 30} nested_total 0 | ... | textured 323 untextured by reason {terrain: 567, no-albedo: 61}
with   [native] capture: frame 2700 drawable by vs {..., 0x475EC9F795E5EDBB(deformed): 30, 0xD4D558DA6A82BDC8(skin): 29, 0xBEAD84BD72072E0E: 24, 0xA5846836C90E1192(deformed): 22}
with   [native] clay: drawn 951 (deformed 194) of 951 drawable, skipped_bad_index 0 other 0 | textured 323 of 951 | 5 uploads, 2220 hits, 25.1 MB resident, instanced 135, skinned 32 | hash 0.56 ms, decode 0.15 ms, record 0.21 ms (max total 2.22 ms over 300)
```

At frame 2700 both runs have 29 `0xD4D5...` draws: `skinned` goes from 3 to 32 and `deformed` from 223 to 194. Captured, drawable, instanced, textured and the skips do not change; `skin-unsupported` appears in none of the logs and the clay pass skips no draw (`other 0` in every window). Over the windows `skinned` is 28 to 34 (25 to 31 `0xD4D5...` draws, as birds come and go) and `deformed` stays 194.

Cost, with about 29 skinned draws: 30.0 fps in every 300-frame window after the world is up, in all three runs. Clay pass in the steady state: decode 0.10 to 0.28 ms (0.02 to 0.04 before), 3 to 7 uploads per logged frame (2), hash 0.36 to 0.68 ms (0.36 to 0.67), slowest frame of a window 2.0 to 2.4 ms (1.8 to 2.0), geometry resident 25.0 to 25.2 MB (24.4), capture median 1.14 to 1.15 ms (1.14 to 1.17), guest work median 7.4 to 7.7 ms (7.6 to 7.9). Single slow frames: the one near scene frame 3016 is in all three runs (5.1 ms before, 5.2 and 4.1 ms with), and the split run has one of 8.0 ms in the window that ends at scene frame 1516, which neither the native run nor the earlier trial runs of the same entries (`fable_2_174.log`, `fable_2_175.log`: decode 0.10 to 0.18 ms, slowest frame 1.7 to 3.1 ms) have; its cause was not looked for.

Screenshots (window shots of the committed build, against the view-off shot of the same autoplay scene at 95 s and the Task 7b native-view shot; copies in `logs\shots\task9_native` and `logs\shots\task9_split`):

- **Native view, 95 s.** The hero stands as in the emulated frame, seen from behind: arms hanging at his sides with the elbows slightly bent, gloved hands beside the thighs, legs together. In the Task 7b shot his arms are spread wide in the bind pose. The dog sits where the emulated dog sits; before it stood on four legs beside the white outline of its own fur shells. The crow on the right-hand railing is perched with folded wings, the emulated silhouette; before its wings were spread. Nothing is smeared or exploded and nothing new floats in the scene.
- **Split, 70 s and 95 s.** The split runs through the hero: his clay right half continues the emulated left half at the shoulder, the belt, the leg and the boot. Between the two shots the clay half changes in the dog (about 10,600 pixels) and slightly in the hero (about 200), so the clay characters are re-posed as the palettes change.

**Known limitations.**

- **The dog's fur shells.** In clay the dog is a blotch: mostly white, with dark patches of its textured body showing through, and the pattern changes from frame to frame. Measured: the shells are the 15 draws per frame of `0x7C5710DEF3EE33C4` ("Wind and displacement" above), which has a transform entry and a `"uv"` entry; their only pixel shader, `0x014F8A02DB7B19CA`, is `no_albedo` in `ps-albedo.json`, and the log lists them as `untextured by ps {..., 0x014F8A02DB7B19CA(no-albedo): 15, ...}`. So they are drawn as opaque untextured clay, without their push along the normal, and since the body is posed they lie on it and fight it for depth; before, body and shells were two separate shapes. The pixel shader's colour fetches tf14 and tf15 are two 128 x 128 textures (at `0x133E1000` and `0x133F1000` in `native_discovery_20261006_110021`) that read all zero in guest memory (the shader's `no_albedo` record in `ps-albedo.json`). Inference, not verified: they are render targets the game fills on the GPU each frame, and the UV-space pass of the dog's mesh (the two `b0` draws above) is what fills them; the rows do not record the render target. In the game the shells are fur layers over the body, presumably translucent; the clay pass has no blending and no alpha test (non-goals of this sub-project). Deleting the `0x7C57...` transform entry would hide them (15 draws become `no-transform`, drawable 951 to 936 of 987); that was not done.
- **The hero's eyes and a sword were not judged.** The autoplay camera is behind the hero, the save's hero carries no weapon, and he has no `0xA1F7...` part in the scene. The dog's rigid eye mesh was measured instead (0.011 units from the skinned body against 0.18 from the bind-pose body). `user-checks.md` check 11.
- **Cost in a crowd was not measured.** The figures above are for about 29 skinned draws; skinning is per vertex on the CPU, re-done for a mesh whenever its palette bytes change.
- **Four shaders rest on their dump readings alone** (`0x3A0F...`, `0x82F6...`, `0x9ED0...`, `0x5F44...`): no stream dumps, no `position_check.py` line, no screenshot. The posed-stream check covers `0xD4D5...` only. `user-checks.md` checks 10 and 11.
- **The offline metrics accept a permuted skin** (controls table). For `0xD4D5...` the game's posed stream closes that gap; a future skin entry without such a stream has the dump reading and the screenshot only.

### Validation (plan Task 10)

Autoplay validation, 2026-10-06, release build of commit 5de47df (`fable_2.exe` written 11:38:01; the later commits change the plan document only), `tools\drive_game.ps1` 120 s runs, no other input sent. The autoplay save loads the bridge scene (quest "The Birth of a Hero") and the camera does not move, so everything below is one scene from one viewpoint; town, field, interior, motion and a crowd are user checks.

**The runs.** Someone was using the machine during this validation (measured, below), and the game world froze in six of today's seven runs. A window of 300 frames is called live when the guest paces on the swap (30.0 fps, swap median 24 to 27 ms) and frozen when every window logs the same captured and drawable counts, the clay pass has 0 uploads and 0.00 ms decode (no vertex or palette byte changes: nothing animates) and the guest runs at 23 to 26 fps with a work median of 39 to 41 ms and no swap wait. Only live windows are used for figures. Because today's split run froze, the split figures and the split screenshots are Task 9's run of the same `fable_2.exe`, `fable_2_177.log` (started 11:38:02), re-read for this section; `fable_2_178.log` is Task 9's native-view run of it.

| Log | Started | Run | Live windows after the world is up | Used |
|---|---|---|---|---|
| `fable_2_177.log` | 11:38 | split, `-Shots "70,95"` (Task 9) | 7 of 7 | split figures and shots |
| `fable_2_178.log` | 11:40 | native, `-Total 110 -Shots "95"` (Task 9) | 6 of 6 | native figures |
| `fable_2_179.log` | 12:01 | renderer off, `-Env @{FABLE2_GUEST_WORK_LOG="1"}` | 0: frozen from about 50 s | the freeze only |
| `fable_2_180.log` | 12:04 | renderer off, `-Shots "95"` | 3, frozen from about 77 s; the 95 s shot shows the F3 Debug window open and the camera in another place, so input reached the game | fps windows only |
| `fable_2_181.log` | 12:08 | renderer off, `-Shots "95"` | 2, frozen from about 72 s | renderer-off figures |
| `fable_2_182.log` | 12:10 | view off, `-Shots "95"` | 4, frozen from about 91 s | view-off figures, reference shot |
| `fable_2_183.log` | 12:13 | split, `-Shots "70,95"` | 0: frozen from about 47 s | the freeze only |
| `fable_2_184.log` | 12:17 | native, `-Shots "95"` | 7 of 7; from 114.7 s the drawn set changes (input arriving with the game in the foreground) | native figures and shot, to scene frame 3016 |
| `fable_2_185.log` | 12:19 | renderer off, `-Shots "95"` | 0: frozen from about 50 s | the freeze only |

The split, view-off and renderer-off runs were not repeated: two waits, of 7 and 9.5 minutes, for the machine to go 60 s (then 45 s) without input ended with a longest idle stretch of 16 s and 29 s.

The time a freeze begins is read from the log's once-a-second `vsync present gate` lines: about 30 host presents a second while live, 25 to 26 frozen, and two to five irregular seconds between, sometimes with a second or two without frames.

Commands: `.\tools\drive_game.ps1 -Total 120 [-Shots ...] -GameArgs "--fable2_native_render=false" -Env @{FABLE2_GUEST_WORK_LOG="1"}`, `-GameArgs "--fable2_native_render=true","--fable2_native_view=off"`, `...,"--fable2_native_view=split"`, `...,"--fable2_native_view=native"`. Shots of the runs used are in `logs\shots\task10_native` and `logs\shots\task9_split`; those of the other runs in `logs\shots\task10_discarded`.

**The freeze.** It is the "unexplained event" of section 10 (`fable_2_133.log`: the same draws every frame, guest work near 39 ms, no swap wait), now seen in seven more runs: the six above and Task 8's split run `fable_2_172.log`, frozen from about 61 s, which was not noticed then (its frame-2700 figures, captured 981 and drawable 949, are the frozen frame's; the same frozen counts as `fable_2_183.log`). What was measured:

- For `fable_2_183.log`, `fable_2_184.log` and `fable_2_185.log` a sampler beside the run recorded once a second which process owned the foreground window and how long ago the machine last received an input event (`GetForegroundWindow`, `GetLastInputInfo`; it sends nothing and touches no window).
  - `fable_2_184.log`, live: the game was the foreground window from 3 s to the end, and the machine received no input from 172 s before the run until about 115 s into it. At that input the drawn set starts to change, as it would when the camera turns: the last clay window has 882 drawn and 511 terrain draws against 949 and 567, and textures upload from 114.7 s.
  - `fable_2_183.log` and `fable_2_185.log`, frozen: another program owned the foreground window in every sample and the game never did (in `fable_2_183.log` the terminal, `warp`; in `fable_2_185.log` that process and, for the first half minute, another), and input was arriving: in 98 of 125 samples for `fable_2_185.log`; for `fable_2_183.log` at 35 to 37 s and 41 to 46 s of the log's time, the last event at 46.2 s and the freeze beginning between 46.4 and 47.4 s.
- `fable_2_181.log` and `fable_2_182.log` had no sampler; their 95 s window shots show the title bar in its inactive colour. The shots of the live runs show it active.
- No warning, error or other log line marks the onset, and the frozen frame shows no menu or message: the shots look like the live scene.
- The renderer is not needed for it: four of the frozen runs had `--fable2_native_render=false`.

What this supports: the world froze in every run in which the game window was seen out of the foreground or input was arriving at the machine, and in none of the three live runs (`fable_2_184.log`: the game measured in the foreground with no input arriving; `fable_2_177.log` and `fable_2_178.log`: active title bar in the shots, nothing else known). Being out of the foreground and input arriving came together in every case, so the data cannot say which of them matters, or whether something else the person did does. What it does not show is the mechanism. The pad drivers of `src\input` read the keyboard only while the game is the foreground window (`keyboard_gamepad.h`), no host code was found that pauses the guest when focus is lost (`ReXApp::OnWindowFocusChanged` is not overridden; the SDK's pad drivers then report an untouched pad), and whether focus alone does it was not tested: autoplay must not touch the foreground window. `user-checks.md` check 15.

**Against the success criteria.**

| Success criterion (spec) | Autoplay measurement (bridge scene) | Result |
|---|---|---|
| 1. At least 90% of main-scene non-terrain draws drawn | `(drawable - terrain) / (captured - terrain)`: 0.914 to 0.924 over the 20 logged frames from 1500 on of `fable_2_177.log`, `fable_2_178.log` and `fable_2_184.log`. `fable_2_184.log`: frame 1500 386 / 418 = 0.923, frame 2100 386 / 421 = 0.917, frame 2700 384 / 418 = 0.919, frame 3000 382 / 416 = 0.918; `fable_2_177.log` frame 2700 384 / 420 = 0.914. Terrain is 567 in every one of these frames: the three `(terrain)` shaders of the `drawable by vs` line, 340 + 152 + 75, which is also the `terrain: 567` of the untextured list; none of the `no-transform` skips is terrain (the `unsupported-prim` shaders are not logged; a skipped terrain draw would only raise the share) | met for this scene; Bowerstone streets: user check 14 |
| 2. Split and overlay screenshots show the hero and the dog in their animated pose with eyes and sword attached | pose met for the bridge scene (hero from behind in one idle stance, dog, crow, continuity across the split seam, the dog's rigid eye mesh numerically 0.011 against 0.18 units); the hero's eyes, a sword, motion, a crowd and the overlay view are not evidenced by autoplay and are user check 11 | partly evidenced, not closed |
| 3. Screenshots show instanced meshes lined up with the emulated image | placement met for this scene: no clay instanced mesh where the emulated image clearly has none (native-view shot at 95 s against the view-off shot). The silhouettes differ, because the clay pass has no alpha test: every card is an opaque rectangle. The four near-field shaders could not be judged from the bridge | met for placement in this scene; user checks 9 and 12 |
| 4a. 30 fps with a clay view on | 30.0 fps in every live window after the world is up: native view 7 of 7 (`fable_2_184.log`) and 6 of 6 (`fable_2_178.log`), split 7 of 7 (`fable_2_177.log`). Today's split run has no live window | met in the undisturbed runs; see "Frame rate" |
| 4b. View-off capture overhead under 0.5 ms per frame | capture median 0.045 ms in each of the four live windows (frames 1500 to 2400, `fable_2_182.log`; p90 0.048 to 0.096 ms, slowest frame 0.116 to 0.364 ms) (four live windows of one run made while the machine was in use; measured by the capture's own timer, no clean rerun) | met |
| 5. User checks | none done | pending: `user-checks.md` checks 7 to 15 |

**Logs** (`fable_2_184.log`, native view, frame 2700; `fable_2_182.log`, view off, frame 2400, the last live window; `fable_2_181.log`, renderer off):

```
native   [native] capture: frame 2700 captured 985 drawable 951 (deformed 194) skipped {no-transform: 4, unsupported-prim: 30} nested_total 0 | records on | capture 1.174 ms (median 1.200, p90 1.860, max 2.041 over 300 frames) | textured 323 untextured by reason {terrain: 567, no-albedo: 61}
native   [native] capture: frame 2700 untextured by ps {0x7CD57B81550F19E3(no-albedo): 22, 0x014F8A02DB7B19CA(no-albedo): 15, 0xA17D8AEC3A817D45(no-albedo): 12, 0xF6D98C7B4D98438B(no-albedo): 12}
native   [native] capture: frame 2700 unsupported by hook {D3DDevice_BeginVertices?: 16, DrawIndx:82217EE8: 10, DrawIndx2:821EF988: 4}
native   [native] capture: frame 2700 no-transform by vs {0x29B6506FBACEB93A: 3, 0x775C6085FBB9D676: 1}
native   [native] capture: frame 2700 drawable by vs {0xC30A97D946FA2BE4(terrain): 340, 0xFB68A7F2301210E1(terrain): 152, 0xECD66A10092E6562: 122, 0x5003700B7C9B1C16(terrain): 75, 0x8123C16DBF583F92(instanced): 73, 0xB636821F95DC9D8E(instanced): 38, 0x475EC9F795E5EDBB(deformed): 30, 0xD4D558DA6A82BDC8(skin): 29, 0xBEAD84BD72072E0E: 24, 0xA5846836C90E1192(deformed): 22}
native   [native] clay: drawn 951 (deformed 194) of 951 drawable, skipped_bad_index 0 other 0 | textured 323 of 951 | 5 uploads, 2220 hits, 25.1 MB resident, instanced 135, skinned 32 | hash 0.72 ms, decode 0.12 ms, record 0.33 ms (max total 2.56 ms over 300) | scene frame 2716
native   [native] clay: Textures: textured 323 of 951 drawn (33%, 84% non-terrain), resident 69 (16.1 MB), uploads 0 (0.0 MB), decode 0.53 ms | top untextured: terrain 567, no-albedo 61
view off [native] capture: frame 2400 captured 985 drawable 0 (deformed 0) skipped {} nested_total 0 | records off | capture 0.044 ms (median 0.045, p90 0.049, max 0.116 over 300 frames) | textured 0 untextured by reason {}
view off [frame] guest 300 frames: 30.0 fps, frame median 33.30 ms, work median 6.47 ms (p90 7.10), swap median 26.71 ms, wait median 0.00 ms
off      [frame] guest 300 frames: 30.0 fps, frame median 33.37 ms, work median 6.27 ms (p90 6.87), swap median 27.08 ms, wait median 0.00 ms
```

The scene repeats from run to run frame for frame: `fable_2_177.log`, `fable_2_178.log` and `fable_2_184.log` log the same drawn and skinned counts at the same scene frames (953 and 34 at 1516, 947 and 28 at 1816, 953 and 34 at 2116, 951 and 32 at 2416 and 2716, 949 and 30 at 3016). `skinned` is consistent with the `0xD4D5...` count plus the 3 `0xA1F7...` draws: the capture line of frame 2700 has 29 and the clay line of scene frame 2716 has 32, and the same holds at 1500, 2100, 2400 and 3000; at 1800 the two lines read 23 and 28. They are logged 16 frames apart and birds come and go, so this is a consistency check, not a count of one frame. `instanced` is 135 throughout. `skin-unsupported`, `instance-unsupported` and `bad-index` appear in none of today's logs, and the clay pass skips nothing (`skipped_bad_index 0 other 0`).

**Cost** (live windows after the world is up; ranges over those windows):

| | Renderer off | View off | Split | Native view | Native view |
|---|---|---|---|---|---|
| Log | `fable_2_181.log` | `fable_2_182.log` | `fable_2_177.log` | `fable_2_178.log` | `fable_2_184.log` |
| Live windows | 2 | 4 | 7 | 6 | 6 (to scene frame 3016) |
| Guest fps | 30.0 | 30.0 | 30.0 | 30.0 | 30.0 |
| Guest work median, ms | 6.27, 6.33 | 6.44 to 6.62 | 7.42 to 7.68 | 7.39 to 7.52 | 7.99 to 8.77 |
| Capture median, ms | - | 0.045 | 1.137 to 1.152 | 1.138 to 1.144 | 1.166 to 1.227 |
| Clay hash, ms | - | - | 0.36 to 0.68 | 0.47 to 0.66 | 0.39 to 0.73 |
| Clay decode, ms | - | - | 0.15 to 0.28 | 0.10 to 0.24 | 0.10 to 0.22 |
| Clay record, ms | - | - | 0.15 to 0.31 | 0.15 to 0.30 | 0.14 to 0.33 |
| Slowest clay frame of a window, ms | - | - | 1.97 to 2.32, one 5.22, one 8.00 | 1.96 to 2.37, one 4.12 | 2.03 to 2.56, one 4.46 |
| Frame that first builds the scene, ms | - | - | 235 | 237 | 318 |
| Geometry resident, MB | - | - | 25.0 to 25.1 | 25.1 to 25.2 | 25.1 |

- Windows in each column: renderer off, the two before the freeze (ending at 53.6 and 63.6 s); view off, the four before the freeze (53.5 to 83.5 s, frames 1500 to 2400); split, all seven (scene frames 1516 to 3316); native `fable_2_178.log`, all six of its 110 s run (1516 to 3016); native `fable_2_184.log`, the six up to scene frame 3016, without the seventh, in which the drawn set changes. So the columns do not cover the same stretch of the scene.
- Renderer off against view off: 6.27 and 6.33 ms against 6.44 to 6.62 ms, on two and four windows of two runs made while the machine was in use. Task 9's renderer-off run `fable_2_173.log` (a discovery capture; its five live windows outside the capture) has 6.36 to 6.76 ms. So the view-off cost is not separable from run-to-run variation here; the capture's own timer says 0.045 ms. Section 11 measured 5.92 ms with the renderer off in this scene (`fable_2_148.log`).
- `fable_2_184.log` ran with the first version of the sampler beside it (a PowerShell job listing processes twice a second) and on a machine that had just been in use; its guest work is 0.5 to 1.3 ms above `fable_2_178.log`'s and its work p90 reaches 11.6 ms against 8.2 ms. Its clay-pass times are inside the ranges of the other two runs.
- **Skinning.** With 28 to 34 skinned draws the clay decode is 0.10 to 0.28 ms per frame over the 19 live windows of the three runs, against 0.02 to 0.04 ms with 3 skinned draws before the entries (`fable_2_176.log`, seven windows). The difference is the re-skinning: in the frozen run `fable_2_183.log`, where no palette changes, the same 30 skinned draws cost 0.00 ms with 0 uploads in every window. Uploads are 3 to 7 per logged frame against 2.
- Textures: 317 to 327 of 945 to 955 drawn draws textured, 83 to 84% of the non-terrain ones; 69 textures, 16.1 MB resident; no upload after the burst at world load until the drawn set changes at 114.7 s.

**Frame rate** (the question left open after Task 7b: three runs in a row, `fable_2_169.log` to `fable_2_171.log`, had one or two 300-frame windows at 29.8 fps, one of them with the view off). Every window after the world is up in today's four runs:

| Run | Window ends at (s): guest fps |
|---|---|
| Renderer off, `fable_2_181.log` | 53.6: 30.0, 63.6: 30.0, 75.1: 25.9 (the freeze begins), 87.5: 24.3, 99.3: 25.3, 111.3: 25.0 |
| View off, `fable_2_182.log` | 53.5: 30.0, 63.5: 30.0, 73.5: 30.0, 83.5: 30.0, 97.6: 23.2 (the freeze begins), 110.4: 23.4 |
| Split, `fable_2_183.log` | 56.6: 23.4 (frozen from about 47 s), 68.9: 24.5, 81.1: 24.5, 93.3: 24.6, 105.5: 24.6, 117.7: 24.6 |
| Native, `fable_2_184.log` | 54.5, 64.5, 74.5, 84.5, 94.5, 104.5 and 114.5: 30.0 each |

- No window reads 29.8. Today's windows are 30.0 (13 live ones, and 3 more in `fable_2_180.log`) or 23 to 26 (frozen, or the window in which the freeze begins). The window that ends near 44 s is the load in every run (26.7 to 28.1 fps) and is not counted.
- Over every log from `fable_2_164.log` to `fable_2_185.log`, 29.8 occurs four times, all in `fable_2_169.log` (2), `fable_2_170.log` (1) and `fable_2_171.log` (1): three runs started within seven minutes (at 10:03:51, 10:07:59 and 10:09:59; the last ended at 10:11:49). The four windows end at 10:04:56, 10:05:26, 10:09:43 and 10:11:43. The builds after them all contain Task 7b's change, and `fable_2_174.log` to `fable_2_178.log` and `fable_2_184.log` have 40 live windows without one. So the windows do not follow the code.
- A 29.8 window is 300 frames in about 10.07 s, two frame periods lost somewhere in it; its frame, work and swap medians and its work p90 are those of a 30.0 window (`fable_2_169.log`: 7.59 ms work median, p90 8.18).
- One of the four is in the view-off run, where no records are built and nothing is drawn by the native renderer. None was seen with the renderer off, on ten live windows (`fable_2_173.log` 5, `fable_2_180.log` 3, `fable_2_181.log` 2), none of them from that stretch.
- The same logs hold larger disturbances that were not reported when they were made: the freeze in `fable_2_172.log`, and one window at 26.4 fps with normal medians in `fable_2_165.log` (about 1.4 s lost).
- What the data supports: the 29.8 windows are confined to one eight-minute stretch, do not depend on the view, and did not recur; these runs are sensitive to what else the machine is doing, which today was measured to be a person using it. What the machine was doing between 10:03 and 10:12 is not in any log. The windows are not explained.

**Slow frames.** The slowest clay frame of each window (`max total`), by the scene frame the window ends at:

| Log | 1516 | 1816 | 2116 | 2416 | 2716 | 3016 | 3316 |
|---|---|---|---|---|---|---|---|
| `fable_2_177.log` (split) | 8.00 | 2.32 | 1.99 | 1.97 | 2.22 | 5.22 | 2.00 |
| `fable_2_178.log` (native) | 2.37 | 2.08 | 1.96 | 2.12 | 1.98 | 4.12 | - |
| `fable_2_184.log` (native) | 2.45 | 2.43 | 2.03 | 2.45 | 2.56 | 4.46 | (12.61, drawn set changing) |

- **The frame near scene frame 3016 recurs**: 4.46 ms today. It is in every live run whose instanced draws are textured, eleven of eleven (`fable_2_166.log` to `fable_2_170.log`, `fable_2_174.log` to `fable_2_178.log`, `fable_2_184.log`: 4.12 to 7.12 ms), always in the window that ends at scene frame 3016 to 3018, and in neither run without instanced UVs (`fable_2_164.log`, no instancing entries: 1.48 ms; `fable_2_165.log`, entries without `"uv"`: 1.74 ms).
- The scene repeats frame for frame (above), so something the game does between scene frames 2716 and 3016 is the likely trigger; that is an inference from the recurrence. In that window `0x8123...` goes from 73 to 70 drawable draws and `0xD4D5...` from 29 to 27 (`drawable by vs` at frames 2700 and 3000), which fits the reviewer's lead that changing instance batches rebuild flat streams. But the counts also change in windows without a slow frame, and by more (`0x8123...` 80, 79, 79, 78, 73 at frames 1500 to 2700 of `fable_2_184.log`), and the log cannot confirm it: the `clay` line gives uploads and decode for the window's last frame only (6 uploads and 0.16 ms at 3016, like any other window), and no `[native] textures: burst` line falls in the window, so no texture was uploaded.
- The 95 s window shot is taken in the same window, 0.1 to 0.8 s after it begins (file times of the shots against the `clay` lines of `fable_2_177.log`, `fable_2_178.log` and `fable_2_184.log`). A shot alone does not cause such a frame: the 70 s shots of the three split runs with instanced UVs leave no mark in their windows (2.04, 1.75 and 1.99 ms in `fable_2_167.log`, `fable_2_170.log` and `fable_2_177.log`), and `fable_2_165.log`, without `"uv"`, had the 95 s shot in the same window and 1.74 ms. A run without a shot at 95 s and with instanced UVs was not made, so the shot is not excluded as a part of the cause.
- **The 8.00 ms frame near scene frame 1516 did not recur**: 2.45 ms today, 2.37 ms in `fable_2_178.log`; it is in one of the fourteen runs that have that window (1.42 to 2.60 ms in the other thirteen). The two windows in which a freeze begins have a frame of 6.67 ms (`fable_2_172.log`) and 4.15 ms (`fable_2_183.log`); nothing shows that the 8.00 ms frame was of that kind.
- Both stay unexplained. Neither shows in the guest frame rate: the windows that contain them are at 30.0 fps.

**Screenshots** (window shots; each was opened and compared with the emulated frame of the same scene: `logs\shots\task10_discarded\fable_2_182_viewoff_95s.png`, the view-off run, world frozen since about 91 s, and Task 7b's live view-off shot `logs\shots\task7b_off\shot_95s.png`).

- **Native view, 95 s** (`logs\shots\task10_native\shot_95s.png`, `fable_2_184.log`).
  - The hero stands where the emulated hero stands, seen from behind, in the same stance: arms hanging with the elbows slightly bent, gloved hands beside the thighs, feet together; shirt, waistcoat, striped trousers and boots textured. Nothing is stretched, detached or left in the spread-arm bind pose.
  - The dog sits where the emulated dog sits, to the right of the hero. It is worse to look at than before the skinning entries: a white shape with a few dark patches, where the emulated dog is dark brown. That is the 15 fur-shell draws, opaque and untextured, lying on the posed body ("Known limitations").
  - The crow on the right-hand railing is a small dark perched bird with folded wings, in the place of the emulated one.
  - Left of the bridge: ferns stand between the fence posts beside the big trunk, where the emulated ferns are, and a band of grass runs along the edge of the path. Right bank: a row of leafy cards behind the fence, where the emulated frame has low bushes. Every card carries its texture on a black rectangle (no alpha test), so the plants read as dark blocks, larger than the emulated plants.
  - The big trunk, the birches and their leaf cards are in the emulated places; the leaf cards are pale untextured clay and the large leaves at the top left are flat pale cards.
  - Not in the clay frame: the lake (pale clay terrain where the water is), the sky, the distant forest, the sparkles on the deck, the quest text and the HUD.
  - Cannot be judged from this shot: the hero's face and eyes (he is seen from behind), a weapon (he carries none), the four near-field instancing shaders (their plants are under and beside the bridge), the small bird flock, and anything in motion.
- **Split, 70 s and 95 s** (`logs\shots\task9_split`, `fable_2_177.log`; today's split shots are of the frozen world and were not used).
  - The seam runs through the hero: the clay right half continues the emulated left half at the head, the shoulder, the belt, the leg and the boot, in both shots.
  - The dog is in the clay half, sitting, mostly white at 95 s and with more of the dark textured body showing at 70 s; the pattern changes between the shots, as the shells and the body fight for depth.
  - The crow, the right-bank row of cards and the grass under the planks are in the clay half; the ferns, the grass band and all trees are in the emulated half, which is why they are judged in native view.
- **Overlay view**: no shot was taken; alignment while the camera moves is a user check (8, 9, 11, 12).

**Remaining skips** (bridge scene, per frame; `fable_2_184.log`, the same in `fable_2_177.log` and `fable_2_178.log`):

| Reason | Draws | What | Next action |
|---|---|---|---|
| `unsupported-prim` | 30 | `D3DDevice_BeginVertices?` 16, `DrawIndx:82217EE8` 10, `DrawIndx2:821EF988` 4 (`unsupported by hook`) | A non-goal of this sub-project (billboards, particles, point lists, the two unmapped draw builders). They are 7% of the non-terrain draws here and the reason the share is 0.92, not higher: map the two builders and the immediate-mode vertices in a later sub-project |
| `no-transform` | 2 to 6 | `0x29B6506FBACEB93A` 1 to 5 and `0x775C6085FBB9D676` 1: billboards, corners computed from relative constants | With the particles; needs the corner maths, not a table entry |
| `instance-unsupported`, `skin-unsupported`, `bad-index` | 0 | not seen in any run | - |

Not skipped but untextured: `terrain` 567 (clay by design) and `no-albedo` 61: `0x7CD57B81550F19E3` 22 (leaf clumps of `0xA584...`; its colour fetch has an untraceable UV), `0x014F8A02DB7B19CA` 15 (the fur shells; its colour textures read all zero in guest memory), `0xA17D8AEC3A817D45` 12 and `0xF6D98C7B4D98438B` 12 (section 11). Next action for the first two: trace the UV of `0x7CD5...` by hand as was done for the instancing shaders; the shells need the translucency follow-up below, not a texture.

**Known limitations.**

- **Wind sway and the push away from characters are not animated.** 194 of the 951 drawn draws are `deformed`: trees and plants stand still while the emulated ones move, and the leaf clumps of `0xA584...` are drawn at the stored position, not the one the shader rebuilds about its pivot. The sway itself was not measured and the error has no bound. For `0xA584...` the lever is known: the wind turns a vertex about its pivot at the distance `|P - Q|`, 1.1 units at the median, 7.7 at p99 and 12.4 at most ("Wind and displacement"), so a typical clump stays within about a unit of where it is drawn and the tips of the largest clumps can be several units off (Task 5 review; user check 8).
- **Plant and leaf cards are opaque.** The clay pass has no alpha test: alpha is a non-goal of this sub-project and the capture records no alpha-test state, so a clip in the clay pixel shader would be drawing from guessed data. Textured cards show the texture's black background; untextured ones are pale rectangles.
- **The dog's fur shells** make the dog a white blotch ("Skinning", "Known limitations").
- **CPU skinning cost in a crowd is unmeasured.** 0.10 to 0.28 ms is for about 30 skinned draws of one hero, one dog and some birds.
- **Instancing cost while the camera moves is unmeasured.** The autoplay camera stands still. In play the engine re-sorts copies by distance and the flat streams are rebuilt. The one window in which the drawn set changed, the last of `fable_2_184.log` (input arrived at the game window in its last second), has a slowest frame of 12.61 ms and 9 uploads in its last frame; that is one uncontrolled observation, and what moved was not seen.
- **Six of the seven instancing entries and both tree entries are on the exception path** of the in-clip rule: `position_check.py` prints REJECT for these eight table entries, and what places them is the view-projection identity, the face normals and the screenshots. The four near-field instancing shaders were not judged on screen at all.
- **Four skin shaders rest on dump readings and on decoder resolution only** (`0x3A0F...`, `0x82F6...`, `0x9ED0...`, `0x5F44...`): they are not drawn in the main scene here. The runtime decoder resolves them to the same skin and position layout as `0xD4D5...` and `0xA1F7...` (Task 9 review; reproduced for this section by running `DecodeVertexFetches`, `SelectPosition` and `SelectSkin` on the dumped microcode with the generated table, in a scratch program that is not committed). That shows that table and decoder agree with each other, not that the reading is right.
- **The skin metrics are blind to permutations**: a skin whose rows or axes are permuted passes the orthonormality and edge checks. `0xD4D5...` is covered by the game's own posed stream; the four shaders above are not.
- **`0x2D40B53C926109BE` is in no capture** and not in the table (check 7).
- **Characters and plants were seen in one scene, from one side, standing still.** Criterion 2 is not closed.
- **The freeze**, the slow frame near scene frame 3016 and the 29.8 fps windows of 10:04 to 10:12 are unexplained (above). The freeze does not need the native renderer.
- **Statistics.** In the runs above `instanced` and `skinned` were counted when positions are built, before the index buffer can fail, so a draw dropped later would still have counted; no such draw occurred (`other 0`). The final fix wave counts them beside `drawn` (follow-up 6), and they read the same: 135 and 28 to 34 (`fable_2_193.log`).

**After the final fix wave** (runtime fixes of follow-up 6, commit df2d523; release build of 2026-10-06 14:12, no warning in the build output; `tools\drive_game.ps1 -Total 120`, no other input sent by the agent). A person was at the machine again. A read-only sampler beside the runs from `fable_2_192.log` on recorded once a second which process owned the foreground window and when the machine last received input (`GetForegroundWindow`, `GetLastInputInfo`).

| Log | Started | Run | What happened | Used |
|---|---|---|---|---|
| `fable_2_190.log` | 14:14 | native | live; the camera turned around between about 75 and 85 s (input at the machine, no sampler yet) | the three windows before the turn; the turned view after it |
| `fable_2_191.log` | 14:17 | native | live; the camera turned at about 45 to 49 s (the last input 49 s into the run, from the idle time read after it) | the turned view |
| `fable_2_192.log` | 14:20 | native | live, 7 of 7 windows; input for 6 s from 45 s with the game in front, the camera turned | the turned view |
| `fable_2_193.log` | 14:23 | split, `-Shots "70,95"` | live, 7 of 7; no input in any sample, the game in front | bridge-scene figures and shots |
| `fable_2_194.log` | 14:25 | view off | three live view-off windows (frames 1500 to 2100); from 74 s input at the game: a clay view was switched on and the camera turned | view-off capture figure |
| `fable_2_195.log` | 14:28 | native, `--fable2_native_geometry_budget_mb=16` | live; input at the machine, the camera turned at the load, F3 open | a geometry budget below one frame, turned view |
| `fable_2_196.log` | 14:30 | native, `--fable2_native_texture_budget_mb=16`, `-Shots "70,95"` | live, 7 of 7, camera untouched; another program in front in 108 of 122 samples and input arriving in 57 | bridge-scene figures and native shot; texture eviction |
| `fable_2_197.log` | 14:33 | native, `--fable2_native_geometry_budget_mb=20` | two live windows, then frozen from about 68 s; the game never in front, input in 97 of 122 samples | a geometry budget just below one frame, bridge view, live windows only |

"The turned view" is the same bridge seen towards the land, after the camera was turned by input at the machine: more trees, plants and characters than the autoplay view (1,117 to 1,149 drawn).

- **The bridge scene is drawn as before.** `fable_2_193.log` has the drawn, textured, uploads, hits, instanced and skinned figures of `fable_2_177.log` in all eight `clay` lines from scene frame 1216 to 3316 (2716: drawn 951, textured 323, 5 uploads, 2,220 hits, instanced 135, skinned 32), and frame 2700 reads captured 987, drawable 951, `no-transform` 6, `unsupported-prim` 30 in both. `fable_2_196.log` and the three windows of `fable_2_190.log` before the turn have drawn 947 to 953, textured 319 to 325, instanced 135 and skinned 28 to 34, the figures of `fable_2_184.log`. `skipped_bad_index 0 other 0` in every `clay` line of all eight logs; `bad-index`, `instance-unsupported`, `skin-unsupported`, `render-other`, `SLOW frame` and `texture path off` occur in none of them. `instanced` and `skinned` are now counted beside `drawn` and read what they read before.
- **Cost.** 30.0 fps in all seven windows of `fable_2_193.log` and of `fable_2_196.log`. Geometry resident 24.4 MB (`fable_2_193.log`, `fable_2_196.log`) and 22.5 MB (`fable_2_190.log`) against 25.0 to 25.1 MB before. Uploads per logged frame 3 to 7 (`fable_2_193.log`, `fable_2_190.log`) and 4 to 8 (`fable_2_196.log`) against 3 to 7. Clay hash 0.36 to 0.89 ms, decode 0.10 to 0.29 ms, record 0.14 to 0.40 ms over the three runs (before: 0.36 to 0.73, 0.10 to 0.28, 0.14 to 0.33). Slowest clay frame of a window 1.93 to 2.99 ms apart from the one near scene frame 3016. Guest work median 7.63 to 8.42 ms split, 7.52 to 7.61 ms and 8.56 to 9.01 ms native (before 7.42 to 7.68 and 7.39 to 8.77), on a machine in use. The frame that first builds the scene: 245 ms twice, 369 ms once.
- **View off.** Capture median 0.046 ms in each of the three live view-off windows of `fable_2_194.log` (p90 0.048 to 0.051 ms, slowest frame 0.114 to 0.128 ms), guest work median 6.69 to 6.83 ms.
- **Screenshots.** The clay half of both split shots (`logs\shots\final_split`, `fable_2_193.log`) equals that of `logs\shots\task9_split` pixel for pixel, apart from the dog (its fur shells change every frame) and the seam column: hero, crow, plant cards, bridge and terrain are identical. The native shot at 95 s of `fable_2_196.log` (`logs\shots\final_tex16`) against `logs\shots\task10_native\shot_95s.png`: 2,806 of 2.07 million pixels differ by more than 24 levels, all but 136 of them on the hero and the dog (animation), the 136 in the fern left of the bridge, where the same amount changes between the 70 s and 95 s shots of one run. Ferns, grass band, right-bank cards and trees stand textured where they stood; nothing is smeared, shifted or missing. In the turned view (`logs\shots\final_native`, `final_native_2`, `final_native_3`) the hero is seen from the side, standing with his arms down, the dog beside him, ferns and flower beds textured along the path, fences and a wagon behind them; nothing is stretched or detached.
- **The turned view** (`fable_2_190.log` from scene frame 2418, `fable_2_191.log`, `fable_2_192.log`): drawn 1,117 to 1,149 of as many drawable, instanced 155 to 162, skinned 35 to 53, textured 455 to 483, `skipped_bad_index 0 other 0`; uploads 5 to 18, geometry resident 37.4 to 44.8 MB (it includes the entries of the first view), hash 0.42 to 1.09 ms, decode 0.13 to 0.71 ms, slowest frame of a window 3.05 to 4.57 ms, and 22.7 to 25.2 ms in the window in which the camera turned. 30.0 fps, 29.6 and 29.9 in two of the three windows with the turn.
- **The slow frame near scene frame 3016 is still there**: 4.40 ms in `fable_2_193.log` (split; 6 uploads in the window's last frame, as in `fable_2_177.log`) and 3.82 ms in `fable_2_196.log` (native; 5 uploads, 6 in `fable_2_184.log`), where the other windows of those runs have 1.93 to 2.99 ms. It was 4.12 to 7.12 ms in the eleven earlier runs. So rebuilding flat UV buffers and decoding whole mesh streams are not what makes this frame slow, or only a part of it; two runs cannot show whether it became shorter.
- **A geometry budget below what one frame needs.** The budget cannot be met, because entries used in the current frame are never evicted, and the cache does not degrade gently: it stays correct and rebuilds a large part of the frame in every frame.
  - `fable_2_197.log`, 20 MB, bridge view (one frame needs 20.8 MB): drawn 947 and 953 as at the default budget, textured 319 and 325, instanced 135; 20.8 MB resident; 646 to 649 uploads and 1,567 to 1,582 hits per logged frame against 3 to 7 and about 2,210 to 2,224; decode 6.35 to 6.74 ms, slowest frame of a window 10.3 to 10.6 ms; 30.0 fps in its two live windows. In its frozen windows, where no stream changes, there are 0 uploads and 2,213 hits: the rebuilding starts with the first buffer a frame has to replace.
  - `fable_2_195.log`, 16 MB, turned view (one frame needs 18.5 MB): drawn 1,139 of 1,139, textured 458 to 470, instanced 159 to 161, skinned 43, `other 0`; 852 to 861 uploads and about 1,890 hits; decode 8.4 to 8.9 ms (12.7 in one logged frame), hash 0.49 to 0.74 ms, slowest frame of a window 14.1 to 15.9 ms; 30.0 fps in six windows and 28.8 in the one after the load. Both shots (`logs\shots\final_geo16`) show the whole scene, with nothing left as bare clay or missing.
  - The mechanism, by reading: the frame's first replaced buffer needs room, the only entries that may leave are those of the previous frame that this frame has not looked up yet, those are rebuilt when their draws come, and each rebuild takes room from the next. Being 0.8 MB over the budget costs about 650 rebuilt buffers a frame. The code before the wave evicted the same entries one at a time. Not evicting previous-frame entries at all when nothing older is left (going over the budget instead, as when everything is in use) would avoid it; that was not done. A last focused review of the eviction rule (2026-10-06; differential fuzz of 2.06 million inserts against an independent model, exact match) put the edge more precisely: the cliff starts when the live set plus twice the keys new per frame exceeds the budget, because previous-frame entries leave lowest id first, which takes long-lived entries before last frame's leftovers. In its simulation of the texture cache at that edge (980 live of a 1,000-entry budget, 20 new per frame, 32 uploads per frame) one live texture stayed pending for all 600 frames. Its measured remedy is the one above (0 rebuilds, resident peaking at 102 to 106% of the budget); it makes the budget soft by one frame's inserts and is recorded as a follow-up, not done.
  - No `SLOW frame` line, no error beyond the usual start-up lines, no device-removed line in either log.
- **A texture budget at the scene's size** (`fable_2_196.log`, 16 MB against 16.1 MB): 68 textures and 14.7 MB resident against 69 and 16.1 MB, the textured counts unchanged, no upload after the burst at world load. One texture that no draw of this view uses was evicted.
- **The freeze, again.** `fable_2_197.log` froze with the game never in front and input arriving; `fable_2_196.log` did not, with another program in front for most of the run and input arriving in 57 of 122 samples; `fable_2_192.log` and `fable_2_194.log` received input with the game in front and stayed live. So being out of front with input arriving is not sufficient for it.

**Follow-up candidates.**

1. Alpha test and translucent layers from captured state: record the alpha-test and blend state per draw, then clip plant and leaf cards and treat the fur shells as the layers they are. This is what would make the plants and the dog look like the emulated frame.
2. Billboards and particles: `0x29B6...`, `0x775C...` and the 30 `unsupported-prim` draws.
3. The freeze. Check 15 is answered (user, 2026-10-06: "the game does occasionally freeze when alt-tabbing") and a read-only investigation the same day established that it never recovers, that it is the game's own non-gameplay loop running over the loaded world (not a hang, not a host pause, not a controller disconnect) and that it is independent of the native renderer; the cause is not established. Evidence, the candidate causes and the next experiments are in `docs/freeze-investigation.md`.
4. The slow frame near scene frame 3016. It is still there after the final fix wave, which stopped rebuilding flat UV buffers when only an instance batch changes and stopped decoding whole mesh streams for instanced draws: 4.40 ms (`fable_2_193.log`) and 3.82 ms (`fable_2_196.log`) against 4.12 to 7.12 ms before, with the same uploads in the window's last frame ("After the final fix wave"). Those two are therefore not its cause, or only part of it. Next: a per-frame breakdown in the log (uploads, decode and hash of the slowest frame of a window, not of its last frame) and a run without a window shot.
5. A UV entry for `0xA584...` through a hand trace of `0x7CD5...`, and captures for `0x2D40...` and the four unsampled skin shaders (checks 7 and 10).
6. The final review (2026-10-06) and its fix wave. None of this changes a result above.
   - Open, in this order:
     - `position_check.py` has no machine-readable marker for exception-path entries: eight of the ten non-skin entries added by this sub-project print a bare REJECT, and the matrix-identity and face-normal checks that place them are not tool options.
     - A partial last vertex drops a whole skinned draw: `SkinPositions` (`bone_skin.h`) returns false when one index or weight word lies past the stream, and the draw is skipped as `render-other`; `position_check.py` gives that one vertex a NaN position instead. The runtime should cull the vertex, as the checker does.
     - The wind error of `0xA584...` has no bound ("Known limitations"; user check 8).
     - The 8.00 ms frame near scene frame 1516 (one run of fourteen, did not recur) and the 29.8 fps windows of 10:04 to 10:12 (did not recur) are unexplained; nothing is planned for them unless they return.
     - The slow frame near scene frame 3016 (item 4) and the freeze (item 3, user check 15).
     - Kept from the task reviews, not judged by the final review: a committed golden test of the checker's float32 equivalence with the C++ decoders; the bias cases of `test_instance_expand.cpp` hardcode the bias (the table's 0.5 is pinned by its `RealDraw` case).
   - Fixed in the final fix wave, tools and docs: `gen_transform_table.py` and `position_check.py` raise on an entry with both `"skin"` and `"instance"`, on a skin with `"pairs"` but no `"weight_fetch"` (it silently became rigid) and on a `"skin"` or `"instance"` entry without `"base"` (it was dropped silently; an entry with `"base2"`, the finder's two-window product, is still left out on purpose); the checker's verdict rejects an entry with rows the runtime would skip or with too few judged draws (rule at the top of this section and under "Instancing", "Checker"); the "Checker" text says that the in-clip share is no evidence for the cut constants; the `0x2D40...` record in `vs-transforms.json` no longer argues from `0xA584...` being rejected.
   - Fixed in the final fix wave, runtime (commit df2d523; built and run, see "After the final fix wave"):
     - Cache eviction. Instanced streams are keyed per draw, so a changed instance batch leaves its old entry behind until the 256 MB budget evicts it, and at the budget every insert searched the whole index for one victim (measured in a scratch program: 24,403 entries, 270 new keys in a frame, 43.7 ms of bookkeeping). An insert past the budget now evicts in one pass, least recently used first (`GeometryCacheIndex`, `test_geometry_cache_index.cpp`). Entries that neither the current frame nor the previous one used go until the total is at 15/16 of the budget, so the inserts after it find room. Entries last used in the previous frame go only as far as the insert needs: in the middle of a frame they are mostly entries a later draw of that frame will look up, and taking them to the mark too rebuilt a live set near the budget in every frame (the re-review's simulation: 17 to 217 live entries rebuilt in the worst frame with the live set at 0.93 to 0.98 of a 16 MB budget and 40 new keys a frame; a scratch simulation of that case rebuilds none with this rule, as the code before the change did). Entries used in the current frame are still never evicted, and once a pass leaves nothing idle the frame's later inserts do not search again. What is not solved: when the entries of the last two frames alone pass the budget, every insert past it searches the index and takes one previous-frame entry, as it did before the change; with a budget below what one frame needs this rebuilds about 650 to 860 buffers a frame (`fable_2_195.log`, `fable_2_197.log`). The texture cache uses the same index and gets the same behaviour at its own budget.
     - Flat UV buffers are keyed on the mesh UVs and on what maps an index to a mesh vertex (`inv_count`, `count`, the bias, the flat count) and hashed on the mesh UV stream alone; the first copy, the instance stream, its row layouts and the offset are out of the key, so a draw whose batch changed keeps its UV buffer (`UvKey`, `test_clay_logic.cpp` 116 to 120 and 134 to 141).
     - An instanced draw decodes only the vertices of one copy from its mesh stream, not the whole stream (`InstanceMeshCount`, `test_clay_logic.cpp` 142 to 149).
     - `instanced` and `skinned` are counted beside `drawn`, after the index list was built.
     - Tests added: `PaletteBones` with a 32-byte stride; an instanced record keeps `deformed`.
   - Closed by the final review without a change (not open): the `InstanceBoundsOk` and `SelectSkin` test gaps of Tasks 1 and 2; the bias literal's `repr` for infinity and NaN; the checker's `lru_cache` size, its double-precision `project()`, and the capture's `DumpStream` hashing before it writes and its physical-address add that can wrap (log only); the instanced cache-key tests varying only two of the row and offset fields; the offset reference not being required to be component 0; the generator not recording the row fetches' format and offset; a rounded reciprocal such as 0.3333 for 3 becoming `bad-index` past index 5000 (not seen on a real draw); no test of `ResolveSkip(kSkinUnsupported, kBadIndex)`; the skinned count K not being checked against the shader's drawable count.
7. **TODO (decided by the user 2026-10-06, not specified or scheduled): everything the game shows outside the gameplay 3D scene, drawn natively.** The native renderer captures only the draws inside the main-scene bracket and native view hides the rest. Wanted in the end: cutscenes, menus, the HUD, loading screens, subtitles, fades, letterbox and video. Chosen approach: native replay — capture the 2D and full-screen draws and redraw them natively, as the Skate 3 reference renderer does for its HUD — not a pass-through of the emulated 2D image over the native scene. In-engine cutscenes are 3D and go through the existing pipeline; their work is shader coverage (new table entries) and camera cuts. This belongs to roadmap sub-project 7 (post-processing and 2D/UI; `docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md`, "Decomposition") and needs its own brainstorm and spec. Not known yet: what native view shows during a menu, a loading screen or a video; whether the 2D is drawn into the same target as the 3D after the resolve; which cutscene shaders the tables lack. A discovery capture during a cutscene and a look at native view in a menu and on a loading screen are the cheap first steps.
8. **Suppression experiment (2026-10-06): how much frame time the emulated GPU costs, pass by pass.** Six autoplay runs of the bridge scene with the frame cap lifted (`--vsync=false --guest_vblank_pacing=false --frame_limit=0`), 120 s each, machine left alone; emulated draws and resolves of whole passes skipped by render-target pitch with the SDK's ablation switch (`--native_render_suppress_debug=true --native_render_suppress_pitches=...`; the clay views do not replace the frame, so `native_render_suppress_emulated_draws` alone does nothing for them). Figures are medians of the last seven 300-frame windows. Copies of the logs and screenshots: `out/native-evidence/suppression-experiment/`.

   | Log | Configuration | Frame rate | Frame median | Work | Render-loop call |
   |---|---|---|---|---|---|
   | `fable_2_201.log` | native renderer off, nothing skipped | 33 to 35 fps (one window 37.6) | 29.0 to 29.5 ms | 6.5 to 7.2 ms | 22.0 to 22.4 ms |
   | `fable_2_202.log` | off, main scene pass skipped (pitch 1120) | 33.5 to 35.5 fps | 29.0 to 29.4 ms | 7.0 to 7.9 ms | 21.7 to 22.2 ms |
   | `fable_2_203.log` | off, main scene and shadow maps skipped (1120, 1040) | 39.7 to 40.0 fps | 24.9 to 25.1 ms | 6.3 to 7.0 ms | 18.0 to 18.7 ms |
   | `fable_2_204.log` | off, every pass skipped except the final composite (`*`, keep 1280) | 80.0 fps | 12.5 ms | 6.1 to 6.4 ms | 6.1 to 6.4 ms |
   | `fable_2_205.log` | native view, nothing skipped | 30 to 33 fps | 29.4 to 33.2 ms | 7.9 to 8.8 ms | 21.4 to 24.3 ms |
   | `fable_2_206.log` | native view, main scene pass skipped (1120) | 32 to 33 fps | 29.3 to 32.8 ms | 8.0 to 8.4 ms | 21.2 to 24.3 ms |

   - **Ceiling.** With every emulated pass but the final composite skipped the scene runs at 80 fps: 12.5 ms, of which about 6 ms is the game's own work and about 6 ms its render-loop call. Removing GPU emulation is worth at most about 2.4 times in this scene; beyond that the guest CPU is the limit.
   - **The main scene pass alone gives no measurable gain** (`202` against `201`, and `206` against `205`), although it holds the largest share of the draws (1,779 of about 2,600 per the pitch ablation of section 4). Main scene plus shadow maps gives one step (29.2 to 25.0 ms); the largest part of the gain, 12.5 ms, comes from the remaining passes (the half- and quarter-resolution post chain and their resolves).
   - **Resolution of the measurement: 4.17 ms.** Every frame median is a multiple of 1/240 s (12.5 = 3, 25.0 = 6, 29.2 = 7 steps; the menus' 8.4 ms = 2), and the display runs at 240 Hz: presentation still locks to the refresh with `--vsync=false`. A saving smaller than one step is invisible, so "no measurable gain" for the main scene pass means under 4.2 ms, not zero, and the split between the passes is known only to that step. A finer instrument is needed before drawing conclusions about single passes: the F3 `GPU emu busy` figure, or the per-pitch statistics of `native_render_collect_stats`.
   - **The native picture does not need the emulated main scene pass** (`logs/shots/suppress_6_native_skip1120/shot_95s.png`): hero, dog, plants, bridge and terrain are drawn as in the run without skipping, and the capture counts are the same (984 captured, 949 drawable, 135 instanced, 30 to 32 skinned). Capture hooks sit on the guest's D3D calls, which still run.
   - **Cost of the native view at an unlimited rate**: `work` rises from about 7.0 to about 8.2 ms (the capture), and some windows fall one step (33.2 ms).
   - Not measured: other scenes, a crowd, what the skipped post passes cost individually, and whether a suppressed pass breaks anything the game reads back (the hero and dog resolve readback is at stake when the main scene is skipped).
   - **Parked by the user (2026-10-06): the 12.5 ms floor.** A correction to the column names first: `Work` is the game's logic and all of its draw calls (everything outside the guest's swap call, `MainRenderLoop_82B9CD68`); `Render-loop call` is the time inside that swap call, which is waiting, not draw submission. The swap time is the same 6.3 to 6.7 ms in the menus (frame 8.4 ms, work 1.6 ms) and in the world with every emulated pass skipped (frame 12.5 ms, work 6.1 ms), so about 6.5 ms per frame is a fixed wait. Two known mechanisms could produce it and which one sets the 4.17 ms step is not established: the game waits for a GPU counter to advance two units per frame (`docs/FPS_CAP_INVESTIGATION.md`), and presentation appears locked to the display's refresh steps. If it can be removed the floor falls to roughly 6 to 8 ms; it may also be worth something with emulation in place (the crowd overlay of `fable_2_200.log` shows the emulated GPU thread 13.3 ms per frame in `present` of a 49.9 ms present interval). The remaining 4.5 ms of world work over the menus' 1.6 ms is simulation plus the draw calls through the recompiled console code; the split needs profiling. Frame = work + swap, so the two run serially on one thread. Cheapest tests when this is picked up: one unlimited autoplay run with the display at 120 Hz (if the menus fall from about 115 to about 60 fps the wait follows the refresh; if not it is the counter wait), or the emulated GPU's present and wait times added to the `[frame]` log line.

## 13. Materials

Sub-project 6 (spec `docs/superpowers/specs/2026-10-06-native-renderer-materials-design.md`, 2026-10-06). Evidence files of this section are in `out\native-evidence\materials\` (git-ignored); the names below are files of that folder unless a path is given.

### Render state

Plan Task 2 (2026-10-06). The game's D3D library keeps the pending GPU register values in its device object. This section establishes where, proves the values against the emulated GPU's register file draw by draw, and lists the states the game uses.

**Result.** The ten registers read from the device shadow after a draw builder returns are the GPU's registers at that draw in every execution of the draw, with one exception that is not a fault of the shadow: in the depth pre-pass of the tiled main scene the D3D library itself forces `RB_COLOR_MASK` to 0, while the shadow keeps the game's value (0xF), which is what every colour pass uses. All sampled rows were paired (100% in both captures). By ruling (2026-10-06) `RB_COLOR_MASK` is taken from the shadow as the colour passes' value: the checker compares it on colour-pass executions and reports the pre-pass apart, where the only accepted difference is a GPU colour mask of 0. The first version of the checker compared every execution alike and exited 1 on both captures for that register. The next subsection repeats the proof on two more captures with thirteen registers and settles the draws that execute only in the pre-pass.

| | Bridge, standing still | After walking, camera moving |
|---|---|---|
| Capture | `native_discovery_20261006_164805.jsonl` | `native_discovery_20261006_165738.jsonl` |
| GPU log | `gpu_state_bridge_20261006_164805.log.gz` | `gpu_state_walk_20261006_165738.log.gz` |
| Checker output | `task2_render_state_check_bridge.txt` | `task2_render_state_check_walk.txt` |
| Sampled rows (draw + tess), every one with `"rs"` | 55,279 (36,613 + 18,666) | 44,726 (30,679 + 14,047) |
| Paired with a GPU draw | 55,279 (100.00%) | 44,726 (100.00%) |
| GPU executions compared (1 / 2 / 3 per row) | 92,847 (29,922 / 13,146 / 12,211 rows) | 80,449 (22,587 / 8,555 / 13,584 rows) |
| `RB_ALPHA_REF`, `RB_DEPTHCONTROL`, `RB_BLENDCONTROL0`, `RB_COLORCONTROL`, `PA_SU_SC_MODE_CNTL`, `RB_BLEND_RED..ALPHA`: unequal executions | 0 of 92,847, each | 0 of 80,449, each |
| Colour-pass executions; `RB_COLOR_MASK` unequal | 80,553; 0 | 66,761; 0 |
| Pre-pass executions (bin select `0x80000001`); `RB_COLOR_MASK` | 12,294; GPU 0 against the device's 0xF in all | 13,688; GPU 0 against the device's 0xF in all |
| Rows with no colour-pass execution (counted apart, not as equal) | 83 | 104 |
| Checker exit code | 0 (`RESULT: OK`; `0x2113`, `0x2114`, `0x2206` named as absent from these GPU logs) | 0 (the same) |

**Shadow offsets (static).** The draw builders flush eight register ranges from the device with the register-run writer `0x8221C908` (r3 device, r4 a dirty mask whose top bit is the first register, r5 the first register, r6 device + offset; register r5 + n is taken from r6 + 4 * n, section 8). The eight calls of `DrawIndexedVertices` (`0x8221E0F0`, `generated/default/fable_2_recomp.201.cpp` lines 1198 to 1411) agree with the fact sheet in every pair:

| `addi r6,r31,<offset>` | `li r5,<first register>` | Mask width (registers) | Call |
|---|---|---|---|
| `0x8221E1B4` +0x2934 (10548) | `0x8221E1B8` 0x2200 (8704) | 12 (`0x8221E1BC rldicr r4,r30,52,11`) | `0x8221E1C4` |
| `0x8221E1D4` +0x2920 (10528) | `0x8221E1D8` 0x2180 (8576) | 5 (`rldicr r4,r30,47,4`) | `0x8221E1E4` |
| `0x8221E200` +0x2880 (10368) | `0x8221E204` 0x2000 (8192) | 16 (`rldicr r4,r30,6,15`) | `0x8221E210` |
| `0x8221E228` +0x28CC (10444) | `0x8221E22C` 0x2100 (8448) | 21 (`0x8221E230 rldicr r4,r30,22,20`) | `0x8221E238` |
| `0x8221E278` +0x2964 (10596) | `0x8221E27C` 0x2280 (8832) | 21 (`rldicr r4,r28,9,20`) | `0x8221E288` |
| `0x8221E2A4` +0x29B8 (10680) | `0x8221E2A8` 0x2300 (8960) | 38 (`rldicr r4,r27,26,37`) | `0x8221E2B4` |
| `0x8221E2CC` +0x2A50 (10832) | `0x8221E2D0` 0x2380 (9088) | 8 (`rldicr r4,r27,18,7`) | `0x8221E2DC` |
| `0x8221E314` +0x2780 (10112) | `0x8221E318` 0x4900 (18688) | 40 (`lis r4,-256`) | `0x8221E324` |

The same `addi r6,r31,0x2934` / `li r5,0x2200` and `addi r6,r31,0x28CC` / `li r5,0x2100` pairs are in ten functions of the generated code: the six draw builders (`0x8221C518`, `0x822060A0`, `0x82217EE8`, `0x8221E0F0`, `0x8221C9C8`, `0x82207C30`, eight calls of the writer each) and `0x822069C0`, `0x82BA5448`, `0x8222B948`, `0x82BA29B8` (six calls each). Constants: `kDeviceRegShadowBase2100` = 0x28CC, `kDeviceRegShadowBase2200` = 0x2934 and `DeviceRegShadowOffset(reg)` in `xdk_layout.h` (test `tests/native/test_xdk_layout.cpp`).

| Register | Name (SDK `register_table.inc`) | Device offset |
|---|---|---|
| 0x2104 | `RB_COLOR_MASK` | +0x28DC |
| 0x2105..0x2108 | `RB_BLEND_RED`, `_GREEN`, `_BLUE`, `_ALPHA` (floats) | +0x28E0..+0x28EC |
| 0x210E | `RB_ALPHA_REF` (float) | +0x2904 |
| 0x2200 | `RB_DEPTHCONTROL` | +0x2934 |
| 0x2201 | `RB_BLENDCONTROL0` | +0x2938 |
| 0x2202 | `RB_COLORCONTROL` | +0x293C |
| 0x2205 | `PA_SU_SC_MODE_CNTL` | +0x2948 |
| 0x2208 | `RB_MODECONTROL` (not in the proof; the offset agrees with the shader flush's device + 0x2954, `xdk_layout.h`) | +0x2954 |

Register numbers and bit layouts were checked against the SDK (`register_table.inc`, `registers.h`, `xenos.h`); they are as the plan gives them, nothing differs. `RB_COLORCONTROL`: bits 0-2 alpha function, 3 alpha test enable, 4 alpha-to-mask enable (bits 24-31 are the four alpha-to-mask offsets, set in real words such as `0x8700001C`). `RB_BLENDCONTROL0`: bits 0-4 colour source factor, 5-7 colour operation, 8-12 colour destination factor, 16-20 alpha source, 21-23 alpha operation, 24-28 alpha destination. `RB_DEPTHCONTROL`: bit 0 stencil enable, 1 depth test, 2 depth write, 4-6 depth function (bits 8 and up are the stencil functions and operations). `PA_SU_SC_MODE_CNTL`: bit 0 cull front, 1 cull back, 2 front face (0 counter-clockwise, 1 clockwise). `RB_COLOR_MASK`: bits 0-3 target 0 (R, G, B, A). Compare functions 0-7: never, less, equal, less-equal, greater, not-equal, greater-equal, always. Blend factors: 0 zero, 1 one, 4/5 source colour and its inverse, 6/7 source alpha, 8/9 destination colour, 10/11 destination alpha, 12/13 constant colour, 14/15 constant alpha, 16 source alpha saturate. Blend operations: 0 add, 1 subtract, 2 min, 3 max, 4 reverse subtract.

**Discovery rows.** `"draw"` and `"tess"` rows carry `"rs": {"2104": "0x...", "210E", "2200", "2201", "2202", "2205", "2105", "2106", "2107", "2108"}`, each read from the device at `DeviceRegShadowOffset(reg)` when the row is written, which is after the builder returned (a float register as its bits; a register that cannot be read is left out). They also carry `"cmd"`, the device's command write pointer at that moment (`kDeviceCommandWriteOffset`, the last dword the builder wrote), which the proof uses to find the draw's packet. Both are written only while discovery is armed (`AppendRenderState` in `capture.cpp`). A four-frame capture of the first round's final build (`task2_final_build_native_discovery_20261006_171520.jsonl`) has the ten registers and `cmd` in all 1,861 draw and tess rows; the rows have since gained three registers and the bin mask (next subsection).

**GPU side (temporary diagnostic, reverted).** A patch to the SDK's command processor (`task2_sdk_gpu_state_log.patch`, 97 added lines in `src/graphics/command_processor.cpp`, not committed; the SDK is back at `0c6d1de` and the installed runtime was rebuilt from it) wrote one line per `DRAW_INDX` packet handed to the backend, to the file named by `FABLE2_GPU_STATE_LOG`: the GPU frame (swaps executed), the draw ordinal in the frame, the physical address just past the packet, index count, primitive type, bin select and bin mask, vertex and pixel shader hashes, surface pitch, `RB_MODECONTROL`, vertex constant `c0` and the ten registers from the register file; and one short line per draw packet skipped by the bin predicate. It sits in `CommandProcessor::ExecutePacketType3Draw` at the call of `IssueDraw`, not inside the D3D12 `IssueDraw` as the plan says: the packet address is only known there, and the register file is the same object. It logged 240 frames starting 2 s before discovery armed.

**Pairing** (`tools\xdk_sigmatch\render_state_check.py <capture> --gpu <gpu log>`, test `tests/test_render_state_check.py`). The emulated GPU executes a frame's commands after the guest wrote them and executes a tiled draw several times, so draws are not matched by order. The identity of a pair is the packet's address within the frame:

- the GPU frame is the capture frame plus one offset for the whole capture, the one with the most confirmed pairs;
- the packet ends at the row's write pointer + 4 (the draw packet is the last thing the builder wrote), or 24 bytes before it (with per-draw bin masks, section 8, seven more dwords follow the packet); `GpuAddress` of `xdk_layout.h` converts the pointer.

A second check confirms the pair, and it is weak:

- the packet's index count is the one the builder's arguments give (`DrawIndexedVertices` r7, `DrawVertices` r6, `8221C9C8` r6 patches, `82207C30` 4 * r7);
- the GPU ran the row's vertex shader, or its `c0` has the row's bits (the depth pre-pass runs the same packet with other shaders, below).

Neighbouring draws often share index count, shader and `c0` (terrain patches of one shader under one matrix), so this check would not identify a draw by itself: the packet of the *next* sampled row passes it for 30,541 of 55,159 bridge rows and 19,373 of 44,606 walking rows (the checker's first `control` line; the review measured the same figures). In real pairs `c0` matches in every execution and the vertex shader alternative never decides (82,463 executions have both, 10,384 `c0` alone, none the shader alone; walking 67,226 and 13,223). What the check does catch is a packet of another draw at the right address, as a wrong frame offset would give. Every confirmed execution of the packet in that frame is compared; a packet at the address that fails the check is counted as not confirmed and never compared, and any sampled row left unpaired fails the run (the tests pin a packet with another index count, and one with another shader and other constants, as not paired).

What the pairing rests on, per capture:

| | Bridge | Walking |
|---|---|---|
| Frame offset; rows confirmed at it; at the runner-up offset | +1587; 6,143 of 6,143 probed; +1617: 467 | +1888; 6,390 of 6,390 probed; +1898: 188 |
| Rows with no packet at the address / skipped by the bin predicate / packet not confirmed | 0 / 0 / 0 | 0 / 0 / 0 |
| Executions confirmed by index count and: vertex shader and `c0` / `c0` alone | 82,463 / 10,384 | 67,226 / 13,223 |
| Frames whose view differs from the frame before (most common main-scene `c0`; `task2_pass_sequences_*.txt`) | 0 of 120 | 118 of 120 |
| Control: the next sampled row's packet passes the confirmation check too | 30,541 of 55,159 rows | 19,373 of 44,606 rows |
| Control: compared with the next sampled row's colour-pass executions instead of its own, a register differs | 13,237 of 55,076 rows | 16,959 of 44,502 rows |

The offset found is the one the two delays predict: capture frame 1 is GPU frame 1588 and 1889, 62 frames after the GPU log's first frames (1526 and 1827), and the GPU log starts 2 s before discovery arms. At another offset the command buffer holds other draws at the same addresses: the best wrong offset confirms 467 and 188 of the probed rows. In the walking capture the view changes in 118 of the 120 frames, so the `c0` test there also pins the frame; on the bridge every frame has the same view and a neighbouring frame would hold the same draws with the same state. The second control shows that the registers do differ between neighbouring draws (a quarter to a third of the rows), so the equality is not the equality of a constant; for the other rows a neighbour's packet would have given the same verdict, which is why the address, not the confirmation, carries the pairing. Its limit: the blend constant registers hold 1.0 in every row of the bridge capture, so for `0x2105..0x2108` only the walking capture is a test (62 rows differ in the control, 0 in the proof).

**Captures.** Both through `tools\drive_game.ps1` with `-Env @{FABLE2_NATIVE_DISCOVERY="120"; FABLE2_NATIVE_DISCOVERY_EVERY="4"; FABLE2_GPU_STATE_LOG="<file>"; FABLE2_GPU_STATE_LOG_FRAMES="240"; ...}`:

- Bridge: `-Total 110 -Shots "52,90"`, `FABLE2_NATIVE_DISCOVERY_DELAY="55"`, `FABLE2_GPU_STATE_LOG_DELAY="53"`. Log `task2_bridge_fable_2_209.log`: 30.0 fps with work medians of 6.2 to 6.4 ms in the windows around the capture (live; the window that contains the capture reads 19.8 fps, the capture's own cost). `task2_bridge_shot_52s.png` and `task2_bridge_shot_90s.png` show the bridge view over the lake.
- Walking: `-Total 110 -Shots "65,71,84" -GameArgs "--dump_shaders=C:\Users\Ryan\code\Fable-2-Recomp\out\shader_dump"`, `-Plan "23:E,27:S,30:E,35:E,55:LS=0/100/3000,59:RS=-60/0/1500,62:LS=0/100/2500,69:RS=-60/0/600,72:RS=60/0/600"`, `FABLE2_NATIVE_DISCOVERY_DELAY="65"`, `FABLE2_GPU_STATE_LOG_DELAY="63"`. The hero walks off the bridge, the camera turns left, he walks on to a fence of posts under the trees; discovery arms after the walk (66.3 s on the plan's clock, complete at 76.2 s), and the two short camera turns at 69 s and 72 s fall inside it. Log `task2_walk_fable_2_212.log`: 30.0 fps, work 5.3 to 6.2 ms around the capture (live). `task2_walk_shot_65s.png` (just before arming): the hero at the fence, tree crowns over the upper half of the view, thin pale trunks left of centre and at the right edge, ferns and grass in front. `task2_walk_shot_71s.png` (inside the capture, after the left turn): the same place seen further left, more trunks and a path with lanterns behind the fence. `task2_walk_shot_84s.png`: the first view again, darker, with the quest banner. The delay differs from the plan's 55 s because the walk has to come first. The plan was found in two tries: `task2_planA_shot_74s.png` (the plan up to the second walk, without discovery) already shows this view. A stick deflection of 30% for 10 s from 70 s did not turn the camera (`task2_planB_shot_69s.png`, `_75s.png` and `_82s.png` show one view), so the two turns inside the capture use 60%.

**The depth pre-pass and `RB_COLOR_MASK`.** The tiled main scene (pitch 1120) is executed under five bin selects, the values section 3b lists. Executions of the sampled main-scene rows, and all GPU draws at that pitch, by bin select:

| Bin select | Bridge: executions of sampled rows; unequal | Walking: executions; unequal | GPU draws at pitch 1120 in the 240 logged frames (bridge): colour mask |
|---|---|---|---|
| `0x80000001` | 12,294; `RB_COLOR_MASK` 12,294 | 13,688; `RB_COLOR_MASK` 13,688 | 102,039 draws, mask 0 in all of them |
| `0x80000003` | 15,899; none | 14,561; none | 131,089: 0xF in 129,985, 0 in 1,104 |
| `0x0000000C` | 12,803; none | 8,346; none | 106,369: 0xF in 105,265, 0 in 1,104 |
| `0x00000002` | 12,554; none | 13,793; none | 104,439: 0xF in all |
| `0x00000008` | 12,554; none | 13,793; none | 103,719: 0xF in all |

- Bin select `0x80000001` is a depth pre-pass: no draw in it writes colour. Of the 12,294 bridge executions there, 10,384 run a vertex shader other than the row's (5,604 with `RB_MODECONTROL` 4, colour and depth; 4,780 with 5, depth only) and 1,910 run the row's own shader depth-only (tessellated terrain). Example (a line of the first version of the checker, which listed these as mismatches): `frame 1 (GPU frame 1588, draw 988) D3DDevice_DrawIndexedVertices? vs 0xBEAD84BD72072E0E ps 0xF6D98C7B4D98438B: device 0x0000000F, GPU 0x00000000 (pitch 1120, bin select 80000001, mode 4, GPU vs 0xC47236E5A9B00356 ps 0x37CAF82F552893EE)`. The same packet then runs at bin selects `0x2` and `0x8` with the row's shaders and colour mask 0xF.
- Rows fall into fixed groups by the bin selects they execute in (`task2_pass_sequences_*.txt`; bridge / walking rows): `0x80000001` + `0x2` + `0x8`: 12,211 / 13,584 (pre-pass, then colour); `0x80000003` + `0xC`: 12,803 / 8,346 and `0x80000003` alone: 3,096 / 6,215 (no pre-pass: all terrain layer draws of `8221C9C8`, every blended mesh, 89 / 33 rows without blending); `0x2` + `0x8` without pre-pass: 343 / 209 (adaptive terrain `82207C30`, blend off); `0x80000001` alone: 83 / 104.
- The 83 / 104 rows that execute only in the pre-pass are all adaptive terrain draws (`82207C30`, vertex shader `0x5003700B7C9B1C16`, pixel shader `0x2F8A0341E8C09698`, words `2200=0x00708766 2201=0x00010001 2202=0x00000004 2205=0x00218002 2104=0x0000000F`). The game never draws them in colour, and the shadow says colour mask 0xF. The device's bin mask decides this; these two captures do not hold it. The next subsection reads it and gives the rule.
- Where the 0 comes from (static): the D3D library's tiling state emitter `0x822155E0`, which the builders call when the pending dirty mask intersects the mask at device + 40 (`DrawIndexedVertices` `0x8221E184 ld r11,40(r31)` .. `0x8221E1A0 bl 0x822155E0`; the emitter returns the bits still to flush). For a dirty colour mask it first writes the shadow's value with a type-0 packet (`0x822158A8 li r10,0x2104`, `0x822158B8 lwz r10,0x28DC(r31)`, stored at `0x822158B0` / `0x822158C4`) and sets r20 = 0 (`0x822158B4`). Then it writes `SET_BIN_MASK_HI` 0 and `SET_BIN_MASK_LO` `0x15555555` (headers `0xC0006100` / `0xC0006000` from `0x82215630` / `0x82215634`; `0x822158E8 lis r8,0x1555`, `0x822158F0 ori r8,r8,0x5555`), a predicated `SET_CONSTANT` (header `0xC0012D01` from `0x82215638`) of `0x00040104` = `RB_COLOR_MASK` with the value r20 = 0 (`0x82215938`..`0x82215948`), and restores the bin mask from device + 0x31A4 / + 0x31A8 (`0x8221594C`..`0x82215960`). The same block overrides `RB_SURFACE_INFO` and `RB_HIZCONTROL` for those bins (`0x82215904`..`0x8221592C`). Bin select `0x80000001` has bit 0, which is in `0x15555555`; `0x2` and `0x8` do not intersect it.
- So the failures are not draws whose state the engine writes straight into the command buffer. They are the library's own override for the pre-pass bins, written after the shadow's value, and the shadow is never changed by it.
- Not established: why bin selects `0x80000003` and `0xC`, which also intersect `0x15555555` (bits 0 and 2), show the shadow's colour mask in all their executions (15,899 and 12,803 on the bridge; 14,561 and 8,346 walking). Inference: the rows executed there take another path through the emitter; none of them has a pre-pass execution, and no row mixes the bin selects `0x80000001`, `0x2`, `0x8` with `0x80000003`, `0xC`, as if the scene were replayed in two separate parts. Also not established: which part of the screen each bin select covers.
- The 1,104 GPU draws with colour mask 0 at each of `0x80000003` and `0xC` are identified (`task2_extra_bridge_164805.txt`, script `task2_r1_extra.py`): one shader pair, `0xB6C9863F710683EC` / `0xA4A965C189287B99`, point lists (primitive type 1) of index count 1 at 24 distinct packet addresses, 48 executions per frame and bin select in the 23 frames that have them and none in the other 217. They are not draws of the four sampled builders. In the walking log the same draws are in all 240 frames (11,520 per bin select), beside 240 depth-only draws of `0xF160B4DA459A6D40` (`RB_MODECONTROL` 5) per bin select, the shader of the census's rows with colour mask 0. I could not reproduce a figure of 14 to 18 executions per frame.
- Outside the main scene every sampled draw executes once (bin select `0xFFFFFFFF`) and all ten registers are equal (26,743 bridge and 16,268 walking executions; pitches 160, 280, 320, 560, 1040, 1280, with `RB_MODECONTROL` 4 and 5).

**Limits.**

- Rows exist for four builders (`DrawVertices`, `DrawIndexedVertices`, `8221C9C8`, `82207C30`). The other draw hooks (`BeginVertices` `0x822060A0`, `0x82217EE8`, the `DrawIndx2` emitters) flush the same shadows with the same code but were not sampled.
- One draw in four was sampled, in two scenes of one region (Bower Lake by the bridge), 120 frames each.
- The proof is about the register words. What the emulator's backend derives from them (its normalized depth control and colour mask) is not part of it.
- Stencil: `RB_DEPTHCONTROL` bit 0 is set in most main-scene words (`0x00708767`: stencil function always, pass operation replace). The stencil reference (`RB_STENCILREFMASK`) is not captured and the native pass has no stencil.

**Census** (main-scene rows, sampled one draw in four over 120 frames: 28,536 bridge, 28,458 walking; from the checker outputs named above, which also list every value's shader pairs and the rows of the other passes). Shader pairs are vertex / pixel shader hashes, the largest first.

| Alpha test (`RB_COLORCONTROL`) | Bridge | Walking | Largest shader pairs |
|---|---|---|---|
| off | 21,981 | 21,380 | `0xC30A97D946FA2BE4` / `0x9C2144E42E95A76F`, `0xECD66A10092E6562` / `0x8D900846800943C8`, `0xFB68A7F2301210E1` / `0x4755A2E4A05BA678` |
| on, greater, with alpha-to-mask | 5,100 | 5,101 | `0x8123C16DBF583F92` / `0xA7E45D07F5CF6627`, `0xA5846836C90E1192` / `0x7CD57B81550F19E3`, `0x475EC9F795E5EDBB` / `0x00E09D1BC5295D52` |
| on, greater | 1,005 | 1,526 | `0xBEAD84BD72072E0E` / `0xA17D8AEC3A817D45` and `0xF6D98C7B4D98438B`, `0x563E3BE17857DB59` / `0xFF19CE696F916E0C` |
| on, greater-equal | 450 | 451 | `0x7C5710DEF3EE33C4` / `0x014F8A02DB7B19CA` |

Alpha-to-mask never occurs without the alpha test. Alpha references of the rows with the test on (bridge / walking): 0.501961 (128/255) 4,034 / 2,875; 0 1,127 / 2,726; 0.0588235 (15/255) 720 / 131; 0.0627451 (16/255) 450 / 451; 0.996078 (254/255) 172 / 380; 0.00392157 (1/255) 52 / 515.

| Blend (`RB_BLENDCONTROL0`; colour and alpha use the same triple in every row) | Word | Bridge | Walking | Largest shader pairs |
|---|---|---|---|---|
| off (one, add, zero) | `0x00010001` | 12,726 | 13,930 | `0xECD66A10092E6562` / `0x8D900846800943C8`, `0x8123C16DBF583F92` / `0xA7E45D07F5CF6627`, `0x475EC9F795E5EDBB` / `0x00E09D1BC5295D52` |
| source alpha, add, one | `0x01060106` | 13,954 | 12,149 | terrain layers: `0xC30A97D946FA2BE4` / `0x9C2144E42E95A76F`, `0xFB68A7F2301210E1` / `0x4755A2E4A05BA678`, `0xC30A97D946FA2BE4` / `0x45243B758F9272AB` |
| source alpha, add, zero | `0x00060006` | 806 | 959 | terrain layers: `0xC30A97D946FA2BE4` / `0x9C2144E42E95A76F`, `0x45243B758F9272AB`, `0x04F23292B2F44077` |
| source alpha, add, inverse source alpha | `0x07060706` | 810 | 520 | `0x7C5710DEF3EE33C4` / `0x014F8A02DB7B19CA`, `0xBEAD84BD72072E0E` / `0xA17D8AEC3A817D45` |
| one, add, inverse source alpha | `0x07010701` | 188 | 683 | `0xD4D558DA6A82BDC8` / `0x401A01FD4E5F8757`, `0x563E3BE17857DB59` / `0xFF19CE696F916E0C`, `0xA1F7E9885EC466DF` / `0x401A01FD4E5F8757` |
| one, add, one | `0x01010101` | 52 | 186 | `0x29B6506FBACEB93A` / `0xDBFD88A80EDBCE36` and `0xC300519EC8915346`, `0x775C6085FBB9D676` / `0xC300519EC8915346` |
| constant colour, add, inverse constant colour | `0x0D0C0D0C` | 0 | 31 | `0xECD66A10092E6562` / `0x401A01FD4E5F8757` |

The blend constant is used: by no row on the bridge and by 31 walking rows (the last line of the table). The constant registers hold 1.0 in all bridge rows and in 28,427 walking rows; the other 31 are exactly the 31 rows with the constant factors, and hold one grey value in all four registers, between 0.188 and 0.902 (0.902 in 16 rows; `task2_blend_constant_rows.txt`), which looks like an object fading in or out (inference). The only blend operation is add.

| Depth (`RB_DEPTHCONTROL`) | Bridge | Walking | Largest shader pairs |
|---|---|---|---|
| test on, write on, greater-equal | 27,936 | 27,520 | everything opaque, alpha-tested, and the terrain layers |
| test on, write on, greater | 360 | 69 | `0xBEAD84BD72072E0E` / `0xA17D8AEC3A817D45` |
| test on, write off, greater | 188 | 354 | `0xD4D558DA6A82BDC8`, `0xA1F7E9885EC466DF`, `0xECD66A10092E6562` / `0x401A01FD4E5F8757` |
| test on, write off, greater-equal | 52 | 515 | `0x563E3BE17857DB59` / `0xFF19CE696F916E0C`, `0x29B6506FBACEB93A` / `0xDBFD88A80EDBCE36` |

| Cull (`PA_SU_SC_MODE_CNTL`) | Bridge | Walking | Rows |
|---|---|---|---|
| back, front face clockwise | 10,833 | 12,918 | every mesh draw (`"draw"` rows) that culls |
| back, front face counter-clockwise | 17,013 | 13,782 | every terrain draw (`"tess"` rows of both builders) |
| none (front face bit 0) | 690 | 1,758 | `0xA5846836C90E1192`, `0xD7DD31B9BF194DE8`, `0x2D40B53C926109BE` / `0x7CD57B81550F19E3`; `0x563E3BE17857DB59`, `0x29B6506FBACEB93A`, `0x775C6085FBB9D676` |

No row culls front faces and none sets both cull bits.

Colour mask (the shadow's value): RGBA in all 28,536 bridge rows and 28,425 walking rows; none in 33 walking rows (`0xF160B4DA459A6D40` with no pixel shader, depth-only draws the game itself issues; they execute at `0x80000003` and `0xC` with `RB_MODECONTROL` 5, all equal). Outside the main scene the mask is 0 in 19,957 of the 26,743 bridge rows (shadow maps and other depth passes).

State combinations (the five words together; 15 distinct in the bridge capture's main-scene rows, 18 in the walking capture's, 18 in both together), for the pipeline key and as real words for tests:

| `2200` depth | `2201` blend | `2202` alpha | `2205` cull | `2104` mask | Bridge | Walking | Rows | Largest shader pair |
|---|---|---|---|---|---|---|---|---|
| `0x00708767` | `0x01060106` | `0x00000004` | `0x00218002` | `0xF` | 13,954 | 12,149 | tess | `0xC30A97D946FA2BE4` / `0x9C2144E42E95A76F` (terrain layer, additive) |
| `0x00708767` | `0x00010001` | `0x00000004` | `0x00218006` | `0xF` | 4,351 | 6,484 | draw | `0xECD66A10092E6562` / `0x8D900846800943C8` (opaque) |
| `0x00708767` | `0x00010001` | `0x8700001C` | `0x00218006` | `0xF` | 4,268 | 3,804 | draw | `0x8123C16DBF583F92` / `0xA7E45D07F5CF6627` (alpha test greater, alpha-to-mask) |
| `0x00708766` | `0x00010001` | `0x00000004` | `0x00218002` | `0xF` | 2,253 | 674 | tess | `0x5003700B7C9B1C16` / `0x7DDAD5C29B3F56EE` (adaptive terrain, opaque) |
| `0x00708767` | `0x00060006` | `0x00000004` | `0x00218002` | `0xF` | 806 | 959 | tess | `0xC30A97D946FA2BE4` / `0x45243B758F9272AB` (terrain layer, source alpha over nothing) |
| `0x00708767` | `0x07060706` | `0x0000000E` | `0x00218006` | `0xF` | 450 | 451 | draw | `0x7C5710DEF3EE33C4` / `0x014F8A02DB7B19CA` (blended, alpha test greater-equal) |
| `0x00708767` | `0x00010001` | `0xAA000004` | `0x00218006` | `0xF` | 429 | 696 | draw | `0x475EC9F795E5EDBB` / `0x00E09D1BC5295D52` (opaque; alpha-to-mask offsets set, enable clear) |
| `0x00708767` | `0x00010001` | `0xAA00001C` | `0x00218000` | `0xF` | 367 | 365 | draw | `0xA5846836C90E1192` / `0x7CD57B81550F19E3` (alpha test, no cull) |
| `0x00708746` | `0x07060706` | `0x0000000C` | `0x00018006` | `0xF` | 360 | 69 | draw | `0xBEAD84BD72072E0E` / `0xA17D8AEC3A817D45` (blended, depth greater with write) |
| `0x00708766` | `0x00010001` | `0x0000000C` | `0x00018006` | `0xF` | 360 | 62 | draw | `0xBEAD84BD72072E0E` / `0xF6D98C7B4D98438B` (alpha test greater) |
| `0x00708767` | `0x00010001` | `0x8700001C` | `0x00218000` | `0xF` | 271 | 878 | draw | `0xA5846836C90E1192` / `0x7CD57B81550F19E3` |
| `0x00708767` | `0x00010001` | `0x0000000C` | `0x00218006` | `0xF` | 233 | 880 | draw | `0xD4D558DA6A82BDC8` / `0x401A01FD4E5F8757` |
| `0x00708767` | `0x00010001` | `0xAA00001C` | `0x00218006` | `0xF` | 194 | 54 | draw | `0x475EC9F795E5EDBB` / `0x00E09D1BC5295D52` |
| `0x00708743` | `0x07010701` | `0x00000004` | `0x00218006` | `0xF` | 188 | 354 | draw | `0xD4D558DA6A82BDC8` / `0x401A01FD4E5F8757` (premultiplied blend, depth greater, no write) |
| `0x00708763` | `0x01010101` | `0x0000000C` | `0x00218000` | `0xF` | 52 | 186 | draw | `0x775C6085FBB9D676` / `0xC300519EC8915346` (additive, no write, no cull) |
| `0x00708763` | `0x07010701` | `0x0000000C` | `0x00218000` | `0xF` | 0 | 329 | draw | `0x563E3BE17857DB59` / `0xFF19CE696F916E0C` |
| `0x00708767` | `0x00010001` | `0x00000004` | `0x00218006` | `0x0` | 0 | 33 | draw | `0xF160B4DA459A6D40`, no pixel shader (depth only) |
| `0x00708767` | `0x0D0C0D0C` | `0x00000004` | `0x00218006` | `0xF` | 0 | 31 | draw | `0xECD66A10092E6562` / `0x401A01FD4E5F8757` (blend constant) |

**What the census means for the later tasks.**

- *Blend factors, operations and compare functions in use* (on this list the plan's Task 4 was dropped, 2026-10-06: after mirroring for the reversed depth the RHI has all of them but the constant-colour factors). Factors: zero, one, source alpha, inverse source alpha, constant colour, inverse constant colour (the last two for 31 rows of one shader pair, with the blend constant as pipeline or dynamic state). Operation: add only. Depth compare: greater-equal and greater. Alpha test compare (a `discard` in the pixel shader): greater and greater-equal. Colour and alpha always use the same triple. No row uses a colour factor of source or destination colour, destination alpha, constant alpha or saturate, and none uses subtract, min or max.
- *Depth convention.* Every row with blending off has depth test on, write on, greater-equal (12,726 of 12,726 bridge rows, 13,930 of 13,930 walking rows); so do the terrain layers, which is what lets a layer draw over the base at equal depth. The depth range is reversed by the viewport, not by the projection: `PA_CL_VPORT_ZSCALE` is -1 and `PA_CL_VPORT_ZOFFSET` 1, both enabled in `PA_CL_VTE_CNTL`, in every sampled row, and the GPU holds the same words at every execution (next subsection; a first reading from the shadow alone is `task2_viewport_z.txt`, 3,729 rows of an eight-frame capture, script `task2_viewport_z.py`). Window depth is 1 - z/w, so the game's greater-equal is less-equal on the clip z/w that the native pass feeds its own depth test today (Less), and greater is less. By ruling the viewport Z scale is captured per draw and the depth function mirrored when depth is reversed.
- *Cull.* The mode is uniform per kind of draw, not over all opaque geometry: mesh draws cull back faces with clockwise front faces (9,835 of the 12,726 bridge rows with blending off; 12,013 of 13,930 walking), terrain patches cull back faces with counter-clockwise front faces (2,253; 674), two-sided foliage and particles cull nothing (638; 1,243). The front-face bit has to travel with the cull bits. Whether the captured modes agree with the emulated image (remove no face it shows) was not checked here and is carried to Task 6, which has the native pass to compare with; and for terrain it also depends on the winding of the grid the renderer builds itself. Done in Task 6: "Cull mode in the native pass" below.
- *Colour mask and the pre-pass.* The shadow's mask is the colour passes' mask. The pre-pass state never reaches the capture, which is right for a pass that draws each draw once in colour. The draws that execute only in the pre-pass (about 3 per frame, adaptive terrain) are told by the device's bin mask and are left out (next subsection).
- *Terrain* (Tasks 9 and 10). Every layer draw of `8221C9C8` blends: `0x01060106` (source alpha, add, one; 13,954 / 12,149 rows) or `0x00060006` (source alpha, add, zero; 806 / 959), with depth `0x00708767`, cull `0x00218002`, no alpha test, and no pre-pass execution. The adaptive builder `82207C30` draws opaque (`0x00708766`, `0x00010001`), with its pre-pass and colour executions in separate draws. All of these rows are in the proof (18,666 and 14,047 tess rows, all paired, equal outside the pre-pass).
- *Pipeline count.* 18 distinct combinations of the five words over both captures; the alpha reference is not part of them (six values).

**Cull mode in the native pass** (plan Task 6, 2026-10-06). The RHI has cull none, front and back and no front-face control; its front face is D3D12's default, clockwise in window space. The clay vertex shader writes the game's own clip position unchanged (`o.pos = c` in `kClayVs`), neither the pass's viewport nor the composite flips y, and the picture is upright, so a triangle has on the clay target the winding it has in the game's window space; the emulator maps the bit the same way (`front_counter_clockwise = pa_su_sc_mode_cntl.face == 0`, SDK `src/graphics/d3d12/pipeline_cache.cpp`). The mapping, `ClayCull` in `clay_logic.h` (tests 164 and 165 of `test_clay_logic.cpp`): with clockwise front faces (bit 2 set: meshes) the captured mode is the RHI's mode; with counter-clockwise front faces (terrain) front and back trade places, so the terrain's "cull back" is the RHI's cull front. The guest's index lists keep their winding through `BuildTriangleList` (odd strip triangles are swapped). The terrain grid is the renderer's own (`BuildGridIndices`); that its triangles have the winding of the game's tessellated patches is shown by the result below, not derived.

Evidence (native-view screenshots of autoplay runs, all live at 30.0 fps; the window's client area, 1918 x 1078 = 2,067,604 pixels, is compared, a pixel differs when a channel differs by more than 24 of 255; two shots of one run 30 s apart differ in 1,165 pixels, 0.06%):

| Scene | Mapping as above | Control: no culling | Pixels that differ | Where |
|---|---|---|---|---|
| Bridge, 90 s | `task6_bridge_native_shot_90s.png` (`task6_bridge_native_fable_2_222.log`) | `task6_ctl_nocull_bridge_native_shot_90s.png` (`..._fable_2_225.log`) | 4,402 (0.21%) | 4,133 in the plant cards left of the bridge (window x 100 to 760, y 500 to 680); of the other 269, 152 by the right rail (leaf cards, and a bird that also differs between two shots of one run), 50 in the thin trees at the far left, the rest in groups under 20 (mask `task6_diff_bridge_new_vs_nocull.png`) |
| By the fence after walking, 84 s and 100 s | `task6_walk_native_shot_84s.png`, `_100s.png` (`task6_walk_native_fable_2_228.log`) | `task6_ctl_nocull_walk_native_shot_84s.png`, `_100s.png` (`..._fable_2_229.log`) | 13,944 (0.67%) and 17,674 (0.85%) | all but 590 and 610 in the fern and grass cards below y 700; the rest in fern tips in front of the posts (366 and 369), plants at the left and right edges, the ivy cards at the upper right and the hero, who moves (masks `task6_diff_walk_new_vs_nocull_84s.png`, `_100s.png`) |

- With no culling every face is drawn and the depth test leaves the nearest, so an image equal to it outside the plant cards means the mapping removes no face that was visible: terrain (the lake bed, the ground under the bridge, the mountains; the pale ground behind the fence), meshes (planks, posts, rails, ropes, the tree trunk, the fence posts, the wall at the right), the hero and the dog are the same in both. Against the emulated frames `task2_bridge_shot_90s.png` and `task2_walk_shot_84s.png` each of these is where the game shows it.
- The cards that differ are, by where they lie, single-sided plant cards, whose words are `0x00218006` (cull back, clockwise front; the census's `0x475E...` / `0x00E0...` and the instanced plants): fronds that face away are drawn without culling and removed with it. The game culls them too by the proven state; against the emulated image this cannot be judged yet, because the cards are still whole rectangles (no alpha recipes).
- Control with the front-face bit inverted (`task6_ctl_flip_bridge_native_shot_90s.png`, `..._fable_2_227.log`): 502,491 pixels differ (24.3%). The lake bed and the ground are gone with the clear colour in their place, the mountains show only slopes that face away, and the meshes are inside out (the hero's front through his back, hollow planks and posts). A wrong mapping fails for terrain and for meshes at once, as their opposite bits imply; the mapping above is right for both kinds.
- The controls were temporary overrides of the decoded state in a scratch build, not committed. Limits: two views of one region; a mesh mirrored by its own transform was not looked for; the walking run does not always end on the same pixels (two of nine runs ended a few degrees off, `task6_ctl_old_walk_native_fable_2_236.log` and `task6_final_walk_native_fable_2_244.log`, and were not compared).
- Depth, for the record: with the game's own function, less-equal after mirroring, a control that drew every record with the old fixed state (`task6_ctl_old_bridge_native_shot_90s.png`, `..._fable_2_224.log`) differs from the bridge shot in 10,863 pixels (0.53%): 6,461 on the dog (its blended fur shells are now left out), 3,931 in the plant cards left of the bridge (culling), 471 elsewhere (the thin trees at the far left, the bird and the leaf cards by the right rail). No speckle of two surfaces at one depth appeared on the terrain or the bridge. By the fence the same control (`task6_ctl_old_walk2_native_shot_84s.png`, `_100s.png`, `..._fable_2_240.log`) differs in 16,804 and 19,439 pixels (0.81%, 0.94%), all but 25 and 36 of them below y 500, in the plant cards and on the hero: the trees, the ground and the fence behind are the same with either depth function.

**Alpha-to-mask and the main scene's multisampling** (plan Task 6, fix round 1, 2026-10-06). `RB_SURFACE_INFO` as the device holds it (+0x2880; written to `"draw"` and `"tess"` discovery rows as `"surface_info"`; the word was not part of the register proof above) is `0x11810460` in all 1,903 main-scene rows of an eight-frame bridge capture (`task6_fix1_native_discovery_20261006_205648.jsonl`): pitch 1120 and `msaa_samples` 1, two samples per pixel (the other passes of that capture have none, and four at pitch 280). The native pass has one sample per pixel and applies alpha-to-mask in the emulator's no-multisampling form (a pixel is kept iff alpha >= 1 - offset / 4, the offset picked by the pixel's parity from `RB_COLORCONTROL` bits 24 to 31; `AlphaToMaskCovers` in `clay_logic.h`), so an object fading under the dither offsets `0x87` is a stipple of whole pixels in four steps from 0.25 there, where the game tests its two samples apart (in the emulator: alpha >= 0.5 - offset / 8 and alpha >= 1 - offset / 8, eight steps from 0.125) and resolves them to a finer fade; under `0xAA` the game keeps one sample from 0.25 and both from 0.75, where the native pass cuts at 0.5.

### Pre-pass draws and the depth range

Fix round 1 of Task 2 (2026-10-06), after the review and three rulings: `RB_COLOR_MASK` is taken from the shadow as the colour passes' value; the draws that execute only in the depth pre-pass are settled here; the viewport Z scale is captured per draw and the depth function mirrored when depth is reversed, which needs the premise proven. Two more captures were taken with the same plans as before, with three more registers and the device's bin mask in `"rs"` and the GPU diagnostic extended by the same three registers.

**Result.** All thirteen registers are equal in every colour-pass execution and, but for the forced colour mask, in every pre-pass execution. One rule on the device's bin mask tells for every sampled row whether the draw is drawn in colour. The depth range is reversed in every sampled draw.

| | Bridge, standing still | After walking, camera moving |
|---|---|---|
| Capture | `native_discovery_20261006_174553.jsonl` | `native_discovery_20261006_174802.jsonl` |
| GPU log | `gpu_state_bridge_20261006_174553.log.gz` | `gpu_state_walk_20261006_174802.log.gz` |
| Checker output | `task2_r1_render_state_check_bridge.txt` | `task2_r1_render_state_check_walk.txt` |
| Sampled rows (draw + tess), every one with `"rs"` | 55,246 (36,582 + 18,664) | 44,684 (30,651 + 14,033) |
| Paired | 55,246 (100.00%) | 44,684 (100.00%) |
| Frame offset; probed rows confirmed; at the runner-up | +1587; 6,139 of 6,139; +1617: 509 | +1879; 6,384 of 6,384; +1884: 168 |
| Colour-pass executions; unequal in any of the thirteen registers | 80,490; 0 | 66,802; 0 |
| Pre-pass executions; unequal in any of the twelve other registers | 12,299; 0 | 13,652; 0 |
| Pre-pass `RB_COLOR_MASK`: GPU 0 against a device value other than 0; any other GPU value | 12,299; 0 | 13,652; 0 |
| `PA_CL_VPORT_ZSCALE`, `PA_CL_VPORT_ZOFFSET`, `PA_CL_VTE_CNTL` | equal in all 92,789 executions | equal in all 80,454 executions |
| Rows with no colour-pass execution | 93 | 107 |
| Colour-pass rule: predicted colour and has a colour execution / predicted colour and has none / predicted pre-pass only and has one / predicted pre-pass only and has none | 55,153 / 0 / 0 / 93 | 44,577 / 0 / 0 / 107 |
| Control: the next sampled row's packet passes the confirmation check too | 30,526 of 55,126 rows | 19,395 of 44,564 rows |
| Control: compared with the next sampled row's colour-pass executions, a register differs | 13,221 of 55,033 rows | 17,015 of 44,457 rows |
| Frames whose view differs from the frame before (`task2_r1_pass_sequences_*.txt`) | 0 of 120 | 118 of 120 |
| Checker | exit 0, `RESULT: OK` | exit 0, `RESULT: OK` |

**Runs.** `tools\drive_game.ps1` with the commands of "Captures" above (bridge: discovery delay 55, GPU log delay 53; walking: the same plan string, discovery delay 65, GPU log delay 63, `--dump_shaders`). Logs `task2_r1_bridge_fable_2_216.log` (30.0 fps, work medians 6.5 to 7.3 ms around the capture, the window with the capture 19.0 fps) and `task2_r1_walk_fable_2_217.log` (30.0 fps, work 5.4 to 6.9 ms, the window with the capture 18.5 fps): both live. Screenshots `task2_r1_bridge_shot_52s.png` / `_90s.png` and `task2_r1_walk_shot_65s.png` / `_71s.png` / `_84s.png`; the two looked at (`bridge_90s`, `walk_71s`) show the views of the first round. The diagnostic is `task2_r1_sdk_gpu_state_log.patch` (the first patch plus registers `0x2113`, `0x2114`, `0x2206`: 99 added lines); it is reverted, the SDK submodule is clean at `0c6d1de`, the runtime was rebuilt from it and the installed DLLs no longer contain the string `FABLE2_GPU_STATE_LOG`. A four-frame capture of the final build (`task2_r1_final_build_native_discovery_20261006_180322.jsonl`, log `task2_r1_final_build_fable_2_218.log`, 30.0 fps) has the fifteen `"rs"` keys and `cmd` in all 1,684 draw and tess rows.

**Discovery rows.** `"rs"` now holds thirteen registers (the ten above, then `2113` `PA_CL_VPORT_ZSCALE`, `2114` `PA_CL_VPORT_ZOFFSET`, `2206` `PA_CL_VTE_CNTL`, all through `DeviceRegShadowOffset`: +0x2918, +0x291C, +0x294C) and `"bin_mask_lo"` / `"bin_mask_hi"`, the device words at +0x31A4 / +0x31A8.

**Checker** (`render_state_check.py`, 38 tests). An execution whose bin select has no bit of `0x2AAAAAAA` is a depth pre-pass (in these captures: `0x80000001`); an untiled pass (bin select all ones) is a colour pass. Exit 1 for: any sampled row not paired (any, also one skipped by the bin predicate or not confirmed); a register unequal in a colour-pass execution; a register other than `RB_COLOR_MASK` unequal in a pre-pass execution; a pre-pass `RB_COLOR_MASK` that is not 0; a row the colour-pass rule gets wrong. A row with no colour-pass execution is counted as such, never as equal. The `RESULT` line carries the paired share. The tests give every register a difference in a high bit only, in a colour pass and in a pre-pass; 38 mutations of the checker (never comparing a register, comparing half of the bits, a narrower `uses_blend_constant`, either alternative of the confirmation alone, a wider exemption, the rule without the main-scene condition, and others) each fail at least one test.

**The bin mask (static).** Device +0x31A4 and +0x31A8 are the low and high word of the explicit bin mask, not a bin mask and a bin select. `0x822655B0` (r3 device, r4 mask; the function section 8 calls SetPredication-like) stores a non-zero r4 at +0x31A4 (`0x822656BC stw r4,12708(r31)`) and all ones for r4 = 0 (`0x822656A8 li r10,-1`, `0x822656AC stw r10,12708(r31)`), stores 0 at +0x31A8 (`0x822656D4 stw r9,12712(r31)`, r9 = 0 from `0x822655C8`), and writes `SET_BIN_MASK_LO` (`0xC0006000`, `0x822656F0`/`0x822656F4`) with the stored word (`0x822656FC lwz r11,12708(r31)`, `0x82265700`). The tiling state emitter restores `SET_BIN_MASK_LO` / `_HI` from the two words after its override (`0x8221594C`..`0x82215960`). Constants `kDeviceBinMaskLoOffset`, `kDeviceBinMaskHiOffset`, `kColourBins` (`0x2AAAAAAA`), `kPrePassBins` (`0x15555555`) and `HasColourPass(bin_mask_lo)` in `xdk_layout.h` (test `test_xdk_layout.cpp`).

**The bin mask (runtime).** The high word is 0 in every row. Rows by the device's low word, the bin selects they executed under and the GPU's bin mask at each execution (the checker's rule table):

| Where | Device bin mask | Rows of | Bin selects executed under | GPU bin mask at each | Bridge rows | Walking rows |
|---|---|---|---|---|---|---|
| main scene | `0xFFFFFFFF` | mesh draws (`DrawIndexedVertices`), `82207C30` | `0x80000001`, `0x2`, `0x8` | `0xFFFFFFFF`, `0x8000000F`, `0x8000000F` | 12,206 | 13,545 |
| main scene | `0xFFFFFFFF` | mesh draws (`DrawIndexedVertices`, `DrawVertices`) | `0x80000003`, `0xC` | `0xFFFFFFFF`, `0x8000000F` | 1,134 | 1,502 |
| main scene | `0x0003000A` | terrain layers `8221C9C8` | `0x80000003`, `0xC` | the device's | 11,666 | 6,967 |
| main scene | `0x00030002` | terrain layers `8221C9C8` | `0x80000003` | the device's | 3,094 | 6,136 |
| main scene | `0x2AAAAAAA` | adaptive terrain `82207C30` | `0x2`, `0x8` | the device's | 331 | 211 |
| main scene | `0x15555555` | adaptive terrain `82207C30` | `0x80000001` | the device's | 93 | 107 |
| other passes | `0xFFFFFFFF` | all builders | `0xFFFFFFFF` | `0xFFFFFFFF` | 26,722 | 16,095 |
| other passes | `0x15555555` | adaptive terrain `82207C30`, pitch 280 | `0xFFFFFFFF` | the device's | 0 | 27 |
| other passes | `0x2AAAAAAA` | adaptive terrain `82207C30`, pitch 280 | `0xFFFFFFFF` | the device's | 0 | 94 |

- An explicit mask reaches the GPU unchanged at every execution. Under no explicit mask (all ones) the GPU has `0x8000000F` from the second execution on: the library's own per-draw masks (section 8).
- Every `"draw"` row has all ones. Explicit masks occur only on terrain: every layer draw of `8221C9C8` has `0x0003000A` or `0x00030002`, and `82207C30` has `0x15555555`, `0x2AAAAAAA` or all ones.
- Each row executes under exactly those bin selects of its group that its mask intersects (`0x00030002` under `0x80000003` but not under `0xC`; `0x15555555` under `0x80000001` only; `0x2AAAAAAA` under `0x2` and `0x8` only). The two groups of bin selects, (`0x80000001`, `0x2`, `0x8`) and (`0x80000003`, `0xC`), never mix in a row.

**The rule for "pre-pass only".** A draw of the tiled main scene (inside the BeginTiling bracket, `"in_scene"`) has a colour-pass execution exactly when the low word of its device bin mask has a bit of `0x2AAAAAAA`: `HasColourPass`. It classifies every sampled row of both captures: predicted colour and executed in colour 55,153 and 44,577 (of them 28,431 and 28,361 in the main scene), predicted colour and not 0 and 0, predicted pre-pass only and executed in colour 0 and 0, predicted pre-pass only and not 93 and 107. The condition "in the main scene" is part of it: outside, the bin select is all ones and a draw executes whatever its mask (the 121 walking rows at pitch 280 in the table, of which 27 have `0x15555555`). Its reach: `0x15555555` is the only mask without a colour bin that occurs, and only in the group whose pre-pass is its own bin select; a mask of that kind among the draws executed under `0x80000003` / `0xC` was not seen.

**Which draws are pre-pass only.** In both captures of this round and both of the first, by builder and shader: only `82207C30` with vertex shader `0x5003700B7C9B1C16` and pixel shader `0x2F8A0341E8C09698` (93 and 107 rows here, 83 and 104 before), about three draws per frame. No other builder and no other shader pair has a row without a colour-pass execution. The GPU runs them once, under `0x80000001`, with `RB_MODECONTROL` 5 (depth only), so the pixel shader, a full colour shader, is not used. They are not a second issue of a colour draw: none of the 93 rows (3 distinct draws, the bridge is static) has the builder arguments and `c0` of one of the 11 distinct draws that `82207C30` issues under `0x2AAAAAAA`, and none of the 107 walking rows matches one of the 208 there (`task2_r1_extra_bridge.txt`, `task2_r1_extra_walk.txt`). What they fill depth for was not established (inference: ground whose colour comes from the layer draws of `8221C9C8`, which have no pre-pass of their own). A pass that draws each captured draw once in colour leaves them out.

**The depth range, proven.** `PA_CL_VTE_CNTL` is `0x0000043F` in all 55,246 and 44,684 rows, main scene and other passes: bits 4 and 5, the Z scale and the Z offset, are set. `PA_CL_VPORT_ZSCALE` is -1 (`0xBF800000`) and `PA_CL_VPORT_ZOFFSET` 1 (`0x3F800000`) in all of them, and the GPU's register file holds the same three words at every one of the 92,789 and 80,454 executions. Window depth is z/w * scale + offset = 1 - z/w: the depth range is reversed in every sampled draw, with no draw of either scene unreversed. Per draw the test is: reversed when bit 4 of `PA_CL_VTE_CNTL` is set and the scale is negative (the SDK takes the scale as 1 when the bit is clear, `src/graphics/util/draw.cpp`). With the census this gives, for a pass that keeps its own unreversed depth: the game's greater-equal is less-equal and its greater is less.

**What Task 3 must capture** (device words at the draw, all proven above): the six state words `RB_COLORCONTROL` 0x2202 (+0x293C), `RB_ALPHA_REF` 0x210E (+0x2904), `RB_BLENDCONTROL0` 0x2201 (+0x2938), `RB_COLOR_MASK` 0x2104 (+0x28DC), `RB_DEPTHCONTROL` 0x2200 (+0x2934), `PA_SU_SC_MODE_CNTL` 0x2205 (+0x2948); `PA_CL_VPORT_ZSCALE` 0x2113 (+0x2918) for the depth direction, with `PA_CL_VTE_CNTL` 0x2206 (+0x294C) bit 4 as the condition under which the scale applies (set in every row seen, so reading the scale alone gives the same answer on these captures); and the bin mask's low word (+0x31A4) to leave out the main-scene draws for which `HasColourPass` is false. The blend constant (0x2105..0x2108, +0x28E0..+0x28EC) is needed only for the constant-colour blend draws, which the ruling leaves out.

## Pending

1. **Pitch ablation** (done 2026-10-01, results in section 4; kept for re-runs). One run per significant pitch `<p>` (1120, 1040, 320, 1280, 560, 280):
```
cd out\build\win-amd64-release
& .\fable_2.exe --fullscreen=false --native_render_suppress_debug=true --native_render_suppress_pitches=<p>
```
   Record what disappears or breaks on screen and name the pass (shadow map, main scene, water, post, UI). Fill section 4 with `pitch | draws | observed effect when suppressed | pass name`. Suppression debug mode also fakes all occlusion queries, so objects that are normally culled may appear.
2. **Tiling re-capture** (done 2026-10-01, results in section 3b; kept for re-runs). The counters and hooks are in place: census rows now carry `gpu.bin_selects` (SET_BIN_SELECT* writes), `gpu.tiles` and `gpu.tile_selects` (distinct non-zero bin-select values; LO/HI half-writes can add an intermediate value), `gpu.pred_draws` (predicated draw packets executed), `gpu.pred_skips` (predicated packets skipped) and `gpu.extents` (per pitch: max window-scissor corner, i.e. tile size in EDRAM, and max viewport size). The 15 extra hooks from section 7 item 1 come from `tools\xdk_sigmatch\fable2_extra_hooks.json` (`0x822A6318` is skipped: `src/diagnostics/fps_probe.h` already overrides it). Regenerate with:
```
python tools\xdk_sigmatch\gen_census_hooks.py --map docs\native-renderer\xdk-map.json --init generated\default\fable_2_init.cpp --src src --out src\native\capture\xdk_hooks.inc --ids-out src\native\capture\xdk_hook_ids.h --extra tools\xdk_sigmatch\fable2_extra_hooks.json
```
   Re-run the capture procedure below in the world and summarize; the summary gains a Tiling section and an extents table. Menu smoke run (2026-10-01): predicated tiling is active even in the menu (about 11 bin-select writes, 181 predicated draws and 130 skipped predicated packets per frame).
3. **F3 attribution** (user, in the world). Run `fable_2.exe --fullscreen=false --vsync=false --guest_vblank_pacing=false`, open F3, and record the Guest line, the GPU emu line and the Verdict. Fills the gameplay attribution in section 5.

Capture procedure, for re-runs: from `out\build\win-amd64-release`, set `FABLE2_D3D_CENSUS=900` and `FABLE2_D3D_CENSUS_DELAY=<seconds until in the world>`, run `fable_2.exe`, wait for `[census] complete` in the game log, then clear both variables. Summarize with `python tools\xdk_sigmatch\summarize_census.py <capture> [--from-frame N --to-frame N]`.
