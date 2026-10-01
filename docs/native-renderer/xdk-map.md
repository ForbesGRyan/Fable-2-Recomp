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
