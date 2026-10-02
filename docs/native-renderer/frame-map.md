# Fable 2 frame map and sub-project 3 recommendation

Status: menu capture, gameplay census, pitch ablation and tiling capture complete; only the uncapped F3 reading is pending (see "Pending"). Sections 1b, 2c, 3, 5, 6b and 7 carry the gameplay results.
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
| `DrawIndx:8221C9C8`, `DrawIndx:82217EE8`, `DrawIndx:82207C30`, `BeginVertices?`, the seven `DrawIndx2` builders | not confirmed | Sampled r4 values do not read as a Xenos primitive type for `8221C9C8` / `82207C30` (0x12 in all 476 census samples) and the register meaning of r5-r8 was not traced; draw records give them `kUnsupportedPrim` (section 9) |

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

### Vertex shader microcode: three sources

The object's microcode is a template whose vertex fetches the XDK rewrites for the bound declaration and strides (`0x821D3318`). A draw row's `vs.source` records which bytes the GPU ran:

- `gpu_load` (gameplay `DrawIndexedVertices`): the template, loaded by `GpuLoadShaders`; hashes match `[vbind]` directly.
- `object` (`DrawVertices` with the device field set, patched in place): the object's microcode; hashes match.
- `immediate` (menu, and whenever device byte +0x2ABC has bit 0x80, `0x8221B4F0..0x8221B504`): `0x821DFDE0` (r3 device, r5 object, r10 variant) copies the template into the command buffer as `IM_LOAD_IMMEDIATE` (header `0xC0002B00 | (dwords + 1) << 16`, then 0, then the dword count; `0x821DFE34..0x821DFE6C`) and patches the copy; on return device+0x30 (`0x821DFF30`) points at the copy's last dword. The copy differs from the template in 14 dwords (menu, all 197) or 14/30 (gameplay), and its hash is the `[vbind]` one (197/197 menu, 194/194 gameplay).

### Limits

- Draw rows cover `D3DDevice_DrawVertices` and `D3DDevice_DrawIndexedVertices` only. The menu's 3D scene is drawn by the engine emitter `DrawIndx:82217EE8` (1260 of 1470 sampled menu draws in `native_discovery_20261002_094059`), which binds neither streams nor shaders through the D3D calls; how many `DrawVertices` a menu capture sees depends on timing (0 or 197 in two runs with the same settings).
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
- Rejected (`"rejected"` gives the reason): `0x79EAC49585797037` (position permuted by `cndeq` after the fetch, 0.204), `0xA1F7E9885EC466DF` (skinned with placement in the bones, 0.477), `0x87D4404FB36AF71D` (position from three fetches, 0.0), `0xD4D558DA6A82BDC8` (0.534), `0x695413A9831D88DA` (memexport particle pass, point lists), `0x475EC9F795E5EDBB` (0.719), `0xA5846836C90E1192` (0.773), `0x29B6506FBACEB93A` (0.600), `0x775C6085FBB9D676` (1 sample), `0x563E3BE17857DB59` (computed index and relative constants, 0.429).

`gen_transform_table.py` writes 12 entries to `src/native/capture/vs_transform_table.inc`.

### Draw records

`capture.cpp` builds one `DrawRecord` per outermost hooked draw inside the main-scene bracket while `fable2_native_render` is true. At each Swap, `capture::Publisher()` publishes `FrameBuilder::Finish(frame)`. Inputs:

- **Shader.** The device field, or the `GpuLoadShaders` fallback, or the immediate copy (section 8). A per-thread cache is keyed by shader object; it is refreshed when the microcode address, size or variant changes, when object dword 10 (the patched-declaration id) changes, or (immediate path) when the copy's hash changes. A steady-state draw does five small reads and no hashing.
- **Position.** `SelectPosition` with the table's `pos_fetch`, then `ApplyFetchEndian`.
- **Vertex and index buffers.** The vertex buffer comes from the position slot's stream object (dwords 6/7) plus the stream offset, and needs `fc_match`. The index buffer comes from object dwords 0/6/7. Records carry big-endian indices (`index_convert.h`), so a 16-bit buffer must be 8in16 and a 32-bit one 8in32; any other endian is `kBadIndex`.
- **Bank.** Only the transform's four registers are converted, into a per-thread 1024-float buffer.
- **Argument mapping (section 8).** `DrawIndexedVertices` and `DrawVertices` as mapped. All other draw hooks get prim 0, which becomes `kUnsupportedPrim`.

Capture-side skip reasons replace `AssembleRecord`'s reason, unless that reason is unsupported-prim (`ResolveSkip`, `CountSkip` and `kMaxDrawCount` in `draw_record.h`, tested in `test_frame_scene.cpp`). The capture stops filling inputs at the first problem, so `AssembleRecord` alone would report, for example, an oversized draw as unknown-shader. The capture-side reasons are:

- index or vertex count above 4,194,304: `kBadMemory`, before any index read;
- indices unreadable: `kBadMemory`;
- largest index plus base vertex at or past the position stream's vertex count, or a negative base vertex: `kBadIndex` (this covers the `pos_suspect` instanced draws);
- non-indexed start + count past the stream: `kBadIndex`.

**Nesting.** A per-thread `DrawNesting` (`draw_nesting.h`, test `test_draw_nesting.cpp`) is entered in `OnXdkCall` and left in `OnXdkReturn` for draw hooks. Only the outermost draw is recorded. Statically, `DrawIndx2:82BA5CE8` calls `0x821969E0`, which calls `DrawIndx2:82B988C8` and `DrawIndx2:82B98770`; the `DRAW_INDX` builders call no other draw hook. At runtime the coverage log's `nested_total` stayed 0 through menu, loading and gameplay, so no nesting occurred in these runs.

**Inactive path.** `OnSwap` returns after one relaxed atomic load when neither the renderer nor discovery is on.

### Read cost

`guest_read.h` checked every page with `VirtualQuery`. On the guest arena's file-mapped views that took about 0.47 ms per call (25k calls = 11.6 s in a timed run). Per-draw records then ran the game at 2.5 frames/s (census `guest_ms` 390-410 ms), and discovery wrote about one frame per second. Reads now check the guest heap page tables instead. `BaseHeap::QueryRegionInfo` reports committed and readable for a whole region, through `Memory::LookupHeap` for virtual addresses and `Memory::GetPhysicalHeap` for physical ones. Those two SDK symbols were added to `rexruntime.def`. A per-thread, per-frame `RegionReadCache` (`page_cache.h`, test `test_page_cache.cpp`) holds 64 regions. Measured cost: records 1.2 ms per gameplay frame (1477 records); discovery 60 frames every 16th draw in 2.3 s.

### Coverage (gameplay)

Run with `.\tools\drive_game.ps1 -Total 120 -GameArgs "--fable2_native_render=true"` (`fable_2_097.log` and `fable_2_099.log`; after Task 11 fix round 1, `fable_2_100.log`, with identical counts and the deformed count added). Every 300 frames:

```
[native] capture: frame 1200 captured 1477 drawable 921 (deformed 5) skipped {no-transform: 229, unsupported-prim: 324, bad-index: 3} nested_total 0
[native] capture: frame 1200 unsupported by hook {D3DDevice_DrawVertices?: 21, D3DDevice_BeginVertices?: 3, DrawIndx:8221C9C8: 255, DrawIndx:82217EE8: 10, DrawIndx:82207C30: 31, DrawIndx2:821EF988: 4}
```

Frames 1200 to 3300 all report the same counts (the player stands still). The menu and loading frames have no main-scene draws, apart from 2 `DrawIndx2:821EF988` draws per frame from frame 600. D / C = 921 / 1477 = 0.62, below the 0.9 target. The shortfall by reason:

| Reason | Draws per frame | Share of C | Cause | Next action |
|---|---|---|---|---|
| unsupported-prim | 324 | 21.9% | `DrawIndx:8221C9C8` 255 (single caller `0x8221FD60`, r4 = 0x12), `82207C30` 31, `82217EE8` 10, `DrawIndx2:821EF988` 4, `BeginVertices` 3: argument mapping not confirmed. `DrawVertices` 21: point and rectangle lists | Trace `8221C9C8`'s arguments (how r4-r8 reach its `DRAW_INDX` header and which streams/shader it binds). It is the single largest gap |
| no-transform | 229 | 15.5% | Rejected shaders, mainly `0x79EAC49585797037` (~95/frame, permuted position) and `0xA1F7E9885EC466DF` (~90/frame, skinned with placement in the bones) | Per-shader position remap for `0x79EA...`; bone-aware transform (or keep skipping) for skinned meshes |
| bad-index | 3 | 0.2% | Instanced draws whose first fetch is per-instance data (`pos_suspect`) | Instancing support, later |

Only 22 shaders were sampled, all from one spot in Bowerstone. Other areas will bring shaders that are not in the table yet. Every sampled shader that draws uses `c0..c3`, so a default of base 0 dot for unknown shaders would cover most of them. That is a design decision for Tasks 12-14, not taken here.

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
