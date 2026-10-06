# Native renderer sub-project 5: coverage and skinning

Status: approved design (brainstorming, 2026-10-05). Implements sub-project 5 of
`docs/superpowers/specs/2026-10-01-native-renderer-foundation-design.md` ("skinned
characters"), widened to the main-scene draws that are still not drawn. Builds on
sub-projects 3 and 4 (`2026-10-01-native-renderer-clay-pass-design.md`,
`2026-10-02-native-renderer-textures-design.md`); evidence in
`docs/native-renderer/frame-map.md` sections 9 to 11.

## Goal

Draw the main-scene geometry the clay pass still skips or draws in the wrong pose:
instanced meshes, characters with weighted bone skinning, and the wind and displacement
shaders (without their sway). Newly drawn meshes get their albedo textures where the
pixel shader is already in the albedo table. This stays a debug view: no lighting, no
frame replacement.

## Context

Bowerstone capture `native_discovery_20261002_194918` (300 frames, every 8th draw):
14391 in-scene non-terrain draw rows, 6708 drawn (46.6%).

| Group | Rows | Share | Why not drawn (or wrong) |
|---|---|---|---|
| Instanced: `0x8123C16DBF583F92` 2957, `0xB636821F95DC9D8E` 1424, `0x6AD4108C3FF07966` 299, `0x48D30ECCF684F488` 157, `0xFC4F2EF6930768BE` 98, `0x33C00226C154C1F2` 80, `0x36B543CECAE5C781` 32 | 5047 | 35% | The first full vertex fetch is per-instance data from stream 1 (slot 94); the capture takes it as the position stream and skips the draw as `bad-index` (or `no-transform`). |
| Wind and displacement: `0x475EC9F795E5EDBB` 1149, `0xA5846836C90E1192` 834, `0x7C5710DEF3EE33C4` 571 | 2554 | 18% | Rejected by the matrix finder (scores 0.72 to 0.77) or no table entry; their dumps show `dp4 oPos, c0..c3` on a displaced position. |
| Skinned bodies: `0xD4D558DA6A82BDC8` | 1018 | 7% | Drawn in bind pose (`deformed`), so rigidly attached meshes (eyes, sword) sit apart from the body. |
| Billboards: `0x29B6506FBACEB93A` 47, `0x775C6085FBB9D676` 35 | 82 | <1% | Corners computed from relative constants. |

Shader facts (SDK dumps in `out\shader_dump`):

- `0x8123...` (instanced): `copy = trunc((idx + c254.z) * c12.x) + trunc(c12.z)`,
  `vertex = idx - trunc((idx + c254.z) * c12.x) * c12.y`; three `16_16_16_16_FLOAT` rows
  fetched from vf1 at `[copy]` (stride 7 dwords, offsets 0, 2, 4, plus an `8_8_8_8` at 6);
  mesh position from vf0 at `[vertex]` (stride 6); `world = rows * p + c7.xyz`; a
  per-vertex distance cut (predicated: `|c9.xyz - world|^2 > c13.z` writes the NaN literal
  `c255.xyz` as the position), then a sway on the default path; `oPos = dp4(c0..c3,
  world)`. Sampled `c12 = (0.0625, 16, 26, 0.3333)`. `c254`/`c255` are shader literals
  (zero in the device bank). `0xB636...` follows the same scheme with one more vf0 fetch.
  (Corrected 2026-10-06 after Task 7: the predicate was first read as an optional sway.)
- `0xD4D5...` (four-bone blend): bone indices are an integer `8_8_8_8` at dword 3, weights
  a normalized `8_8_8_8` at dword 4 (all four explicit), three `16_16_16_16_FLOAT` rows
  per bone from vf3 (slot 92, 6 dwords per bone, offsets 0, 2, 4); the rows are blended by
  weight, applied to the position, then `oPos = dp4(c0..c3, ...)`. A `cexec b0` block
  between the blend and the transform can overwrite x and y. `0x3A0F9098B839DDBC`,
  `0x82F6433A69263C75` and `0x9ED0BA440DBD51D4` share the layout; `0x5F4416192E87005F`
  uses one bone.
- `0xA1F7E9885EC466DF` already skins rigidly through `rigid_skin.h` (one bone index, three
  rows from the same palette stream), on the CPU in the geometry cache, re-decoded when
  the vertex or palette bytes change (0.2 to 0.4 ms per frame for about 10 streams).
- `0x475E...`: position `r1.wxyz` (half4, stride 9) plus a `frc`/`sin` sway.
  `0x7C57...`: float3 position plus a displacement along the normal. `0xA584...`:
  `pivot + (position - pivot) * factor` with sway, where the pivot is another vertex
  element.

One-stream assumptions today: `SelectPosition(-1)` takes the first non-mini fetch;
`DrawRecord` has one `vb`/`pos`; `FillDrawInputs` rejects when the largest index reaches
past that stream; the clay vertex shader reads `positions[indices[vid] + base_vertex]`.

## Decisions

- **Scope:** instancing, weighted skinning (1 to 4 bones) and the three wind/displacement
  shaders drawn undeformed. Billboards, particles, point lists and the two unmapped draw
  builders (`DrawIndx:82217EE8`, `DrawIndx2:821EF988`) stay skipped with reasons.
- **Approach A: per-vertex work on the CPU in the geometry cache.** Instanced draws
  become a flat position buffer indexed by the guest index, so the index path and the clay
  shaders do not change. Skinning extends the existing rigid path. Rejected for now: GPU
  skinning and instancing in the clay vertex shader (three shader variants, more
  bindings, harder to test); it is the follow-up if CPU cost becomes a problem or when
  lighting needs skinned normals, and it needs no change to what the capture records.
- **Textures follow:** new vertex shaders get `"uv"` entries so their draws use the
  albedo entries sub-project 4 already has.
- **Evidence first:** every table entry is derived from the shader dump and accepted only
  after an offline check on captured bytes, then a screenshot.

## Architecture

```
guest draw -> capture (guest thread, table lookups only)
   vs-transforms.json entry may carry
     "instance": row fetches, mesh fetch, index constants, offset constant
     "skin":     index fetch, weight fetch, row fetches, index/weight pairing (1..4 bones)
     "deformed": wind and displacement shaders, drawn undeformed
   -> DrawRecord gains
        instances { instance stream, row layouts, count, inv_count, first, bias, offset }
        skin      { palette stream, index and weight layouts, bone count }
      and the mesh stream comes from the table's position fetch
FrameScene publish (unchanged)
render thread, geometry cache
   instanced -> positions[i] = rows(copy(i)) * mesh[vertex(i)] + offset, uvs[i] = mesh_uv[vertex(i)]
   skinned   -> positions = sum(weight_k * rows(bone_k)) * p, rebuilt when bones change
clay pass, texture cache, composite views (unchanged)
```

### Components and files

| Unit | Purpose |
|---|---|
| `src/native/capture/instance_expand.h` (pure) | Index to (copy, vertex), bounds, flat position and UV build. |
| `src/native/capture/bone_skin.h` (pure, replaces `rigid_skin.h`) | Palette decode, 1 to 4 weighted bones, layout selection. |
| `docs/native-renderer/vs-transforms.json`, `tools/xdk_sigmatch/gen_transform_table.py`, `src/native/capture/vs_transform_table.inc` | `"instance"` and the extended `"skin"`. |
| `tools/xdk_sigmatch/position_check.py` | Offline replay of instancing, skinning and plain entries on discovery rows. |
| `src/native/capture/capture.cpp`, `draw_record.h` | Second stream resolve, new record fields, instance-aware index check, discovery dumps. |
| `src/native/render/geometry_cache.{h,cpp}`, `clay_logic.h` | Instanced and skinned position builders, flat UVs, cache keys, stats. |

## Instancing

- Table entry: `"instance": {"mesh_fetch": n, "row_fetches": [a, b, c], "inv_count":
  "c12.x", "count": "c12.y", "first": "c12.z", "bias": <literal>, "offset": "c7.xyz"}`.
  Fetch numbers are `DecodeVertexFetches` ordinals; the generator records each fetch's
  format and offset so a mismatch at runtime rejects the entry.
- `bias` is a shader literal (`c254.z`, or `c254.w` in `0xB636...` and `0x36B5...`); its
  value comes from the literal table the game uploads with the shader (read in Task 7 with
  a discovery diagnostic, raw dwords in frame-map section 12) and is recorded in the
  evidence.
- Capture: read the named constants, resolve the instance stream and the mesh stream,
  compute the copies available (`instance stream bytes / stride`) and mesh vertices, and
  reject (`bad-index`) when the largest index maps past either. Non-finite or
  non-positive counts are `bad-index`.
- Geometry cache: build `positions[0..max index]` and, for textured draws,
  `uvs[0..max index]`; key = both streams' addresses, sizes and layouts plus the
  constants; content hash = both streams' bytes.
- Shaders: `0x8123...` and `0xB636...` first; the other five when their dumps show the
  same scheme, otherwise `instance-unsupported` with the reason recorded.
- A shader that moves the position after the rows (sway, push) is drawn without the move
  and counted `deformed`.
- Distance cut (added 2026-10-06 after Task 7 measured that 48% of the instanced positions
  in the bridge scene are ones the game drops, 40 of 135 draws wholly): the table entry
  names the two constants, `"cut": {"eye": "c9.xyz", "dist2": "c13.z"}` inside
  `"instance"`. Capture copies them into the record per draw. The clay vertex shader tests
  the flat world position it reads (the same point the game tests, before sway or push)
  and writes a NaN position when `|eye - world|^2 > dist2`, so a triangle with a cut
  vertex is dropped as in the game. Records without a cut carry `dist2 = +inf`. The two
  constants are not part of the flat-stream cache key or content hash: the camera moving
  does not rebuild buffers. `position_check.py` applies the same test and scores the
  in-clip share on the vertices the game keeps.

## Skinning

- Table entry: `"skin": {"index_fetch": n, "weight_fetch": m, "row_fetches": [a, b, c],
  "pairs": [[index component, weight component], ...]}`. No `weight_fetch` means one bone
  with weight 1 (today's rigid form; `0xA1F7...` keeps its entry).
- The pairing and the row component order are read from the dump instruction by
  instruction and recorded as evidence.
- Position: `M = sum(weight_k * rows(bone_k))`, `p' = (M.row0 . p, M.row1 . p,
  M.row2 . p)`, then the entry's transform.
- Discovery records whether the `cexec b0` condition is set for in-scene draws; if it
  is, those draws stay in bind pose (`deformed`).
- Capture resolves the palette stream as it does for rigid skin; the geometry cache
  re-skins when vertex or palette bytes change. A bone index past the palette culls the
  vertex. Skinned draws are no longer counted `deformed`.
- Shaders: `0xD4D5...`, then `0x3A0F...`, `0x82F6...`, `0x9ED0...` (same layout) and
  `0x5F44...` (one bone).

## Wind and displacement shaders

- `0x475E...` and `0x7C57...`: entries with base 0, dot, `deformed`, the position fetch
  and swizzle from the dump.
- `0xA584...`: the plain position is accepted only if the offline check and a screenshot
  show it lands correctly; otherwise it stays rejected with the reason.

## Textures

- Each new vertex shader gets a `"uv"` entry: from `shader_trace.py` where it traces, by
  hand (instruction by instruction, recorded) where it refuses.
- Instanced draws take UVs from the mesh stream by vertex number into the flat UV buffer.
- A draw without a UV entry is drawn as flat clay with `uv-unsupported`.

## Evidence and checks

- Discovery rows for table-instanced and table-skinned draws gain a bounded dump of the
  streams they need (instance rows, mesh vertices, palette, index and weight bytes) and
  the named constants.
- `position_check.py` replays the entry's math and reports, per shader: the share of
  sampled draws with at least half their vertices inside the clip volume under the draw's
  own `c0..c3` (accept at 90%); for skinning also the weight sums and bone indices in
  range.
- Screenshots in split view confirm alignment (instances) and pose (eyes and sword on the
  body).

## Stats (F3 and log)

- New skip reasons `instance-unsupported` and `skin-unsupported`.
- `Geometry:` line gains `instanced I, skinned K`.
- The per-shader log breakdown tags `(instanced)` and `(skin)`.

## Failure handling

- An entry that does not match the decoded fetches (format, offset, stream): the draw is
  skipped with `instance-unsupported` or `skin-unsupported`; nothing is drawn from
  guessed data.
- Garbage constants or indices: `bad-index`, with no out-of-range read; flat buffers obey
  the existing draw-count cap.
- Unreadable streams: `bad-memory`.

## Testing

- Standalone clang tests (TDD) for the index math (bias, edges), the flat position and UV
  build (bytes cut from a real capture), weighted skinning (1 and 4 bones, pairing, bone
  past the palette) and record assembly with the new fields.
- Python tests for the generator's new fields and `position_check.py`.
- Gameplay only through `tools\drive_game.ps1` (in-process autoplay).

## Success criteria

1. At least 90% of main-scene non-terrain draws are drawn in the scene the autoplay save
   loads.
2. Split and overlay screenshots show the hero and the dog in their animated pose with
   eyes and sword attached.
3. Screenshots show instanced meshes lined up with the emulated image.
4. 30 fps with a clay view on; view-off capture overhead under 0.5 ms per frame.
5. User checks (logged in `docs/native-renderer/user-checks.md`): alignment in town, field
   and interior with characters moving; a 10-minute run with an area transition; a
   Bowerstone-streets coverage figure.

## Non-goals

GPU skinning or instancing, skinned normals, wind animation, billboards and particles,
point lists, the unmapped draw builders, Bower Lake table entries (user capture pending),
lighting, alpha, frame replacement.

## Risks

- **Skinning component orders read wrong:** visibly broken pose; caught by the offline
  check and the screenshot; fallback is bind pose, flagged.
- **An instanced shader that differs from the `0x8123...` scheme:** stays
  `instance-unsupported`; the two large shaders are 87% of the group.
- **CPU skinning cost in crowds:** measured and reported; GPU skinning is the follow-up.
- **The autoplay save loads the bridge scene, not the town:** instanced scenery may be
  sparse there; evidence then rests on the Bowerstone capture through the offline check,
  and the on-screen check is a user check.
