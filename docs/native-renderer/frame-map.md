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

Sub-project 5 (spec `docs/superpowers/specs/2026-10-05-native-renderer-coverage-skinning-design.md`, 2026-10-05). Evidence capture for every entry of this section: **`native_discovery_20261005_103213`** (autoplay, bridge scene, 120 frames, every 4th draw, 11523 in-scene draw rows, 17 vertex shaders), with the vertex streams of its rows in `native_geo_20261005_103213`. Entries are replayed offline by `tools\xdk_sigmatch\position_check.py <capture> --json docs\native-renderer\vs-transforms.json [--entry <candidates.json>]`. A draw passes when at least half of its sampled vertices are inside the clip volume under its own `c0..c3`; a shader is accepted when its share of passing draws reaches the capture's baseline (the share of the trusted `0xECD66A10092E6562`) minus 0.10, floor 0.60. This capture: baseline 0.810, accept at 0.710 or more.

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
  - The entry draws the plain `P`, not the rebuilt position. The wind turns each vertex about its pivot at a fixed distance and shifts the pivot; over the 8890 sampled vertices of the frame's 22 draws `|P - Q|` is 1.1 units at the median, 7.7 at p99 and 12.4 at most, so the pivots are branch attachments, not the vertices themselves. What makes the plain `P` acceptable is observation, not a bound: `P` projected over the view-off screenshot, which has the wind applied, lands on the leaf clumps of the emulated trees, and native view shows the leaf cards on the emulated crowns. That was judged by eye; the sway itself was not measured.
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
- The failing draws are whole meshes outside the view. `0x475E...`: 109 of 200 fail, 54 wholly beside the view, 41 mostly behind the camera, 14 across an edge. `0xA584...`: 94 fail, 27 beside, 33 behind, 34 across an edge. The frame's 30 `0x475E...` draws are static (the autoplay camera does not move): 13 pass and 15 have no sampled vertex on screen. The game draws every part of a tree whose bound touches the view, and trees behind the camera (section 9 found the same for `0xA1F7...`: 150 of 460 rows behind).
- Projection onto the emulated frame. The replayed positions of every distinct draw, projected with the row's `c0..c3` and plotted over a view-off screenshot of the same autoplay scene (window shot at 95 s): with `yxw1` the `0x475E...` points are vertical lines on the birch trunks left of the bridge and on the sapling behind the hero, and the `0xA584...` points sit on the leaf clumps of the same trees. One group of stems and leaves projects onto the bridge deck beside the left post: a shrub under the bridge. The deck hides its stems in the emulated frame and in the clay view alike, and its top shows through the railing in the emulated frame. With `wxy1` the trunks lie horizontal across the bridge; with the fetch's own `w` the `0xA584...` meshes collapse into dots. The 4337 vertices of the `0x7C57...` buffer cover the sitting dog.

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
- Checker: `position_check.py` applies the same test in float32 to the replayed flat positions. A draw passes when at least half of its kept vertices are inside the clip volume; a draw with no vertex kept is counted `cut` and leaves the share. The line ends with `cut N, kept vertices K of S`.

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
- `r2 = cndeq(c255.xxxy, r7.zxyy, c255.yyyy) = (p.z, p.x, p.y, 1)` (instr 28; `c255 = (0, 1, 0.5, 0)`, "Literals" above), with `p = r7` as fetched.
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
