# Light formulas of the scene shaders

Native renderer sub-project 7 (lighting), plan Task 2, 2026-10-07. This is the "Formulas" text of frame-map section 14: per family of pixel shaders the closed formula of the game's ambient, baked and sun light, where each input comes from, the instruction numbers of the reading, what the machine check switched off, and how many of the scene's draws each family covers. Nothing here was run in the game; every statement rests on the shader dumps (`out/shader_dump/shader_<HASH>.ucode.frag` / `.vert`), on the two captures of the materials validation (`out/native-evidence/materials/task11_bridge_up2_capture/native_discovery_20261007_123345.jsonl` at the bridge, `task11_trees_up1_capture/native_discovery_20261007_123632.jsonl` by the trees; 8 frames each, every draw) and on the literal tables of capture `out/native-evidence/native_discovery_20261005_123635.jsonl`.

The formulas are the pure functions of `tools/xdk_sigmatch/light_formulas.py`. They are the reference the C++ and HLSL of the later tasks are pinned to. A formula is in that file only if `tools/xdk_sigmatch/light_check.py` reproduces the microcode's colour with it. An input whose value is open or in no capture row has no default there and must be passed by keyword: the terrain layers' `sun_gate` (it carries the facing factor of `c109`), `c49`, and the leaves' `m1` and `m2`. The defaults that remain are values every captured row has (`c11x` = 1 at layer rows, `c27w` = 1) and a mask of 1 for the objects' and plants' `sun_gate`.

## Result

| Family | Pixel shaders | Vertex shaders it was checked with | Function | State |
|---|---|---|---|---|
| `terrain-layer` | `9C21...`, `4755...` | `C30A...`, `FB68...` | `terrain_layer` | closed; needs pixel `c109` (reads zero in the captures, as `c80` and `c87` do) |
| `terrain-layer-multi` | `4524...`, `04F2...`, `AF1E...`, `5783...` | `C30A...`, `FB68...` | `terrain_layer` with `sun_gate_multi` | closed; light 0 also reads `c80.w`, `c94.x`, `c96.x`, `c98`, `c105.x`, `c107.x`, `c112.x` |
| `terrain-far` | `7DDA...`, `2F8A...` | `5003...` | `terrain_far` | closed |
| `object` | `8D90...`, `2FD2...` | `ECD6...`, `79EA...`, `D4D5...`, `A1F7...` (`2FD2...`: `79EA...`, and `ECD6...` since plan Task 8a) | `lit_object` | closed; the skinned normal of `D4D5...` / `A1F7...` is taken from the microcode, not from a formula |
| `object-multi` | `648B...`, `7892...` | `ECD6...`, `79EA...` (`648B...` only), and since plan Task 8a the skinned `D4D5...`, `A1F7...` with both | `lit_object` with `sun_gate_multi` | closed, as `terrain-layer-multi` |
| `tree-trunk` | `00E0...` | `475E...` | `lit_object` | closed with bool `b129` clear |
| `tree-trunk-multi` | `58D3...`, `2FBE...` | `475E...` | `lit_object` with `sun_gate_multi` | closed with `b129` clear |
| `tree-leaf` | `7CD5...` | `A584...` | `tree_leaf` | closed since plan Task 8c ("Tree leaves": the literals, the vertex side's sun) and in the tables |
| `tree-leaf-multi` | `7CD5...` (the same pixel shader: the lights are the vertex shader's) | `D7DD...` (two lights), `2D40...` (four) | `tree_leaf` with light 0's cone and range in `spot` | closed since plan Task 8c; in the tables under `tree-leaf` |
| `plant` | `A7E4...`, `2605...` | `8123...`, `FC4F...`, `48D3...` | `plant` | closed |
| `plant-multi` | `E99F...` | `8123...`, `FC4F...` | `plant` with `sun_gate_multi` | closed |
| `plant-lit` | `165E...` | `B636...`, `6AD4...`, `36B5...`, `33C0...` | `lit_object` | closed |
| `plant-lit-multi` | `F525...` | `B636...`, `6AD4...`, `33C0...`, and since plan Task 8b `36B5...` | `lit_object` with `sun_gate_multi` | closed |
| none | `401A...` (blended characters), fur `014F...`, water `F6D9...` / `A17D...`, 7 small ones | | | not read |

All 32 pairs agree with the microcode in 400 of 400 random trials, the largest relative difference 2.3e-14 (`out/native-evidence/lighting/task2/light_check_all.txt`; tolerance 1e-4, relative to the larger of the two values per channel). Five more object pairs were run for plan Task 8a and agree likewise (`2FD2...` with `ECD6...`; `648B...` and `7892...` with `D4D5...` and `A1F7...`: `out/native-evidence/lighting/task8a/light_check_object.txt`, `light_check_object_multi.txt`; largest relative difference 8.9e-16); the counts and the coverage below are of the 32. The checked pairs are 93.9% of the decoded main-scene draws at the bridge and 92.2% by the trees ("Coverage" below). "Closed" means the arithmetic is known and checked; whether a family can be drawn also depends on its inputs being readable at run time, which "Open" at the end lists.

What differs from the research brief and the spec's table (spec design 2), found by this reading:

1. *Terrain layers have a second factor on the sun.* `sat(8 * (N . c109))` multiplies the screen mask before it reaches the sun term, in all six layer shaders. The far shaders, objects and plants have no such factor. `c109` reads (0, 0, 0, 0) in all 8,128 in-scene tess rows, as `c80`, `c87` and `c112` do (`tess_c19_bridge_trees.txt`; brief B: the device bank misses them).
2. *The two- and four-light variants are another form for light 0*, not the one-light form with more lights added: the direction is `normalize(c80.xyz - P * c80.w)` where the one-light shaders use `c80.xyz` as it is, the mask enters as a shadow amount with strength `c112.x`, and a cone and range factor is blended in by `c94.x`. With `c80` a unit direction, `c80.w = 0`, `c94.x = 0` and `c112.x = 1` it is the one-light form. They have family names of their own.
3. *Only one of the four vertex shaders of the object family reads a colour stream.* `ECD6...` raises the 8_8_8_8 element of stream vf3 to the literal 2.2. `79EA...`, `D4D5...`, `A1F7...` and the trunks' `475E...` build the same interpolator from four weights per vertex (stream vf1) and per-object constants `c28..c39` ("probe colour" below).
4. *The plants' vertex constants `c21`, `c22`, `c25`, `c26` are wind.* The ambient of `8123...` is `pow(instance colour, c18.w) * c18.xyz + c54.xyz`, and `c18`, `c54` equal the frame's pixel `c21` (with the exponent times 2.2) and `c20` in every captured row. The plants with a normal map (`165E...`, `F525...`) are the object form.
5. *Tree trunks are the object form.* `c72..c76`, tf11 and tf12 belong to a fog block under bool `b129`; bool `b132` applies a fog the vertex shader computes.
6. *Tree leaves are lit per vertex*, with a wrapped front term, a back term for green texels only, and their own vertex constants for the sun.

What the research brief (sections A3, A4, B, C3) states about the forms of `8D90...` with `ECD6...`, `A7E4...`, `2605...`, the layers' light block of `4524...` and the mask's use, read by hand there, was found as stated, instruction numbers included; the six items above are what it did not have.

## Method

`tools/xdk_sigmatch/ucode_eval.py` interprets the dump text (it was evidence of the materials sub-project's Task 9 and is tracked now, with `ps_check.py` and `vs_check.py`; frame-map section 13, "Method", has its rules). For the mesh shaders it gained vertex fetches, `a0`-relative constants, `cndge` / `cndgt`, `trunc`, `sin`, `cos`, `maxa` / `maxas`, the jump on a bool constant and loops with a given count. Its tests are `tests/test_ucode_eval.py`: one hand-written dump per instruction class with hand-computed results, and the two Task 9 checkers on the real terrain dumps. Their full runs are `out/native-evidence/lighting/task2/vs_check.txt` (identical to Task 9's `vs_check.txt`) and `ps_check.txt` (every comparison holds in every trial, as in Task 9's; the four counts of the informative "a light factor below 0" differ, because that script's random texture is seeded with Python's salted `hash`).

`light_check.py --all` checks a pair like this. One trial draws random vertex data and vertex constants, runs the vertex shader's microcode, hands its interpolators to the pixel shader's microcode (interpolator `oN` is pixel register `rN`, the register after the last one is the pixel position) with random pixel constants and textures, and compares `oC0.xyz` divided by the output scale with the family's function. The function's arguments are computed from the same random inputs by closed formulas (the helpers of `light_formulas.py`), so the check covers the vertex side as well: the vector of `t`, the vertex and probe colours, the plants' ambient and bent normal, the leaves' light. A trial is a single vertex; the interpolation between vertices is not modelled. The vertex data comes from a callback that is told the stream, the offset and the index the shader computed, and makes one random element for each; so the four bone fetches of the skinned shaders `D4D5...` and `A1F7...` get four different matrices (unless two random bone indices coincide), but nothing here states or checks what those shaders do with them: their normal is taken from the microcode's export ("Objects").

What a trial switches off, in every family, and why:

| Switched off | How | Why |
|---|---|---|
| normal maps (tf2, the terrain's side maps tf14) | the texel reads (0.5, 0.5): the tangent-space normal is (0, 0, 1) | the pass lights with the vertex normal (spec ruling 4) |
| lights 1 to 3 | colours `c88..c90` zero; light 0 keeps every constant, random | ruling 5 |
| fog | `c67.x = 1e9`, so the predicate `distance > c67.x` is false; trees: vertex `c111.x = 1e9` | ruling 8 |
| emissive (tf13), cube map (tf14), shadow map (tf9) | bools `b133`, `b134`, `b135` clear | rulings 1 and 4 |
| specular | the specular texture tf1 reads black | ruling 4 |
| the trees' fog volume | bool `b129` clear | ruling 8 |

A constant that only a switched-off block reads is not supplied to the interpreter, which fails on a read of a missing constant; so the trials also show that `c66`, `c68`, `c28` (fog), `c24` (emissive; except `2FD2...`, below), `c32..c35`, `c48`, `c114` (shadow map) and `c72..c76` (trees) are not read in this configuration. The screen mask tf8 is **not** switched off: its texel is random in every trial and is part of `sun_gate`. Bool `b128` (objects) and `b132` (trees) are random.

The tests (`tests/test_light_check.py`) show that the check can fail: on a hand-written shader of the terrain form, which always runs, and on the real dumps, a formula with `t` in the place of `t^2`, without `c11.x`, or with two floor channels exchanged disagrees; so does every pair when one helper is replaced by a plausible wrong form (the vector of `t` without the bend towards `c19`, the vertex colour without its power or with its channels reversed, the probe colour without its weights, the plants' normal without the bend, the ambient without `c54`, the leaves' light without the wrap or with the view reversed, the multi-light gate replaced by the one-light gate, the facing factor dropped). 22 such replacements, every pair that uses the helper.

**Literals.** `c252..c255` (and lower registers in the tree shaders) are literal constants, not in the dump text.

| Shaders | Values used | Source |
|---|---|---|
| terrain pixel and vertex shaders | frame-map section 13, "Constants" | `"ps_lit"` / `"vs_lit"` of the tess rows of both captures: one table per shader in every row (reread for this task) |
| mesh vertex shaders `ECD6...`, `79EA...`, `D4D5...`, `A1F7...`, `475E...`, `A584...`, the seven plant shaders | `ECD6...`: `c255 = (2.2, 0.5, 1, 0)`; `79EA...`, `D4D5...`, `A1F7...`: `c255 = (0, 1, 0.5, 0)`; `A584...`: `c249.w = 0.4545` (1 / 2.2), `c253.w = 1`; the others in `light_check.py` | `"vs_literals"` of `native_discovery_20261005_123635.jsonl` (the temporary diagnostic of frame-map section 12, "Literals"; one row per shader) |
| mesh pixel shaders | the 2 and -1 of the normal map decode, the 0 of a `dp2add`, the 1 of `1 - x`, by which register each idiom names | **no capture row holds them** (draw rows carry no `"ps_lit"`): read from the idioms, an assumption. They are the pixel bank's registers 252 to 255; a tess-style `"ps_lit"` field on draw rows, or a GPU-side log of the pixel constants at a draw, would settle them |
| `7CD5...` `c255.w`, `c255.z` | random in every trial | not known when the check was written, and the formula takes the two factors they make ("Tree leaves"), so the form is checked for any values. The values are known since plan Task 1: `c255 = (1, 0, 0.4, 0.6)` in every execution of the shader in both GPU logs (frame-map section 14, "The shaders' literals": 2,640 draws at the bridge, 4,800 by the trees), so `c255.w` = 0.6 and `c255.z` = 0.4 |
| vertex shaders `D7DD...`, `2D40...` | none | their tables (registers 248 to 255) are in no capture; not run |

Limits. The interpreter and the reading share the operand conventions, as in Task 9. The check is of the arithmetic, not of the values: which constants the game supplies is the capture's and Task 1's subject. Where a formula has a guard (`light_formulas.py`: a zero-length vector normalises to zero, a non-finite value counts as 0), the microcode has none; the check never feeds such inputs, and a test shows that for a zero-length normal the microcode's saturating dot products (NaN to 0, the interpreter's rule) give what the guards give. The SDK's own interpreter differs from `ucode_eval.py` exactly there: it makes a product with a zero factor +0 whatever the other factor is ("Direct3D 9 behavior" in `interpreter.cpp`), so its zero-length normal is 0 before the saturation, with the same end result; it also rounds to single precision, which `ucode_eval.py` does not. The operand conventions themselves (subtraction order, absolute value before negation, the third operand of `dp2add`, base 2 of `exp` and `log`, the predicate ops' results, the address register's rounding and clamp, saturation) have hand-computed tests that cite the SDK's lines.

## Shared pieces

Colours are linear. Every family's function returns the colour before fog and before the output scale `c20.w` (16 in every row).

**The ambient factor.** `L = c22 + (1 - c22) * t^2` per channel, the floor being `(c22.x, c22.y, c22.z)` for red, green and blue, channel for channel, in every shader that has the factor. Observed `c22` = (0.0588, 0.0902, 0, 0) in every row (frame-map section 13, "Constants"): blue has no floor.

**The vector of `t`.** `t = sat(normalize(axis) . N)`, N the unit normal the pixel shader lights with. The terrain layers, the objects, the trunks and the lit plants take `axis` from an interpolator the vertex shader builds:

```
axis = lerp(n, C, (1 + n . C) / 2)        (not normalised; ambient_axis)
```

with `n` the vertex shader's normal and `C` its constant `c19.xyz`. The far terrain uses `t = sat(N . c23.xyz)` with the pixel constant as it is. Vertex `c19.xyz` equals pixel `c23.xyz` of the same row in all 5,607 in-scene rows of the eight mesh vertex shaders that were compared (to 1e-6; `vertex_constants_bridge_trees.txt`) and bit for bit in all 8,128 in-scene tess rows (`tess_c19_bridge_trees.txt`), a unit vector that moves from frame to frame (`c23`: frame-map section 13). For unit vectors `t` is 1 where the normal is `C` and again where it is `-C` (there `k = 0` and the axis is the normal itself), and smallest in between: 0.66 at `n . C` = 0.27 (the function evaluated over the angle).

**The baked term.** `pow(x, c21.w) * c21.xyz`, computed as `exp2(c21.w * log2(|x|))`; `x` is the baked light map tf3 (terrain), the vertex shader's colour (objects, trunks) or the instance colour (plants, with the vertex copies of the constants).

**The sun.** `A * c87.xyz * n_dot_sun * sun_gate`.

- One-light shaders: `n_dot_sun = sat(N . c80.xyz)`, `c80` as it is (not normalised by the shader); `sun_gate = facing * mask`, mask the tf8 texel at the pixel position (`pixel.xy * c0.zw + c0.xy`), 1 = lit.
- Terrain layers only: `facing = sat(8 * (N . c109.xyz))`, the 8 a literal (`"ps_lit"`: `c254.z` in `9C21...` and `04F2...`, `c255.x` in `4755...` and `5783...`, `c254.x` in `4524...`, `c254.w` in `AF1E...`).
- Two- and four-light shaders, light 0, bool `b135` clear (`sun_gate_multi`):

```
V        = c80.xyz - P * c80.w                    P the world position
n_dot_sun = sat(N . normalize(V))
shadow   = (1 - facing * mask) * c112.x
range    = sat(1 - |V| * c96.x)
cone     = sat((normalize(V) . c98.xyz - c105.x) * c107.x)
sun_gate = (1 - shadow) * (1 + c94.x * (cone * range - 1))
```

Light 1 has the same form with `c81`, `c88`, `c99` and component y of `c94`, `c96`, `c105`, `c107`, `c112` (`4524...` 92 to 108); lights 2 and 3 of the four-light shaders were not read. Only light 0 is checked. With `b135` set the shadow amount is the larger of this and the tf9 term (`4524...` 70 to 90; brief A4; not checked).

**The sun at a multi-light draw** (plan Task 7, 2026-10-08; frame-map section 14, "Native light"). The sun is not light 0 of these shaders: at every `-multi` draw of two later captures it stands in slot 1 behind one lantern (the two-light shaders) or in slot 2 behind two (the four-light shaders), with `c112` one-hot at its slot, `c94` zero there and `w` = 0. `light_check.py --multi-sun` runs each two- and four-light pair of the terrain, object and trunk families with that light set and dark lanterns against the family's one-light function fed the sun (`n_dot_sun = sat(N . sun)`, `sun_gate_single`, for the layers with the facing factor of `c109`): all nine pairs agree in every trial at every slot behind light 0 (`out/native-evidence/lighting/task7/light_check_multi_sun.txt`), and so do the four pairs of the skinned object shaders entered for plan Task 8a (thirteen pairs: `out/native-evidence/lighting/task8a/light_check_multi_sun.txt`), and so do the six two-light pairs of the plants, whose sun stands in slot 1 at every `-multi` plant draw of three later captures (plan Task 8b: nineteen pairs, `out/native-evidence/lighting/task8b/light_check_multi_sun.txt`, `plant_rows_check.txt`; "Instanced plants"). So, for the sun alone, a `-multi` draw is its family's one-light function; the general `sun_gate_multi` above is what light 0 computes when it is a lantern.

**Vertex copies of the frame constants** (observed, both captures, compared row by row with the pixel bank of the same draw: `vertex_copies_bridge_trees.txt`): vertex `c18.xyz` is pixel `c21.xyz` bit for bit and `c18.w` is 2.2 x pixel `c21.w` (2.86; to 5e-9) in all 2,096 in-scene rows of the seven plant vertex shaders and `A584...`; vertex `c54` is pixel `c20` (xyz and the 16) bit for bit in all 1,464 rows of `8123...`, `FC4F...`, `48D3...` and `A584...`. So the plants' and leaves' ambient needs no constant beyond the frame's light block, if these equalities are taken as the rule (an inference from two scenes).

## Terrain layers: `terrain-layer`, `terrain-layer-multi`

```
colour = A * c11.x * (pow(tf3, c21.w) * c21.xyz * L + c20.xyz) + A * c87.xyz * n_dot_sun * sun_gate
```

`terrain_layer(A, baked, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c11x, sun_gate)`; `sun_gate` is required. The ambient part is frame-map section 13's; this task adds `t` and the sun.

| Input | Source |
|---|---|
| A | the material's colour: `tf0^2`, on steep ground mixed with the side texture (frame-map section 13, item 1). With flat side normal maps the steep block does not change the normal's direction (checked: half of the trials of `9C21...`, `4524...`, `04F2...` are steep) |
| baked | tf3 at the base coordinate (already a material input: `base_slot`) |
| tint, tint_pow, add, floor | frame block: `c21.xyz`, `c21.w`, `c20.xyz`, `c22.xyz` |
| c11x | per draw: `c11.x` (1 in all 7,344 in-scene layer rows, `fix1_recounts_bridge_trees.txt`; the function's default) |
| N | the tf17 normal of the vertex shader, `(2x - 1, 2y - 1, sqrt(|1 - x^2 - y^2|))`, normalised by the pixel shader. The vertex shader uses it as decoded, also where `x^2 + y^2 > 1` (checked: about one trial in six) |
| t | `ambient_t(ambient_axis(n, vs c19.xyz), n)`; vs `c19` is in the tess rows (`"consts"`) and equals pixel `c23.xyz` there |
| n_dot_sun, sun_gate | "The sun" above: `c80`, `c87`, `c109`, tf8 through `c0`; multi: also `c80.w`, `c94.x`, `c96.x`, `c98`, `c105.x`, `c107.x`, `c112.x`, and the world position (`o4` / `o2`: `c4..c6` times the patch point, identity in the captures) |

Instruction numbers. Vertex shader `C30A...`, the vector `o8`: `n . c19` (50), `+ 1` (58), `k = 0.5 * (n . c19 + 1)` (59, scalar half), `k * c19.xyz` (56, 57, 59, 60), `1 - k` (60, scalar half), `n.xy * (1 - k) + k * c19.xy` (61), `n.z * (1 - k)` (63), `+ k * c19.z` (66), export (83). `FB68...` the same in `o6` (49, 56 to 60, 62, 65; export 79). Literals `c253.x` = 0.5 and `c254.y` = 1.

Pixel shaders (the other terms are in frame-map section 13's table):

| Term | `9C21...` | `4755...` | `4524...` |
|---|---|---|---|
| `normalize(o8)` (`o6`) | 47, 50, 54 | 12, 15, 16 | 51, 52, 57 |
| N normalised | 56 to 58 | 25 to 27 | 58 to 60 |
| t; `t^2` | 59; 64 | 28; 33 | 61; 64 |
| `N . c109`; `sat(8 * .)`; `* tf8` | 61; 62; 63 | 30; 31; 32 | 62; 63; 64 |
| `sat(N . c80)`; times the gate | 60; 64 | 29; 33 | (multi) |
| `* c87`; `* A`; added | 66; 67; 69 | 35; 36; 39 | 109; 110 |
| multi: shadow amounts `(1 - facing * mask) * c112.xy` | | | 66, 67 |
| multi: light vectors, lengths, directions | | | 91 to 96 |
| multi: `sat(N . dir)`; range; cone | | | 97, 98; 99; 100 to 103 |
| multi: `(1 - shadow) * sat(N . dir)`; `1 + c94 * (cone * range - 1)`; product | | | 102 to 104; 105, 106; 107 |

`04F2...`, `AF1E...` and `5783...` were checked by machine and not read line by line beyond what frame-map section 13 lists.

## Far terrain: `terrain-far`

```
colour = F * (pow(tf3, c21.w) * c21.xyz * L + c20.xyz) + F * c11.x * c87.xyz * sat(N . c80.xyz)
L from t = sat(N . c23.xyz)
```

`terrain_far(F, baked, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c11x)`. F = `tf13^2`, N the unit tf17 normal (interpolator `o2`), `c23` and `c80` as they are, no mask, no `c109`, no cone. `c11.x` scales the sun here (0.488 at the bridge's adaptive rows; brief A3 reads it as the mask's far value). `2F8A...` multiplies the whole colour by its fade `sat(d2 * c31.x + c31.y)`, which stays outside the function. Instructions of `7DDA...`: N normalised into the two dot products (9, 11 to 13), `sat(N . c80)` (16), `sat(N . c23)` (18), `t^2` (19), L (7, 20), `* c11.x * c87` (7, 8, 17 to 19, 21), ambient (22, 23), sum (24).

## Objects: `object`, `object-multi`

```
colour = (A * c27.w * (pow(vcolour, c21.w) * c21.xyz * L + c20.xyz) + A * c87.xyz * n_dot_sun * sun_gate) * c49.xyz
```

`lit_object(A, vcolour, tint, tint_pow, add, floor, t, n_dot_sun, sun_col, c27w, sun_gate, c49)`. `c27.w` is on the ambient part only. **`c26` is not in the function**: after fog the shaders of this family, the trunks' and the leaves' multiply the colour by `c20.w * c26.xyz` and the alpha by `c26.w` (`8D90...` 84, 85; `00E0...` 88, 89; `7CD5...` 68, 69), so whoever draws them applies `c26.xyz` with the output scale; the check divides it out. `c26` is a per-draw pixel constant in the readable bank (`"pconst"`), (1, 1, 1, 1) in all 4,084 in-scene rows of the eight pixel shaders (`8D90...` 2,492, `2FD2...` 32, `648B...` 200, `7892...` 88, `00E0...` 560, `58D3...` 136, `2FBE...` 80, `7CD5...` 496; `fix1_recounts_bridge_trees.txt`). The plants' and the terrain's shaders have no `c26`. The gate has no facing factor: one light `sun_gate = mask`; multi `shadow = (1 - mask) * c112.x`.

| Input | Source |
|---|---|
| A | `tf0.rgb^2`, divided by `tf0.a^2` when bool `b128` is set (both states checked; the state in the game is the device's bool word, not proven) |
| vcolour | interpolator `o6`: `vertex_colour` or `probe_colour`, below |
| N | interpolator `o1`, normalised by the pixel shader. `ECD6...`, `79EA...`: the 10_11_11 element at dword 2 through the rows `c4..c6` (xyz), times `c7.w`. In the captured rows the three rows times `c7.w` are orthonormal to 3e-7 (`ECD6...` 1,944 rows, `79EA...` 484), so `o1` is a unit vector there |
| t | `ambient_t(ambient_axis(o1, vs c19.xyz), N)`: the axis is built from `o1` as exported, in all four vertex shaders |
| tint, tint_pow, add, floor, n_dot_sun, sun_col | frame block: `c21`, `c20.xyz`, `c22`, `c80`, `c87` (multi: "The sun") |
| c27w, c49 | per draw: `c27.w` (1 in all 4,084 rows counted for `c26`), `c49.xyz` (above `c31`, in no row: no default in the function) |

**`vertex_colour`** (`ECD6...`): `o6 = pow(|colour|, 2.2)` per component, the 2.2 the literal `c255.x`. The colour is the 8_8_8_8 element of stream vf3 (the capture's fetch slot 92, stride one dword, swizzle `zyxw`: red is the fetched z), read at **the vertex index plus vertex constant `c20.x`** (instr 9, 14; the check asserts the index). `c20.x` is 0 in 840 of 976 `ECD6...` rows at the bridge and 824 of 968 by the trees, and otherwise a whole number up to several thousand (bridge: 6, 54, 225, 505, 895, 1464, 8263, 8401, 8665; trees: 224, 670, 4054, 5579, 7445, 8753): several meshes share one colour stream. A reader must bound the offset by the stream: the element read is `c20.x` plus the vertex index, and it has to lie inside the bound colour stream (its size in bytes over 4) or the draw keeps its material and counts `light-unknown`. In all 1,944 rows `c20.x` is a whole number and `c20.x` plus the draw's largest index is inside the stream the row lists for fetch slot 92 (`fix1_recounts_bridge_trees.txt`).

**`probe_colour`** (`79EA...`, `D4D5...`, `A1F7...`, and `475E...`, `A584...` of the trees): `o6 = c[28 + a0].xyz + (c[29 + a0] . w, c[30 + a0] . w, c[31 + a0] . w)`, four-component dot products, `w` the 32_32_32_32_FLOAT element of stream vf1 (fetch slot 94, stride 4 dwords) and `a0` the truncated fourth component of the position element. Every sampled vertex of every shader that has the index holds 0, 4 or 8 there (the rows' `"positions"` samples, both captures, `fix1_recounts_bridge_trees.txt`: `79EA...` 30,432 vertices, `475E...` 49,248, `D4D5...` 30,200, `A584...` 25,600, `A1F7...` 14,336, and the leaves' `D7DD...` 3,584 and `2D40...` 2,560; in `79EA...` it is the register's x after the fetch swizzle `zxwy`, in the others its w), so a draw carries up to three sets, `c28..c31`, `c32..c35`, `c36..c39`; they differ per object (4 to 53 distinct values of `c28` per shader and capture). No power in the vertex shader. The only power the probe colour takes is the pixel shader's `c21.w`, on its absolute value ("The baked term": the `log`'s operand is `r_abs` in each of the seven object and trunk pixel shaders, read for plan Task 8a); the weights are signed, and the colour is exactly zero or slightly negative (to -0.062) in part of the captured vertices, which the game therefore lights as 0 and as the mirror image.

Instruction numbers. `ECD6...`: world normal (20 to 22), `* c7.w` (27: `o1`; 29), `c19 * c7.w` (16), `d = n . c19` (30), `1 + d` (36), `k = 0.5 * (1 + d)` (39), `k * c19` (15, 37, 39, 40), `1 - k` (40), `o5` (42); colour fetch (9, 14), logs (21, 22, 29, 30), `* c255.x` (31), exp into `o6` (32 to 35). `79EA...`: `a0` (28, 29), dot products (30 to 32), `o6` (39), `o5` (15 to 21, 28, 29, 33, 34, 36, 37, 40). `8D90...`: A (14, 18; `b128` 30 to 32), `* c27.w` (36), N (13, 20 to 29), mask (33, 34), logs of `o6` (37 to 39), `* c21.w` (43 to 45), exp (46 to 48), `* c21.xyz` (49), `normalize(o5)` (37, 40, 42), t (43), `t^2` (48), L (35, 51), `+ c20` (52), `sat(N . c80)` (39), `* mask` (48), `* c87` (49, 50, 53), `* A` and the sum (54, 55), `* c49` (73), `* c20.w * c26` (84, 85). Its specular term, off in the check, is `S * c87 * sat(c23.w * sat(N . c80)) * sat(V . R)^4 * mask` with `S = tf1.rgb^2` (15, 18, 19, 22, 38, 40 to 47, 50, 53); `b133` 56 to 58; `b134` 59 to 71; fog 72 to 83. `2FD2...` differs in the specular exponent only, `lerp(c24.x, c24.y, tf1.a)` (17, 18), which it computes outside any bool block; its ambient and diffuse are the same function. `648B...`: shadow amounts `(1 - mask) * c112.yx` (40, 44), lights from 72.

The skinned shaders `D4D5...` and `A1F7...` export as `o1` the bone-transformed normal made orthogonal to the bone-transformed tangent and normalised (`A1F7...` 19 to 21, 26 to 31, 34 to 41). `light_check.py` takes their `o1` from the microcode. Its closed formula was written and checked later (plan Task 5a; frame-map section 14, "Normals", "Skinned"): with `B` the weighted sum of the vertex's bone matrices (the one its position goes through), `B3` its 3 x 3, `n` the normal, `t` the 2_10_10_10 element at dword 6 and `R` the rows `c4..c6`, `Nw = R B3 n`, `Tw = normalize(R B3 t)`, `o1 = normalize(Nw - Tw * (Tw . Nw))`; `out/native-evidence/lighting/task5a/skinned_normal_check.py` runs it against the two dumps (400 trials each, largest difference 3.4e-14). It is not a function of `light_formulas.py`. **The native renderer's skinned normal omits the tangent step**: it is `normalize(R B3 n)`, the game's `Nw` normalised, the blended bone matrix as it is (ruling of 2026-10-08; no tangent is among the light inputs, and normal maps are out of scope). On the captured meshes that is within cosine 0.999 of `o1` in every vertex at the bridge and in all but 377 of 14,620 (`D4D5...`) and 16 of 6,256 (`A1F7...`) by the trees, the worst at 0.975 (frame-map section 14, "Normals", "Follow-ups").

## Tree trunks: `tree-trunk`, `tree-trunk-multi`

The object formula, checked with the same function. `00E0...` with `475E...`: `o1` the world normal times `c7.w` (62 to 64, 73 to 75; `c7.w` runs from 0.67 to 1.54 in the rows and the rows times it are orthonormal to 2e-7), `o5` the axis (61 to 64, 69 to 72, 75, 76, 79 to 84), `o8` the probe colour (16, 31, 65 to 68, 77). Pixel: A = `tf0.rgb^2 / tf0.a^2` always (14, 21, 38, 39), pow of `o8` (18, 19, 22, 24, 25, 27, 30, 31), t (19, 21, 22, 34), L (17, 35, 36), `+ c20` (37), sun `c87 * sat(N . c80) * mask` (33, 35, 38 to 41), ambient `* c27.w` and sum (42), `* c49` (43), `* c20.w * c26` (88, 89). No specular in `00E0...`.

The two bool blocks, which are what `c72..c76`, tf11 and tf12 belong to:

- **`b132`** (`00E0...` 16, 44; `7CD5...` 10, 25): `colour = colour * o6 * c49 + o7` (leaves: `o4`, `o5`). The two interpolators are a fog the **vertex** shader computes from its own tables tf16 (1D) and tf17 with vertex `c110`, `c111`, `c8.x` (`475E...` 85 to 94; `A584...` 136 to 144): a factor and an added colour, 1 and 0 where the distance is below vertex `c111.x` (`475E...` 95, 96). That is why these pixel shaders read no `c66..c68`. With the vertex fog off the bool makes no difference; the check runs both states.
- **`b129`** (reached only with `b132` set; `00E0...` 45 to 87, `7CD5...` 26 to 67), read at block level and not checked: the ray from the eye `c19` to the pixel is cut by a height `tf12(xy * c74.xy + c74.zw) * c76.x + c76.y` (45 to 58), tf11 is sampled at the two ends through `c73` (63 to 69), the two samples and the height differences times `c76.w` are combined (70 to 81), and `T = exp2(c76.z * amount)` blends the colour towards `c72.xyz * c28.y`: `colour * T + (1 - T) * c72 * c28.y` (82 to 87). A ground fog volume with its own height and density maps. It carries one more unknown literal (`c254.w`; in `7CD5...` `c254.x`). In both captures tf11 and tf12 are the 1 x 1 dummy (brief C2).

Which state `b129` and `b132` have in the game is not known (the device's bool word reads `0x100` in every row and was never compared with the GPU's).

## Tree leaves: `tree-leaf`

Everything but the texture and the mask is computed in the vertex shader.

```
colour = (A * (amb * c27.w + m1 * sun * front) + green * A * m2 * sun * back) * c49.xyz

amb   = pow(probe colour, c18.w * 0.4545) * c18.xyz + c54.xyz          (o1)
Nf    = the world normal, negated where it faces away from the eye
front = sat((Ld . Nf + c50.w) / (1 + c50.w)) * spot                      (o2 = sun * front)
back  = sat(-(Ld . Nf)) * (1 - (1 - sat(V . Nf))^2) * spot               (o3 = sun * back)
green = 1 where A.g >= A.r, else 0
m1    = mask * c255.w + c255.z,   m2 = m1 * c255.w + c255.z
```

`tree_leaf(A, vs_ambient, front, back, sun_col, c27w, m1, m2, c49)` with `instance_ambient`, `probe_colour`, `leaf_light`, `leaf_mask`; `m1`, `m2` and `c49` are required, and `c26.xyz` is applied outside as for the objects. All constants but `c27`, `c49`, `c255` are **vertex** constants: `sun` = `c83.xyz`, `Ld = normalize(c76.xyz - P * c76.w)`, V the unit vector to the eye `c9`, `spot = 1 + c90.x * (cone * range - 1)` with `range = sat(1 - |light vector| * c92.x)` and `cone = sat((Ld . c94.xyz - c101.x) * c103.x)`, the wrap `c50.w` (0.4 in every row). The ambient has no factor L and no vector of `t`; its exponent `c18.w * 0.4545` is 1.3 with the captured 2.86, the pixel `c21.w`.

Instructions. `A584...`: normal (83, 87, 95, 96), world position (101 to 103), view (104 to 107), flip (108 to 110), `V . Nf` (113), probe colour (113, 115 to 117, 122), light vector and direction (114, 118 to 121), `Ld . Nf` and the range (123, 127), back (124, 120, 127, 131), cone (125, 126, 129), wrap (111, 125, 128, 130), spot (133, 134), products (135), `o2` (136, 154), `o3` (137, 150), exponent (112), logs, exp, `* c18.xyz + c54` (124, 126 to 132), `o1` (153). `7CD5...`: A (13, 14, 18, 19), `amb * c27.w` (9), mask (8, 12), m1 (15), front (16), m2 (17), back for green texels (18, 20 to 22), sum (23), `* c49` (24), `* c20.w * c26` (68, 69).

Open in this family:

1. **The literals `c255.w` and `c255.z` of `7CD5...`: closed** (plan Task 1). They are 0.6 and 0.4: the GPU's `c255` is (1, 0, 0.4, 0.6) at every colour-pass draw of the shader in both logs (frame-map section 14, "The shaders' literals"). So `m1 = 0.6 * mask + 0.4` and `m2 = 0.6 * m1 + 0.4`: with the mask at 1 both are 1, and the mask is the softened shadow `lerp(0.4, 1, mask)` applied once and twice, which was a guess when this was written (`c255.w + c255.z = 1`). The check still draws the two at random and hands the formula `m1` and `m2`: the form is checked for any values, and a consumer passes `leaf_mask(mask, 0.6, 0.4)`. Since plan Task 8c the check runs with the shader's own literals: `c255` = (1, 0, 0.4, 0.6) in the `"ps_lit"` of every row of `7CD5...` in the two captures of plan Task 1 whose rows carry literal tables (288 and 408 rows: `out/native-evidence/lighting/task8c/leaf_literals.txt`).
2. **The sun's direction and colour: closed** (plan Task 8c, 2026-10-08). In the device's vertex bank `c76`, `c83`, `c90`, `c92`, `c94`, `c101`, `c103` read (0, 0, 0, 0) (the bank misses the light set, as it does on the pixel side); the registers as the upload model follows them (`"vfull"` of the rows of plan Task 1's two captures) hold the set, and at every main-scene leaf row of both (176 at the bridge, 328 by the trees: `out/native-evidence/lighting/task8c/leaf_rows_check.txt`):
   - the frame's sun (direction pixel `c109.xyz`, colour pixel `c(87 + i).xyz` where `c112` is one-hot at `i`, one value per frame over every main-scene row) stands in the vertex set **bit for bit**: `c(76 + s).xyz` its direction with `w` = 0, `c(83 + s).xyz` its colour, with `s` = 0 in all 408 rows of `A584...`, 1 in all 56 of `D7DD...` and 2 in all 40 of `2D40...` (the slots the pixel side's two- and four-light shaders have it in);
   - `c90` is 0 at `s` (no cone, no range: `spot` = 1), `c94 + s` is (0, 0, 0, 0), `c101` (-1, -1, -1, -1), `c103` (1, 1, 1, 1); every slot before `s` is a lantern (`w` = 1, `c90` = 1 there; colours such as (1.76, 0.82, 0.10); the fourth light of `2D40...` is unused, colour zero);
   - vertex `c9.xyz` (the eye) is pixel `c19.xyz` of the frame, `c18` is (pixel `c21.xyz`, `float(2.2f * c21.w)`) and `c54` is pixel `c20`, all bit for bit in 504 of 504 rows; `c50` is (1, 100, 1, 0.4) in all;
   - pixel `c27`, `c49` and `c26` are (1, 1, 1, 1) in all; `c7.w` is 0.70 to 1.53 (53 values).
   The device's vertex bank equals the followed value at every register of those rows but the light set (76 to 85, 90 to 93, 101 to 107) and the literals (248 to 255): `c4..c7`, `c9`, `c18`, `c28..c39`, `c50`, `c54` are readable from the bank. So a consumer takes the sun, the eye, `c18` and `c54` from the frame's block and the rest per draw. These are observations of two daylight scenes of one area, not a rule read from the game.
3. **`D7DD...` and `2D40...`: closed** (plan Task 8c). Their literal tables are in the capture by the trees of plan Task 1 (`"vs_lit"` of all 56 and 40 rows; `leaf_literals.txt`): the same values as `A584...`'s in another order (the 1 / 2.2 of the ambient exponent is `c248.w` and `c255.x`; `D7DD...` has a literal -1 at `c255.x`, `2D40...` at `c248.y`, for `cone * range - 1`). Family `tree-leaf-multi`: light `j` reads `c(76 + j)`, `c(83 + j)`, `c(94 + j)` and component `j` of `c90`, `c92`, `c101`, `c103`; the two interpolators are the sums over the lights. `light_check.py --family tree-leaf-multi --trials 400`: both pairs 400 of 400 with light 0 random and the others dark (largest relative difference 5.8e-16; `out/native-evidence/lighting/task8c/light_check_leaves.txt`). `light_check.py --multi-sun`: with the game's set (the sun in slot 1, or 1 to 3, behind dark lanterns, `c90` zero at its slot) both are the ONE-light `tree_leaf` of that sun, direction as it is and `spot` = 1, 400 of 400 at every slot (`light_check_multi_sun.txt`); with a lantern lit, a cone on the sun or the sun as a position the check fails (`tests/test_light_check.py`, `MultiSun`).

What the native pass needs per vertex beyond the cache's normals (plan Task 8c; read in the three dumps, instruction numbers of `A584...` above, the same statements at 98 to 163 of `D7DD...` and 100 to 184 of `2D40...`): the probe weights of stream vf1 and the probe set's index in the position's fourth component (fetch source z), which the cache holds for probe records since plan Task 5b (the leaf shaders have the trunks' elements); nothing else. There is **no translucency term beyond the back light** (`back`: the light from behind the card, through leaf texels only), **no wrap but `c50.w`** on the front light, and **one power**: the ambient's `pow(|probe colour|, c18.w * 0.4545)`, on the absolute value (`log` of `r_abs[6]`, instr 124, 126, 127), carried with `* c18.xyz + c54.xyz` in interpolator `o1`; `o2` and `o3` carry the front and back light times the light's colour. The normal is NOT normalised (83, 87, 95, 96: the rows, times `c7.w`), and it is negated where it faces away from the eye (the predicate of 108 to 110): the leaf cards are drawn without culling (register 0x2205 = `0x00218000` in every leaf row of four captures, where every trunk and plant row has `0x00218006`, cull back: `task8c/leaf_cull.txt`), and the shader lights the side the viewer sees. The position the light uses is the swayed one (wind on the card about its pivot), which the native pass does not draw.

## Instanced plants: `plant`, `plant-multi`, `plant-lit`, `plant-lit-multi`

Two forms. Without a normal map (`A7E4...`, `2605...`, two-light `E99F...`):

```
colour = A * amb + A * c87.xyz * sat(Np . c80.xyz) * mask
amb    = pow(instance colour, c18.w) * c18.xyz + c54.xyz           (vertex shader)
```

`plant(A, vs_ambient, n_dot_sun, sun_col, sun_gate)` with `instance_ambient`. With a normal map (`165E...`, two-light `F525...`) the object form, the vertex shader exporting `pow(instance colour, c18.w) * c18.xyz` and the pixel shader applying L and `c20`:

```
colour = A * (pow(instance colour, c18.w) * c18.xyz * L + c20.xyz) + A * c87.xyz * sat(Np . c80.xyz) * mask
```

`lit_object` with `vcolour` = the instance colour, `tint` = vs `c18.xyz`, `tint_pow` = vs `c18.w`, `c27w = 1`, and `t = sat(normalize(ambient_axis(N, vs c19.xyz)) . Np)`. A = `tf0.rgb^2 / tf0.a^2` always.

| Input | Source |
|---|---|
| instance colour | the 8_8_8_8 element at dword 6 of the instance stream (vf1, fetch slot 94, stride 7 dwords), channels (z, y, x) of the fetch |
| N | `normalize(M n)`: n the vertex stream's 16_16_16_16_FLOAT element at dword 2, M the instance's 3 x 3, the rows that transform the position (elements at dwords 0, 2, 4 of the instance stream; `vs-transforms.json` has the row order per shader) |
| Np (`plant_normal`) | N bent towards the instance's up axis: `bent = normalize(lerp(N, U, h))` in the vertex shader, `Np = normalize(lerp(N, c45.x * bent, sat(c45.x * h)))` in the pixel shader; U = M's third column, normalised; `h = sat(z * c45.y + c45.z)`, z the vertex position's third component in the instance's space |
| vs `c18`, `c54`, `c19` | equal to pixel (`c21.xyz`, 2.2 x `c21.w`), `c20`, `c23.xyz` in the captured rows ("Shared pieces") |
| vs `c45` | per draw: (0, 0, 0, 0) (no bend) in 232 of 623 `8123...` rows at the bridge, else (1, -200, 100, 0), (1, -134, 100, 0) and two more; `B636...` zero, (1, 141.4, 2.775, 0), (1, -25.63, 79.56, 0) and one more; `6AD4...` zero in every row |
| sun, mask | frame block and tf8; the multi variants "The sun" with `shadow = (1 - mask) * c112.x` |

The vertex constants `c21`, `c22`, `c25`, `c26`, `c10` of these shaders are the wind (a cosine of a phase from the instance's position, times directions `c21`, `c22` and strengths `c26`: `8123...` 39 to 54, 57, 58): they move the position, not the light. Four of the seven vertex shaders (`48D3...`, `FC4F...`, `36B5...`, `33C0...`) also push the plant away from points `c56` onwards in a loop over integer constant `i0`; the check gives the loop 0 to 4 iterations and the light outputs do not change with it.

**Which interpolator carries what, per vertex shader** (plan Task 8b; found by trying every interpolator against each form, with instance rows that are no rotation, 400 trials a shader: `out/native-evidence/lighting/task8b/plant_colour_check.txt`, `plant_bend_check.txt`). The copy's colour takes one power, `c18.w` as it is, in the vertex shader, and no other anywhere.

| Vertex shader | Colour | Form | Vector of the ambient factor | Bend `(c45.x * bent, c45.x * h)` |
|---|---|---|---|---|
| `8123...` | `o4` | ambient: `pow * c18.xyz + c54.xyz` | none | `o3` |
| `FC4F...` | `o4` | ambient | none | `o3` |
| `48D3...` | `o3` | ambient | none | `o2` |
| `B636...`, `6AD4...`, `36B5...`, `33C0...` | `o7` | base: `pow * c18.xyz` | `o5` | `o6` |

The form goes with the pixel shader's family in every pair of the table: the three ambient-form vertex shaders are paired with `A7E4...`, `2605...`, `E99F...` only, the four base-form ones with `165E...`, `F525...` only. All seven build the bend in one form: the wrong candidates (the third row of the instance's 3 x 3 in the place of its third column, an up axis that is not normalised, another position component for the height, a height blend that is not saturated, a lerp that is not normalised) each differ from the microcode by 0.49 or more over the trials, and with `c45.x` = 0 the bend's interpolator is (0, 0, 0, 0). `F525...` with `36B5...` was run for plan Task 8b (400 of 400, `light_check_plants.txt`); a capture of the third spot draws it once a frame.

Instructions. `8123...`: instance colour (17), logs (26 to 28), `* c18.w` (29), exp (30 to 32), `* c18.xyz + c54` (33), `o4` (76); normal through the instance rows (21 to 24), normalised (30, 35, 36), `o1` (59, 71); h (38), up axis (40 to 42, 45, 46, 48, 49, 51), lerp (53, 55), normalise (56, 59, 60), `* c45.x` (61), `o3` (77). `A7E4...`: A (8, 14, 18, 19), N (12, 13, 15), blend with `o3` (11, 16, 17), normalise (18 to 20), `sat(Np . c80)` (21), `* mask` (7, 22), `* c87` (23, 24), `* A` (25), `+ A * o4` (26), `* c20.w` (39). `2605...` the same with the blend in `o2` and the ambient in `o3` (7 to 21). `6AD4...`: colour (18, 26, 29 to 31, 33, 34, 37), `* c18.xyz` (40, 42, 43), `o7` (82); axis (55 to 64), `o5` (81); bend (53, 54, 57 to 59, 62, 63, 65 to 67), `o6` (83). `165E...`: A (15, 41, 43, 44, 46 to 48), N with the normal map (14, 18, 21, 24 to 32), blend with `o6` (33 to 37), t (20, 22, 23, 38), L (16, 50), `* o7 + c20` (51), `sat(Np . c80)` (40), sum (52 to 56), `* c20.w` (69).

## Coverage

Share of the decoded main-scene draws ("draw" and "tess" rows with `in_scene`), 8 frames per capture, by `light_check.py --coverage` (`coverage_bridge_trees.txt`). This counts what the capture decodes, not what the materials pass ends up drawing: of the rows without a family the pass leaves out the fur shells `014F...` and the water `A17D...` (frame-map section 13, "Validation").

| Family | Bridge: per frame, share | Trees: per frame, share |
|---|---|---|
| `terrain-layer` | 348.0, 36.4% | 367.0, 41.0% |
| `terrain-layer-multi` | 144.0, 15.1% | 59.0, 6.6% |
| `terrain-far` | 75.0, 7.8% | 23.0, 2.6% |
| `object` | 123.0, 12.9% | 192.5, 21.5% |
| `object-multi` | 21.0, 2.2% | 15.0, 1.7% |
| `tree-trunk` | 30.0, 3.1% | 40.0, 4.5% |
| `tree-trunk-multi` | 0 | 27.0, 3.0% |
| `tree-leaf` (with `A584...`) | 22.0, 2.3% | 28.0, 3.1% |
| `plant` | 71.0, 7.4% | 34.0, 3.8% |
| `plant-multi` | 15.0, 1.6% | 12.0, 1.3% |
| `plant-lit` | 39.0, 4.1% | 23.0, 2.6% |
| `plant-lit-multi` | 10.0, 1.0% | 5.0, 0.6% |
| **checked pairs together** | **898.0 of 956.5, 93.9%** | **825.5 of 895.8, 92.2%** |
| `tree-leaf-multi` (`D7DD...` / `2D40...`; not run when this table was made, checked since plan Task 8c) | 0 | 12.0, 1.3% |
| `401A...` (blended characters), not read | 16.5, 1.7% | 23.0, 2.6% |
| fur `014F...`, water `F6D9...` + `A17D...` | 39.0, 4.1% | 21.0, 2.3% |
| 7 others (`DBFD...`, `C300...`, `FF19...`, `1421...`, `09CB...`, `BB43...`, `A753...`) and one draw without a pixel shader | 3.0, 0.3% | 14.2, 1.6% |

Of the mesh draws alone (389.5 and 446.8 per frame) the checked pairs are 331.0 (85.0%) and 376.5 (84.3%); of the terrain draws all (567 and 449). The one-light families (`terrain-layer`, `terrain-far`, `object`, `tree-trunk`, `tree-leaf`, `plant`, `plant-lit`) are 708.0 per frame (74.0%) at the bridge and 707.5 (79.0%) by the trees. A family whose sun inputs cannot be read is drawn with its ambient only (as written in plan Task 2, before the source of the sun's constants was known; since plan Task 1 the frame's sun is read, frame-map section 14, "Pixel constants", and no family is drawn with its ambient alone: a frame without a light block is drawn unlit as a whole, `light-block`). For the leaves that is now exact in form (their two pixel literals are known: "Tree leaves", item 1); the family's open items were its sun, which is a set of vertex constants, and the two vertex shaders that were not run; both are closed since plan Task 8c ("Tree leaves", items 2 and 3), and the leaves are lit with their sun.

## Open

1. **Constants the captures do not hold.** Pixel `c80`, `c87`, `c109`, `c112` read zero in the tess rows and `c0` in every row; `c94`, `c96`, `c98`, `c105`, `c107`, `c49` are in no row; vertex `c76`, `c83` and the leaf light's others read zero in the draw rows' vertex bank. Task 1 looks for the pixel side; the vertex side is new here. (Closed for the leaves by plan Task 8c: the followed vertex registers hold the frame's sun bit for bit; "Tree leaves", item 2.) **Closed for the pixel side** by plan Task 1: the registers are uploaded by `LoadAluConstants` from buffers past the device's bank, the capture follows those loads, and the frame's light block was held equal to the emulated GPU's registers (frame-map section 14, "Pixel constants" and "The frame's light block"); `c49`, `c26`, `c27.w` and `c11.x` are read per draw since plan Task 4 ("Light inputs"). Still unread, by ruling: `c94` to `c107` and the other lights' slots (no lanterns: spec ruling 5).
2. **The value of `c109`**, without which the facing factor of the terrain layers (36% to 41% of the draws) cannot be computed. If it were the sun's direction, `facing = sat(8 * (N . c80))`; that is a guess. **Closed** by plan Task 1: `c109.xyz` is the sun's direction, bit-equal to `c80.xyz` of a one-light draw in every colour draw of the two GPU logs, and it is the direction the frame's block carries (frame-map section 14, "Pixel constants"; "The sun" above); the facing factor is `sat(8 * (N . c109))` with the literal 8 from each shader's table entry (`facing_scale`).
3. **Bool states**: `b128` (objects: divide by alpha), `b129`, `b132` (trees), `b133..b135`. Only the device's word is known (`0x100`). **Closed for the capture, not used by the pass**: the device's bool word was held against the GPU and refuted, and the word the capture follows through the game's two bool writers (`"pbool_load"` in the discovery rows) equals the GPU's in every main-scene colour execution checked, 5,387 of 5,387 and 3,314 of 3,314 (frame-map section 14, "Pixel constants", "The bool word"). The pass itself reads no bool: by spec ruling 16 the blocks under `b133`, `b134` and `b135` are not drawn. What stays open is in frame-map section 14, "Validation", "Limitations".
4. **Mesh pixel shader literals** are read from idioms. **`7CD5...`'s `c255.w`, `c255.z`**: closed since (plan Task 1: 0.6 and 0.4; "Tree leaves", item 1).
5. **Not run**: `7CD5...` with `D7DD...` / `2D40...` (literal tables; run since plan Task 8c: "Tree leaves", item 3). **Not read**: `401A...` (it has no tf8 and takes its shadow from the two cascades tf6 / tf7 directly; `getWeights2D` is not interpreted), the 2- and 4-light shaders' lights 1 to 3, every `b135` block.
6. **Skinned normals** (`D4D5...`, `A1F7...`): closed since (plan Task 5a; "Objects" above and frame-map section 14, "Normals"): the bone transform and the step that makes the normal orthogonal to the tangent, checked against the microcode by a script of that task's evidence. Open: the formula is not in `light_formulas.py`, and the tangent element it reads is not among the light inputs the capture records, so the native skinned normal is built without the tangent step ("Objects").
7. **Inputs the spec's capture list (design 3) does not have**: the probe weights stream vf1 and the constants `c28..c39` with the position's fourth component (objects with `79EA...`, `D4D5...`, `A1F7...`, trunks, leaves: 76.0 draws per frame of the checked ones at the bridge and 182.5 by the trees, where `8D90...` with `79EA...` alone is 41.5); the colour stream's index offset `c20.x`; for plants the instance colour and the bend constants `c45`. **Closed**: the light inputs carry all of them since plan Task 4 (`c28..c39`, `c20.x`, `c45`; frame-map section 14, "Light inputs"), and the geometry cache builds the probe weights and set indices (plan Task 5b), the instance colours (5b) and the bend inputs (8b) ("Normals", "Probe colours", "Plants").

## Evidence

In `out/native-evidence/lighting/task2/` (git-ignored): `light_check_all.txt` (`light_check.py --all --trials 400`), `light_check_list.txt` (the 32 pairs), `coverage_bridge_trees.txt`, `vertex_constants_bridge_trees.txt`, `vertex_copies_bridge_trees.txt` and `fetch_layouts_bridge_trees.txt` with their scripts `vertex_constants.py`, `vertex_copies.py`, `fetch_layouts.py` (one pass each over the two captures: the vertex bank's registers the formulas read, their comparison with the pixel bank, and the fetch layouts and the position's fourth component of the shaders named here), `tess_c19_bridge_trees.txt` (`tess_c19.py`: the tess rows' vertex `c19` against pixel `c23`, and their zero sun constants), `mutations.txt` (the light tests run against five deliberately wrong functions), `fix1_recounts_bridge_trees.txt` (`fix1_recounts.py`: the colour stream's offset against the stream's size, the a0 index per shader, `c26`, `c27.w`, `c11.x`), `fix1_interpreter_mutations.txt` (the interpreter's hand tests against 18 broken conventions), `fix1_light_check_all.txt` (the 32 pairs after fix round 1), `vs_check.txt`, `ps_check.txt` (the tracked copies of the Task 9 checkers). To rerun: `python tools/xdk_sigmatch/light_check.py --all`, `python -m unittest discover -s tests -p "test_light_check.py"`.
