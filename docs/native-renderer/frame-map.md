# Fable 2 frame map and sub-project 3 recommendation

Status: capture half complete (static + 3D scene segments); gameplay half pending (see "Pending: gameplay capture").
Capture used: `out\build\win-amd64-release\logs\d3d_census_20261001_130522.jsonl` (600 rows). Row 1 accumulates every call since process start and is dropped by `summarize_census.py`. The capture has two distinct segments (section 2): frames 2-480 are a static screen (likely splash/loading/title) and frames 481-600 are a 3D scene. Treat the 3D segment as the one that matters. This capture predates `gpu.gpu_frame`, so repeated GPU frames could not be dropped.

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

## 2. Frame statistics by segment (real capture)

The capture is two segments, summarized with `summarize_census.py --from-frame/--to-frame` (row 1 dropped):

- Frames 2-480 (479 frames): static screen, likely splash/loading/title.
- Frames 481-600 (120 frames): 3D scene. Frame 481 (19.5 ms IssueDraw CP time, 9.7 ms resolve CP time, 244 draws) is the first 3D frame and most likely pipeline/shader creation, so steady-state claims below use frames 482-600 (119 frames).

Commands:
```
python tools\xdk_sigmatch\summarize_census.py <capture> --from-frame 2 --to-frame 480
python tools\xdk_sigmatch\summarize_census.py <capture> --from-frame 482 --to-frame 600
```

### 2a. 3D scene (frames 482-600, steady state; the segment that matters)

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

## 3. Gameplay frame / 4. Pass list

See "Pending: gameplay capture" below.

## 5. CPU vs GPU emulation

What the capture supports (3D segment, frames 482-600; the static screen is similar but irrelevant):

| Quantity | Median | Mean |
|---|---|---|
| guest frame time (`guest_ms`) | 33.32 ms | 33.32 ms |
| swap interval (`swap_interval_ms`) | 33.25 ms | 33.34 ms |
| emulated IssueDraw CP time (`draw_cpu_ms`) | 0.308 ms (max 5.27 ms) | 0.436 ms |
| resolve CP time (`copy_cpu_ms`) | 0.077 ms | 0.079 ms |

Scope of the timing counters: `draw_cpu_ms` is command-processor CPU time inside `IssueDraw` only. It excludes PM4 packet parsing, the present path and GPU execution time, so it is not a measure of total command-processor or GPU cost. The data therefore supports only this claim: the menu and early 3D scene are vsync-locked (guest frame time tracks the 33.3 ms swap interval at both median and mean, a 30 Hz lock) and not emulation-bound there. It does not say how expensive gameplay emulation is: the 3D scene has 281 draws per frame, gameplay will have more, and `draw_cpu_ms` scales with draws (frame 481 spiked to 19.5 ms).

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

## 7. Recommendation for sub-project 3 (PROVISIONAL, pending gameplay data)

Based on the 3D segment (frames 482-600) and the identified functions. Do not base pitch decisions on the static screen (frames 2-480).

1. **Draw coverage first (function-trace fallback).** About 87-88% of 3D-scene draws are unhooked and `DrawIndexedVertices?` never fires even in 3D. Put the function-trace call-count correlation fallback first in sub-project 3: correlate per-frame call counts of candidate functions (all functions, not just the mapped ones) against `gpu.draws` to find the real draw submission path, before building any capture on `0x8221C518` or `0x8221E0F0` alone. The census already holds the strong symbols (`FABLE2_D3D_CENSUS_HOOK` overrides with callers and r3-r8 samples) that sub-project 3 will need; plan a shared dispatch so the census becomes an observer called from the capture hooks rather than a second set of overrides on the same functions.
2. **Draw function to capture first:** `D3DDevice_DrawVertices?` at `0x8221C518`, reached from `0x821CA7E8` (LR `0x821CB18C`), is the only draw entry that provably executes (115 of 120 3D frames). Verify `0x8221E0F0` as DrawIndexedVertices in gameplay before building an indexed path on it.
3. **Pitches:** the 3D scene has the largest draw count on pitch 1280 (158 median per frame) and secondary targets 560 (79), 320 (37), 280 (4), 160 (2). Which of these is plain world geometry, shadow maps or post is unknown until the gameplay ablation (pending section). Use `--native_render_suppress_pitches=1280` only after that confirms the world pass has its own pitch; if shadow or scene targets share 1280, suppression must be narrowed by draw index. For `native_render_keep_pitches`, start from the secondary pitches (560/320/280/160) that must still present, not from pitch 640, which only appears on the static screen.
4. The emulation-cost data are inconclusive for the performance case (section 5): `draw_cpu_ms` excludes most command-processor work. A full CP busy-time counter is a sub-project 3 prerequisite.

All of the above is provisional until the pending gameplay capture is filled in.

## Pending: gameplay capture

Needs the user to load a save in the game (cannot be automated). Results fill sections 3, 4, the gameplay half of 5, and finalise 7.

1. Capture (separate run). The census log goes to the exe folder, so run from there; the arming delay lets you load a save first (frames before it are not recorded or counted):
```
cd out\build\win-amd64-release
$env:FABLE2_D3D_CENSUS = "900"
$env:FABLE2_D3D_CENSUS_DELAY = "90"   # seconds from the first guest frame until you are standing in the world
& .\fable_2.exe
```
   Completion shows as `[census] complete: 900 frames written to ...` in the game log (`out\build\win-amd64-release\logs\fable_2_<n>.log`); stop the process after that. The file is `out\build\win-amd64-release\logs\d3d_census_<timestamp>.jsonl` (the `[census] writing` line gives the exact path). Clear both env vars afterwards.
2. Summarize (add `--from-frame N --to-frame N` to isolate a segment, e.g. to skip a load hitch):
```
python tools\xdk_sigmatch\summarize_census.py out\build\win-amd64-release\logs\d3d_census_<gameplay>.jsonl > gameplay.md
```
   Paste `gameplay.md` into section 3.
3. Ablation, one run per significant pitch `<p>` from gameplay.md (emulated frames, no native renderer):
```
& .\out\build\win-amd64-release\fable_2.exe --native_render_suppress_debug=true --native_render_suppress_pitches=<p>
```
   Record, per pitch, its median draw count and what disappears or breaks on screen; name each pass (shadow map, main scene, water, post, UI). Note: suppression debug mode also fakes all occlusion queries, so objects that are normally culled may appear.
4. Fill in:
   - Section 4: table `pitch | draws | observed effect when suppressed | pass name`.
   - Section 5: compare `guest_ms_median` to `swap_interval_ms_median` (vsync-locked or not); a CP busy-time counter is needed to attribute any shortfall.
   - Section 6: gameplay top callers (check whether `0x8221E0F0` is hit, and by which LR).
   - Section 7: replace the provisional text with the largest plain-world-geometry pass, its pitch for `native_render_suppress_pitches`, the pitches for `native_render_keep_pitches`, and the draw function plus caller to capture first.
