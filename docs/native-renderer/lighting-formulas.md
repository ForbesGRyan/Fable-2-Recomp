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
5. **Not run**: `7CD5...` with `D7DD...` / `2D40...` (literal tables; run since plan Task 8c: "Tree leaves", item 3). **Not read**: `401A...` (it has no tf8 and takes its shadow from the two cascades tf6 / tf7 directly; `getWeights2D` was not interpreted then and is since the shadows sub-project, "Shadows" below), the 2- and 4-light shaders' lights 1 to 3, every `b135` block.
6. **Skinned normals** (`D4D5...`, `A1F7...`): closed since (plan Task 5a; "Objects" above and frame-map section 14, "Normals"): the bone transform and the step that makes the normal orthogonal to the tangent, checked against the microcode by a script of that task's evidence. Open: the formula is not in `light_formulas.py`, and the tangent element it reads is not among the light inputs the capture records, so the native skinned normal is built without the tangent step ("Objects").
7. **Inputs the spec's capture list (design 3) does not have**: the probe weights stream vf1 and the constants `c28..c39` with the position's fourth component (objects with `79EA...`, `D4D5...`, `A1F7...`, trunks, leaves: 76.0 draws per frame of the checked ones at the bridge and 182.5 by the trees, where `8D90...` with `79EA...` alone is 41.5); the colour stream's index offset `c20.x`; for plants the instance colour and the bend constants `c45`. **Closed**: the light inputs carry all of them since plan Task 4 (`c28..c39`, `c20.x`, `c45`; frame-map section 14, "Light inputs"), and the geometry cache builds the probe weights and set indices (plan Task 5b), the instance colours (5b) and the bend inputs (8b) ("Normals", "Probe colours", "Plants").

## Evidence

In `out/native-evidence/lighting/task2/` (git-ignored): `light_check_all.txt` (`light_check.py --all --trials 400`), `light_check_list.txt` (the 32 pairs), `coverage_bridge_trees.txt`, `vertex_constants_bridge_trees.txt`, `vertex_copies_bridge_trees.txt` and `fetch_layouts_bridge_trees.txt` with their scripts `vertex_constants.py`, `vertex_copies.py`, `fetch_layouts.py` (one pass each over the two captures: the vertex bank's registers the formulas read, their comparison with the pixel bank, and the fetch layouts and the position's fourth component of the shaders named here), `tess_c19_bridge_trees.txt` (`tess_c19.py`: the tess rows' vertex `c19` against pixel `c23`, and their zero sun constants), `mutations.txt` (the light tests run against five deliberately wrong functions), `fix1_recounts_bridge_trees.txt` (`fix1_recounts.py`: the colour stream's offset against the stream's size, the a0 index per shader, `c26`, `c27.w`, `c11.x`), `fix1_interpreter_mutations.txt` (the interpreter's hand tests against 18 broken conventions), `fix1_light_check_all.txt` (the 32 pairs after fix round 1), `vs_check.txt`, `ps_check.txt` (the tracked copies of the Task 9 checkers). To rerun: `python tools/xdk_sigmatch/light_check.py --all`, `python -m unittest discover -s tests -p "test_light_check.py"`.

## Shadows

Native renderer sub-project 8 (shadows), plan Task 2, 2026-10-08. The two pixel shaders that write the game's screen mask as closed formulas, the lit families with the mask as an input, and the shaders the game draws into its two shadow maps. Nothing here was run in the game. Every statement rests on the shader dumps, on the four GPU constant logs of the lighting sub-project's Task 1 (`out/native-evidence/lighting/task1_bridge1_gpu_pconst.log.gz`, `task1_trees1_...`, `task1_bridge3_...`, `task1_trees2_...`: 60, 60, 16 and 16 frames at the bridge and by the trees) and on the three captures of 2026-10-08 whose rows hold the shadow passes (`out/build/win-amd64-release/logs/native_discovery_20261008_105316.jsonl` and `_105833.jsonl` at the bridge, `_105659.jsonl` at the third spot; 8 frames each). Evidence files are in `out/native-evidence/shadows/`, all named `task2_*` (list at the end). What only a run can settle is listed under "Inputs that stand on inference"; the discovery task of the same plan (frame-map section 15) is where those are settled.

### Corrected after the later tasks

Added 2026-10-09 (plan Task 6). This part was written before anything was run; the discovery (plan Task 1), the capture, the maps and the mask (plan Tasks 3 to 5b) settled several things it leaves open or states otherwise. The text below is kept as written; where it disagrees with this list, the list holds. The evidence is in `frame-map.md` section 15.

1. *The literals.* "The literal tables are not in the binary dumps ... they are in the GPU constant logs" (item 1 below) is right but not the whole of it: the mask rows of plan Task 1's captures carry them as `"ps_lit"`, read from the shader object (eight registers from `c248` for `0x0F99...`, four from `c252` for `0x1B4B...`), equal to the register file in all 212 logged frames (section 15, "Mask draws", "Literals").
2. *The mask draws have decoded rows now* (`"kind":"verts"`), so "Inputs that stand on inference" 1 to 5 are no longer inference: tf13 is the scene depth at `0x1A02A000` (`k_24_8_FLOAT`, 1120 x 720, point, clamp); **tf6 and tf7 are the near and the far map, 1024 x 1024 `k_24_8_FLOAT`, point filter, clamp** (the filter of tf6, given below as not known, is point); **tf14 is a 32 x 32 `k_8_8` texture, point filter, repeat**, whose texels are unit vectors (length 0.989 to 1.010) and whose two bytes are exchanged by the 8-in-16 swap before they are the shader's x and y; `c36..c43` at the mask draws equal the rows' `"pfull"` in every paired frame; and the second rectangle (`0x0F99...`) has no stencil, alpha or depth test, so in the emulated frame it writes every pixel (how the two share the screen on a console stays an inference).
3. *The sample position of a map's texel* (inference 6 below) is settled for the native maps: they are drawn with a viewport origin of (0.5, 0.5), so the registers' rows with their half texel address them as they stand; a lookup is tested against the pass's own matrix (section 15, "Native shadow maps", "Native shadows").
4. *The casters outside the main scene.* Item 6 below says a caster's vertex shader cannot be replayed by `position_check.py` because shadow-pass rows carry no stream dumps. By the review of plan Task 2, `0x5781...`/`0x3347...` also has 9 rows inside the main scene, with stream dumps (the plan's ledger names `native_discovery_20261008_105316.jsonl`). Plan Task 6 did not find them again: that capture has no row of the shader, and the 112 rows of `native_discovery_20261008_105659.jsonl` are all outside the scene; so this correction stands on the review's word and the replay was not made.
5. *What was drawn differently from these formulas.* Only `0x0F99...` is ported (`0x1B4B...` is overwritten everywhere in the emulated frame). The lit shaders read the mask as the game's fetch of tf8 does: linearly at the pixel's corner, the mean of four mask texels (plan Task 5b), which the family formulas here take as one value. The mask's HLSL is held to these formulas by pinned numbers in its C++ mirrors, by text pins and by a comparison of the GPU's output with the same arithmetic on the CPU (99.2% of the pixels within one level of 255; section 15, "Oracle and fix round"), not by a test that runs the HLSL.
6. *`c12.w`* was seen between 0.462 and 0.512 in the captures and logs of plan Task 1 (and at 0.532 in a later run: run 378's log); it moves with the game's clock.

### Result

| What | Shader | Function of `light_formulas.py` | State |
|---|---|---|---|
| The mask of a pixel | `0x0F99810F072BF0FC` | `mask_0f99` = `mask_near`, `mask_far`, `mask_combine` | closed: 2000 of 2000 random pixels, largest relative difference 1.6e-11 |
| The mask of a distant pixel | `0x1B4B5AE12EF270CA` | `mask_1b4b` = `mask_far` and a fade by depth | closed: 2000 of 2000, 6.9e-12 |
| The near cascade's term alone | `0F99...`, `d < c12.x` | `mask_near` | 2000 of 2000 (exact: a count of taps) |
| The far cascade's term alone | both | `mask_far` | 2000 of 2000 each, 2.5e-11 and 9.1e-12 |
| The mask in the lit families | the 40 pairs of `light_check.py`'s `FAMILIES` | unchanged: `sun_gate_single`, `sun_gate_multi`, `leaf_mask` | the mask was already a random input of every family's check; with the formula taking it as 1 every masked pair fails ("The lit families") |
| Casters | 9 shader pairs | `shadow-casters.json`, `vs-transforms.json` | all nine in the table; two limitations ("Casters") |

(`task2_light_check_mask.txt`: `light_check.py --family mask`, `--family mask-near`, `--family mask-far`, 2000 trials each; tolerance 1e-4 relative, over all four output components.)

What differs from the research note (`.superpowers/sdd/shadows-research.md`) and the spec, found by this reading:

1. *The literal tables are not in the binary dumps.* `shader_<hash>.ucode.bin.frag` is the microcode alone (`0F99...`: 345 words: the dump numbers its 103 instructions from 11, three words each with the control flow before them, and three zero words end the file; `1B4B...`: 123 words, 35 instructions numbered from 5). The literals of both shaders are in the GPU constant logs, which hold all of `c248..c255` at every execution ("The literals").
2. *`c77.x`, `c46`, `c47` are known at 152 frames, not one.* The four logs hold them at both mask draws of every frame, and they are one value each for `0F99...` in all of them ("Inputs").
3. *The three depth constants of `1B4B...` are `c12` over again.* Its start, fade start and fade end are the depth values of the view distances `c12.x + c12.z` (22), `c12.y` (35) and `c12.y + 10` (45) in every one of the 152 frames ("The far shader").
4. *`0x57818A7FD1C4F026` had a table entry* (a plain one, from the materials sub-project). Without one were `0xA8A5...` and `0x87E0...`; and none of the five alpha-tested casters' vertex shaders had the `"uv"` their pixel shaders need.
5. *A pixel where no cascade applies gets `c12.w`, the sky included.* Beyond `c12.y + 10` units both terms are zero and the fade is complete; a depth of the clear value is such a pixel (with the logged `c119` its distance is `1 / (c119.z + c119.w)`, about 5,000 units). The spec's design 5 gives the mask 1 there; no lit surface is drawn at the clear depth, so the difference shows only where a draw that writes no depth (a blended one) lies over the sky.
6. *The rows of a pass outside the main scene carry no stream dumps*, so `position_check.py` cannot replay a caster's vertex shader on the existing captures. `vs_check.py --casters` checks the table entries against the microcode instead ("Casters").

What the note states about the two shaders' arithmetic (its section 1, "The mask pass", instruction numbers included) was found as stated; the compare directions, the tap pairing and the fade's target are now machine-checked.

### The interpreter

`ucode_eval.py` gained what the two shaders need (`tests/test_ucode_eval.py`, `MaskShaderInstructions`: hand-computed):

- **`getWeights2D`**: the linear filter's two lerp factors, `frac(coordinate * size + Offset - 0.5)` per axis, in the source's x and y. That is the rule of the SDK's translator (`thirdparty/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp`, lines 724 to 730 and 949 to 970: the offsets are added in texels, 0.5 is subtracted "for coordinate lerp factors", then `frc`). The texture's size is the caller's (`tex_size`). With the shaders' offsets of -0.5 the factors are `frac(coordinate * size)`.
- **Fetches with texel offsets** (`OffsetX=-1`): the sampler callback gets the attributes; `point_texel` is the texel a point fetch reads, `floor(coordinate * size + offset)`.
- The predicated branch as `0F99...` has it (a jump on the cleared predicate over a block whose instructions carry `(p0)`, a `serialize` line whose instruction has no number) ran already; a test now pins it.

Not modelled: the translator's own rounding offset of 1.5 / 1024 texel on every fetch coordinate (an emulator measure against fetches that land between texels), and single precision.

### The literals

The emulated GPU's registers at every execution of the two shaders in the four logs, one value per register in all 152 frames (`task2_gpu_log_mask_registers.txt`, made by `task2_mine_gpu_log.py`; the bits are in the file):

| Register | `0F99...` | `1B4B...` |
|---|---|---|
| `c248` | (10, 1/12, -1, 2) | not read |
| `c249` | (0, 1, 0.5, 0.1) | not read |
| `c250` | (-0.480026, -0.597710, -0.194983, 0.767022) | |
| `c251` | (0.473434, -0.791559, -0.203345, 0.185461) | |
| `c252` | (-0.073580, 0.412458, -0.695914, 0.507431) | |
| `c253` | (-0.405810, -0.932615, 0.962340, 0.519456) | |
| `c254` | (-0.326212, -0.321940, 0.620716, -0.893124) | (0.5, 1, 0, -1) |
| `c255` | (-0.840144, 0.896420, 0.457137, 0.064425) | (2, 0, 0, 0) |

In `0F99...`: `c248.x` = 10 is how far beyond `c12.y` the far cascade is still read, `c248.y` the weight of a near tap, `c249.w` = 0.1 the slope of the fade (1 / 10); the others are the 0, 1, 0.5, -1 and 2 of the arithmetic. `c250..c255` are the twelve tap offsets, with **the x of a tap in one register and its y in another** (instr 33 to 50: one register's pair is multiplied by the rotation's two terms, the other's is added times the same terms exchanged):

```
(c251.z, c254.z) (c251.w, c254.w)   (c253.z, c250.z) (c253.w, c250.w)   (c252.z, c255.z) (c252.w, c255.w)
(c255.x, c252.x) (c255.y, c252.y)   (c251.x, c250.x) (c251.y, c250.y)   (c254.x, c253.x) (c254.y, c253.y)
```

That is `light_formulas.MASK_NEAR_TAPS`: (-0.203345, 0.620716), (0.185461, -0.893124), (0.962340, -0.194983), (0.519456, 0.767022), (-0.695914, 0.457137), (0.507431, 0.064425), (-0.840144, -0.073580), (0.896420, 0.412458), (0.473434, -0.480026), (-0.791559, -0.597710), (-0.326212, -0.405810), (-0.321940, -0.932615): twelve points inside the unit disc, the 12-tap Poisson disc of the published shader samples (a recognition, not evidence; the pairing is what the check holds: with the pairs read within a register, or one tap's x and y exchanged, the check fails).

### The mask of a pixel: `0x0F99810F072BF0FC`

```
depth = tf13((|pixel| + 0.5 + c46.zw) * c46.xy)                      the scene depth at the pixel's centre
S     = (X, -Y, 1 - depth, 1),  X = (2 |px| + 1 + 2 c46.z) * c46.x - 1,  Y likewise     (mask_screen_point)
P     = (c116 . S, c117 . S, c118 . S) / (c119 . S)                  the world position
d     = |P - c31.xyz|                                                 its distance from the eye

near  = 0;  where d < c12.x + c12.z:                                  (mask_near)
          (a, b)  = 2 * tf14((|pixel| + c47.xy) * c47.zw).xy - 1
          (u, v)  = (c36 . S, c37 . S) / (c39 . S),   z = (c38 . S) / (c39 . S)
          tap i   = (u, v) + c77.x * (a x_i + b y_i,  a y_i - b x_i)          (x_i, y_i) the twelve offsets
          near    = c248.y * (number of taps with  1 - z > tf6(tap i))        strictly greater
far   = 0;  where c12.x < d < c12.y + c248.x:                         (mask_far)
          (u, v, z) = (c40 . S, c41 . S, c42 . S) / (c43 . S);   x = u * width, y = v * height
          lit(i, j) = 1 where 1 - z >= tf7 texel (i, j), else 0               greater or equal
          far = (1 - fx)(1 - fy) lit(ix - 1, iy - 1) + fx (1 - fy) lit(ix, iy - 1)
              + (1 - fx) fy lit(ix - 1, iy) + fx fy lit(ix, iy),   ix = floor(x), fx = frac(x), the same in y

m     = lerp(near, far, sat((d - c12.x) / c12.z))                     (mask_combine)
mask  = lerp(m, c12.w, sat(c249.w * (d - c12.y)))                     oC0.xyz; oC0.w = 1
```

`mask_0f99(pixel, sample_depth, depth_scale, depth_offset, screen_to_world, eye, split, near_rows, far_rows, sample_rot, rot_offset, rot_scale, offsets, radius, sample_near, sample_far_texel, far_size, tap_weight, far_reach, slope)`, keyword arguments; the three parts:

```python
mask_near(P, near_rows, sample_near, rot, offsets, radius, tap_weight=None)   # lit taps * tap_weight (1 / len(offsets))
mask_far(P, far_rows, sample_far_texel, size)                                  # the weighted sum of four texels
mask_combine(d, near, far, split, slope)                                       # split = c12, slope = c249.w
```

`P` is any homogeneous point the rows apply to, and each cascade divides by its own fourth row. The game's rows at the mask draws are for the screen point `S`; with the world-to-map rows and the world position `(x, y, z, 1)` the functions return the same number (the screen rows are the world rows times `c116..c119`; a test compares the two forms on 50 random matrices). So a native pass that has the world position needs no screen-space rows.

With `c12` = (20, 35, 2, `w`): the near map alone to 20 units from the eye, a blend to the far map over the next 2, the far map to 35, a fade to `c12.w` over the next 10, and `c12.w` beyond, where neither cascade is read. `c12.w` is **not 1**: 0.4637 and 0.4833 in the two GPU logs' frames, and one value per frame between 0.4828 and 0.5119 in the captures (the far terrain's `c11.x` is the same number, context brief A3).

The far term is the linear filter of four lit / shadowed texels, not of four depths: it is what a bilinear fetch of the comparison's result at `(u, v)` minus half a texel would return. The half texel is the one the scene's `c36..c43` carry in their translation (research note, "The light matrices").

Instruction numbers. Depth coordinate (11, 13, 15), fetch (16); X and Y (11, 12, 14, 15, 17), `1 - depth` (17, scalar half); S (18); world position (19 to 22, with `1 / w` in 22), minus the eye (23), `d` (24, 25); the near predicate `c12.x + c12.z > d` (25 to 27); both terms zeroed (27: `-|x| > 0` is never true). Near block: rotation coordinate (28, 29), fetch (30), `* 2 - 1` (32); S again (31, 34); offsets times the rotation (33 to 41, 45 to 50); the three row products (42 to 44) and `c39 . S` (51); `* c77.x` (52 to 56), `1 / w` (56), plus `(u, v)` (57 to 62); twelve fetches of tf6 (63 to 74); `1 - z` (75); `sgt` (76 to 78); the sum (79 to 85); `* c248.y` (86). Far predicate `d > c12.x` and `c12.y + c248.x > d` (87 to 91). Far block: S (92, 93); rows (94 to 97), division (97, 98); four fetches of tf7 at texel offsets (-1, -1), (0, -1), (-1, 0), (0, 0) (99 to 102); `getWeights2D` with offsets -0.5 (103); `1 - f` (104); the four products (105); `1 - z` (105, scalar half); `sge` (106); the weighted sum (107). Output: `far - near` (108); `d - c12.y`, `d - c12.x`, `1 / c12.z` (109); the blend weight, saturated (110); `m` (111); `c12.w - m` and the fade weight, saturated (112); the result with alpha 1 (113).

### The far shader: `0x1B4B5AE12EF270CA`

```
depth = tf13((|pixel| + 0.5 + c31.zw) * c31.xy);   z = 1 - depth
z <= c46.x:   oC0 = (c12.w, c12.w, c12.w, 0)
else:         oC0.xyz = lerp(mask_far(S, c40..c43, tf7), c12.w, sat((z - c47.x) / c77.x));   oC0.w = 1
```

`mask_1b4b(pixel, sample_depth, depth_scale, depth_offset, far_rows, sample_far_texel, far_size, start, fade_start, fade_width, fade_to)` returns `(value, alpha)`. The far term is `0F99...`'s, the same four texels and weights, compared with `>=` (instr 27 to 36). The shader has no world position: it works by the depth value, the fade as `((1 - c47.x) - depth) * (1 / c77.x)` (9, 32, 36, 37).

In view distance its three constants are `c12` again. With `c119` of the same frame's `0F99...` draw, the distance along the view axis of a pixel of depth `z` at the screen's centre is `1 / (c119.z * z + c119.w)`; over all 152 frames of the four logs that gives 21.999 to 22.003 units at `c46.x` (`c12.x + c12.z`), 34.997 to 35.004 at `c47.x` (`c12.y`) and 44.996 to 45.014 at `c47.x + c77.x` (`c12.y + c248.x`) (`task2_far_shader_depths.txt`). So where this shader writes alpha 1 the near block of `0F99...` cannot run (the distance is at least the view depth, beyond 22), and the two shaders agree there but for the fade's shape: `1B4B...`'s is linear in the depth value, `0F99...`'s in the distance. Along the view axis the two fade weights differ by at most 0.063 (at 39.7 units; arithmetic on the two expressions, not a measurement), which times `c12.w - far` is at most 0.034 of mask. **Inference**: that the game writes `1B4B...`'s result exactly at the pixels where its alpha is 1 (through the alpha test, with the stencil keeping `0F99...` off them) is read from the two rectangles' depth words and from this alpha, not from a row; the discovery task settles it. A native pass that evaluates `mask_0f99` at every pixel reproduces both rectangles up to that fade shape.

Instruction numbers. Depth coordinate (5 to 7), fetch (8); `1 - depth` (10, scalar half); X and Y (5, 10, 11, 15); `z - c46.x` (12), the predicate and the alpha (13); the value of the near side `c12.w` (12, scalar half). Far side: the four rows as scalar products (14 to 25), `1 / w` (25), division (26); fetches (27 to 30); weights (31, 33, 34); `1 - z` (34, scalar half); `sge` (35); the weighted sum (36). Fade: `1 - c47.x` (9), `- depth` (32), `1 / c77.x` (32), the product, saturated (36, 37); `c12.w - far` (37); the lerp (38); output (39).

### Inputs

| Input | Register at the mask draw | Value and where it is from |
|---|---|---|
| depth scale, offset | `0F99...` `c46`; `1B4B...` `c31` | (1/1120, 1/720, 0, 0) in all 152 frames of the four logs |
| screen to world | `c116..c119` | per frame (`0F99...` draw); one value over the 60 and 16 frames at the bridge, where the camera stands |
| eye | `c31.xyz` of `0F99...` | (82.0721, 157.339, 50.1707) in the logs at the bridge, the pixel `c19` of the captures' rows at the same spot (`task2_caster_rows.txt`) |
| `split` | `c12` | (20, 35, 2, 0.4637) and (20, 35, 2, 0.4833) at the mask draws of the two frames the lighting task printed in full (`task1_bridge1_mask_draws_gpu_frame_1040.txt`, `task1_trees1_mask_draws_gpu_frame_1643.txt`); in the three captures (the followed registers of the rows, `task2_c12_rows.txt`): `c12.xyz` = (20, 35, 2) in all 28,439 rows of the shadow passes and the main scene, `c12.w` one value per frame, 0.4828 to 0.5119 over the 24 frames |
| near rows, far rows | `c36..c39`, `c40..c43` | at the mask draws: for the screen point (research note; row 0 held against the world rows times `c116..c119` there). At scene draws the same registers hold the world-to-map rows |
| rotation coordinate | `c47` of `0F99...` | (0.5, 0.5, 1/32, 1/32) in all 152 frames: the pixel's centre over 32 |
| kernel radius | `c77.x` of `0F99...` | 2.5 / 1024 (0.00244141) in all 152 frames: 2.5 texels of the near map |
| far map size | the fetch constant of tf7 | 1024 x 1024 by inference: `c13.x` = `c14.x` = 1/1024 in the same two frames and in every row of the three captures; the shaders do not read `c13` or `c14`, the size enters through the fetches |
| start, fade start, fade width | `c46.x`, `c47.x`, `c77.x` of `1B4B...` | 0.995474, 0.997163, 0.000636 at the bridge; by the trees they move in the sixth place with `c119` (above: 22, 35 and 45 units) |

### Inputs that stand on inference

These are inputs of the functions, so the formulas do not depend on them; the check drew each at random. What they are in the game is settled by the discovery task's rows (frame-map section 15).

1. **tf13 is the scene depth of the pre-pass**, holding the reversed value (the shaders use `1 - depth` as the depth that is 0 at the near plane). The mask draws have no decoded row; inference from the arithmetic and from `c119`.
2. **tf6 and tf7 are the near and the far map**, 1024 x 1024, holding `1 - depth` of the surface nearest the light. Inference from the blended characters' shader `0x401A...`, which binds two such textures in slots 6 and 7 (research note). **The filter of tf6 is not known**: the near taps are compared after the fetch, so a linear filter would compare interpolated depths. The check and the tests' depth maps use point fetches.
3. **tf14, the rotation texture**: its size, contents, filter and addressing are unknown. `c47.zw` = 1/32 makes it repeat every 32 pixels if it wraps. The shader does not normalise `(a, b)`: whether the texels are unit vectors is the texture's matter.
4. **`c36..c43` at the mask draws are the scene's world rows times `c116..c119`**: held for one row of one frame by the research note.
5. **How the two rectangles share the screen** ("The far shader").
6. **Which sample position a map's texel has.** The half texel in the scene's rows suggests the game's rasteriser takes the texel's corner as its sample position (inference; the register that decides it is in no row). A native map drawn with the pass's own matrix has its samples at the texel centres, and then the half texel of `c36..c38` / `c40..c42` shifts every lookup by half a texel (0.015 units in the near map, 0.034 in the far one). Left to the tasks that draw the maps; named here because the formulas take the rows as given.
7. **Anything at night, in town or indoors**: every value here is from two daylight places.

### The check

`light_check.py --family mask` runs each shader's microcode on random pixels against `mask_0f99` / `mask_1b4b`; `--family mask-near` and `--family mask-far` run it where only one cascade's block decides the output (`0F99...`: `d < c12.x`, or `c12.x + c12.z < d < c12.y`; `1B4B...`: beyond its start and before its fade) against `mask_near` or `mask_far` alone. `--all` runs the three after the 40 pairs.

One trial: a random pixel (whole or fractional, either sign of the position register, which the shaders read through its absolute value) and depth; screen-to-world rows built so that the pixel lies at a chosen distance from the eye, the distance drawn from one of five regimes (near only, blend, far only, fade, beyond; `1B4B...`: not far, far, fade, beyond); random cascade rows with a fourth row that is not (0, 0, 0, 1); a random `c12`; random depth and rotation offsets and scales in half of the trials; a far map of 1024 x 1024 or 512 x 2048. The textures are functions of the texel: the two maps and the rotation texture give a fixed pseudo-random value per texel and trial, and the depth texture answers only at the coordinate the shader must compute (anything else is NaN and fails the trial). For `0F99...` half of the trials replace the literal table by random offsets, reach, tap weight and slope (the form holds for any table), and in a quarter of all trials the cascades' depth rows are constants and the maps hold the compared value itself in half of their texels, so the equal case of both comparisons is reached with exact numbers. Of 400 trials of `0F99...`: 85 near only, 67 blend, 83 far only, 87 fade, 78 beyond; 100 with equal depths; 204 with a random table (`task2_mask_mutations.txt`).

**Switched off: nothing.** The two shaders have no bool blocks and no fog; every instruction is reached by some trial and every constant the dumps name is read (`0F99...`: 12, 31, 36 to 43, 46, 47, 77, 116 to 119, 248 to 255; `1B4B...`: 12, 31, 40 to 43, 46, 47, 77, 254, 255). What a trial replaces are the textures (above) and, in the trials with a random table, the literals.

That the check can fail (`tests/test_light_check.py`: `MaskSynthetic`, which always runs, on a hand-written shader of `0F99...`'s form with two near taps; `MaskRealDumps` on the two dumps; counts of 400 trials in `task2_mask_mutations.txt`): a near comparison reversed or made inclusive, a far comparison reversed or made strict, the far texels at offsets 0 and +1, the far weights taken as `frac(x - 0.5)` or exchanged, the fade to 1 in the place of `c12.w`, the blend over `c12.y`, the rotation reversed or dropped, a tap's x and y exchanged, the offsets paired within a register, one tap moved by a tenth of the radius, eleven taps; for `1B4B...` the fade to 1, the fade from the start depth, alpha 1 everywhere. Each disagrees (the least sensitive, one tap moved by a tenth of the radius, still in 14 of 400 trials). `MaskFormulas` pins the functions without microcode: hand-computed values; constant maps (all lit, all shadowed, and a map equal to the compared depth: shadow for the near cascade, light for the far one); a half-plane edge, where the near term's penumbra runs from one kernel extent to the other (1.80 radii unrotated, within one radius on either side under any rotation) and the far term is a ramp of exactly one texel; zeros and non-finite values.

**Guards** (`light_formulas.py`; the microcode has none and the check never feeds such inputs): a projection whose `w` is zero or whose results are not finite gives 1 for that cascade without a fetch; a tap with a coordinate that is not finite counts as lit; a blend width that is not positive is a step at `c12.x`; a pixel without a finite world position gives `c12.w`; every function returns a finite number for any input.

### The lit families

The research note's table (its section 4) says the mask is fixed to 1 in the families' checks. It was not: since the lighting sub-project's Task 2 every trial draws the tf8 texel at random and the family's `sun_gate` is made of it ("Method" above). Nothing in the families' functions changed in this task, so every value pinned for the renderer stands, and at a mask of 1 each function is what it was. What this task adds is the proof that the mask is an input everywhere (`tests/test_light_check.py`, `LitFamiliesWithTheMask`; `task2_family_mask.txt`): with `sun_gate_single`, `sun_gate_multi` and `leaf_mask` made to ignore the mask, every one of the 38 pairs of the twelve masked families disagrees with the microcode (200 trials a pair; between 53 and 164 of them still agree, the ones whose sun term is zero), and so does each two- and four-light family with the sun in the game's slot. The far terrain has no mask (two pairs).

| Family | The sun term with the mask |
|---|---|
| `terrain-layer` | `sun_gate = sat(8 * (N . c109)) * mask` |
| `object`, `tree-trunk`, `plant`, `plant-lit` | `sun_gate = mask` |
| the `-multi` forms | light 0: `shadow = (1 - facing * mask) * c112.x`, `sun_gate = (1 - shadow) * (1 + c94.x * (cone * range - 1))`; with the game's light set (the sun behind dark lanterns, `c112` one-hot at its slot) the one-light form of the sun: `--multi-sun`, `task2_light_check_multi_sun.txt` |
| `tree-leaf`, `tree-leaf-multi` | `m1 = 0.6 * mask + 0.4` on the front light, `m2 = 0.6 * m1 + 0.4` on the back light: 0.4 and 0.64 in full shadow |
| `terrain-far` | none: its sun is scaled by `c11.x`, the number the mask fades to (`c12.w`) |

`task2_light_check_all.txt` is `light_check.py --all --trials 400` after this task: 40 of 40 pairs and 5 of 5 mask checks agree in every trial.

### Casters

The pairs of shaders the game draws into the two maps: every decoded row with surface word `0x10000410` of the three captures (1,855, 2,315 and 1,864 rows in 8 frames each; `task2_caster_rows.txt`, made by `task2_caster_rows.py`). All have depth word `0x00708766` (greater-equal, write).

| Vertex / pixel shader | Rows (bridge, third spot, bridge) | Kind | Alpha | State |
|---|---|---|---|---|
| `0xF160...` / none | 464, 815, 464 | opaque | | `0x2202` = `0x00000004` |
| `0xFBD3...` / none | 400, 376, 400 | opaque | | `0xAA000004` (no test) |
| `0x82F6...` / none | 101, 184, 104 | opaque | | `0x00000004` |
| `0x5F44...` / none | 8, 120, 8 | opaque | | `0x00000004` |
| `0x4325...` / `0xF1EB...` | 368, 336, 368 | alpha-test | `tf0.w * c15.w` | test greater, reference 0, and alpha-to-mask (`0xAA00001C`); no cull (`0x2205` = `0x00219800`) |
| `0x3A0F...` / `0x3347...` | 296, 212, 294 | alpha-test | `tf0.w` | test greater (`0x0000000C`), reference 254/255, in 32 + 32 rows 128/255 |
| `0x5781...` / `0x3347...` | 0, 112, 0 | alpha-test | `tf0.w` | reference 128/255 |
| `0x87E0...` / `0x3347...` | 0, 80, 0 | alpha-test | `tf0.w` | reference 254/255 |
| `0xA8A5...` / `0x85A9...` | 218, 80, 226 | alpha-test | `tf0.w` times the dissolve | reference 254/255 |

`docs/native-renderer/shadow-casters.json` holds the nine as `{vs, ps, kind, alpha: {texture_slot, scale_register}, limitation, evidence}`; `tools/xdk_sigmatch/gen_caster_table.py` writes `src/native/capture/shadow_caster_table.inc`, one `FABLE2_SHADOW_CASTER(vs, ps, kind, slot, scale_reg)` a pair (kind 0 opaque, 1 alpha-test; `ps` 0 without a pixel shader; `scale_reg` register * 4 + component, -1 none). The generator is strict (an unknown key, missing evidence, an alpha entry without a pixel shader, a pair listed twice are refused with the hash) and holds the table against `vs-transforms.json`: a caster's vertex shader needs a transform there, an alpha-tested one's also the `"uv"` of interpolator 0. Nothing includes the table yet.

**The pixel shaders.** All three read tf0 at interpolator 0 (xy) times `c8.xy` plus `c8.zw` (instr 2, 3) and have no kill; the draw's alpha test decides.

- `0xF1EB...`: `oC0.w = tf0.w * c15.w` (instr 4). `c15` is (0, 0, 0, 1) in all 1,072 rows.
- `0x3347...`: `oC0 = tf0` (instr 4).
- `0x85A9...`: `oC0.w = pass * tf0.w` (instr 14), with

```
fade   = sat(c255.x - (|c19.xyz - P| - c15.z) * c15.y)         P: the world position (interpolator 1), c19 the eye   (7 to 11)
dither = tf10.w at (|pixel.xy| * c0.zw + c0.xy) * c50.xy       a 32 x 32 texture; c50 = (35, 22.5)                    (3, 4, 6)
pass   = 1 where fade + dither >= c255.x, else 0               c255.x = 1 ("ps_lit" of all 524 rows)                 (12, 13)
```

  **The dissolve is not one comparison of constants**: it compares a per-pixel distance term plus a texel of a dither texture with 1. By the plan's rule it is left out and is a limitation of the table: the caster is drawn as the texture's alpha test alone. How far that is from the shader: `c15` is (62.4, 0.625, 62.4, 0) in 450 rows and (46.8, 0.8333, 46.8, 0) in 74. Within `c15.z` of the eye `fade` is exactly 1 and so is `pass` whatever the dither (it is not negative), so the alpha test alone is the shader's own result there; over the next `1 / c15.y` (1.6 or 1.2 units) the game thins the cut-out, and just beyond the vertex shader drops the copy (its cut `c13.z`: 65.1, 66.3 and 51.1 units). The mask shows a cascade's shadow only within `c12.y + 10` = 45 units of the eye, so what the limitation changes is the shadow of bushes 46.8 units and more away falling on ground nearer than 45.

**Other limitations in the table.** The three deformed vertex shaders (`0xFBD3...` trunks, `0x4325...` leaves, `0xA8A5...` bushes) are drawn without their sway, as the scene's trees are. The leaves' alpha-to-mask (`0x2202` bit 4 with the offsets `0xAA`) is not read: the caster is the alpha test alone, alpha above the reference 0.

**The vertex shaders.** `vs-transforms.json` gained entries for `0xA8A5B2A89F70336A` (instanced: the scheme of `0x8123...` without the instance colour: rows at fetches 0 to 2, the mesh position at fetch 3, `c12` = (1 / count, count, first), bias 0.5, offset `c7.xyz`, cut `c9.xyz` / `c13.z`; deformed) and `0x87E0DB8B7C32F6A5` (one bone per vertex, the index in the register's z; rows at fetches 3 to 5), and `"uv"` for the five alpha-tested casters' vertex shaders (`0x4325...`, `0x3A0F...`, `0x5781...`, `0x87E0...`, `0xA8A5...`); `vs_transform_table.inc` is the generator's output for it (15 lines more).

`position_check.py` replays stream dumps, and rows outside the main scene have none; these vertex shaders are drawn nowhere else. So the entries are checked against the microcode: `vs_check.py --casters` builds random streams and constants laid out as the dump fetches them, replays the entry with `position_check.py`'s own functions (the position fetch and its swizzle, the instance expansion, the bone skin) and compares the projected position, the vertices an instanced entry's cut drops and the entry's uv with the shader's microcode on the same bytes (`task2_vs_check_casters.txt`, 100 draws of 12 vertices each):

| Vertex shader | Form | Agree | Compared |
|---|---|---|---|
| `0xF160...` | plain | 1200 of 1200 | position |
| `0x5781...` | plain | 1200 of 1200 | position, uv |
| `0x5F44...` | one bone | 1200 of 1200 | position |
| `0x82F6...` | four bones | 1200 of 1200 | position |
| `0x3A0F...` | four bones | 1200 of 1200 | position, uv |
| `0x87E0...` | one bone | 1200 of 1200 | position, uv |
| `0xA8A5...` | instanced | 1200 of 1200 | position with the sway's amplitudes `c26` zero, the cut (626 vertices dropped by both), uv |
| `0x4325...` | deformed | 1200 of 1200 | uv only: the sway has no switch |
| `0xFBD3...` | deformed, no uv | not run | nothing to compare: its entry says only that the position is deformed |

The five entries that existed agree as well, which is the check of the check; 21 wrong entries (rows reversed, the index's other component, another fetch, swizzles as stored, a bias of a vertex and a half, no cut, u and v exchanged, ...) each fail (`tests/test_gen_caster_table.py`). What this does not show is anything about the game's data. From the rows (`task2_caster_vertex_rows.txt`): in 524 of 524 rows of `0xA8A5...` the vertex bank's `c12` is (1 / count, count, first) with count a whole number (256, 190, 692) and the copies the largest index reaches lie inside the row's instance stream; of the 6,912 sampled positions of `0x5781...`'s 112 rows, 6,200 lie inside the light's clip volume under the row's `c0..c3`. `0x87E0...`'s bone rows are in no row. **Open**: a replay of real streams (`position_check.py`) needs shadow-pass rows with stream dumps, which the capture task of this plan can write.

### Evidence

In `out/native-evidence/shadows/` (git-ignored): `task2_gpu_log_mask_registers.txt` (`task2_mine_gpu_log.py`: the registers of the two mask shaders in the four GPU logs); `task2_far_shader_depths.txt` (`task2_far_shader_depths.py`); `task2_light_check_mask.txt` (`--family mask`, `mask-near`, `mask-far`, 2000 trials); `task2_light_check_all.txt` (`--all --trials 400`); `task2_light_check_multi_sun.txt` (`--multi-sun --trials 400`); `task2_mask_mutations.txt` (`task2_mask_mutations.py`: the wrong forms and the regimes); `task2_family_mask.txt` (`task2_family_mask.py`: every lit pair with the mask taken as 1); `task2_caster_rows.txt` (`task2_caster_rows.py`: the shadow-pass rows per pair, their state words and constants); `task2_c12_rows.txt` (`task2_c12_rows.py`: pixel `c12`, `c13`, `c14` over the rows); `task2_caster_vertex_rows.txt` (`task2_caster_vertex_rows.py`); `task2_vs_check_casters.txt` (`vs_check.py --casters`); `task2_add_table_entries.py`, `task2_write_caster_table.py` (how the two tables' entries were written); `task2_python_suite.txt`, `task2_native_suite.txt`. To rerun: `python tools/xdk_sigmatch/light_check.py --family mask`, `python tools/xdk_sigmatch/vs_check.py --casters`, `python -m unittest discover -s tests -p "test_light_check.py"`, `-p "test_gen_caster_table.py"`, `-p "test_ucode_eval.py"`.

## Fog and sky

Native renderer sub-project 9 (fog, in-scatter and sky), plan Task 2, 2026-10-09. The fog block of the pixel shaders, the trees' vertex form of it and the sky's pixel shader as closed formulas, each checked against the microcode, and the list of fogged pixel shaders as data for the capture. Nothing here was run in the game. Every statement rests on the shader dumps, on one GPU-side log of the shadows sub-project for the literal registers (`out/build/win-amd64-release/logs/gpu_shadow_bridge2.log`, 40 frames at the bridge; frame 1116 is the one quoted) and three more of the same kind for one count ("Which literal is the 1"), and on the research note `.superpowers/sdd/fog-research.md`, whose reading this part checks. **No texel of any fog table was known when this was written** (the discovery of the same plan, Task 1, captures them): every check here runs on random tables, and what the tables hold, their decode included, is not this part's subject.

### Result

| What | Shaders | Function of `light_formulas.py` | State |
|---|---|---|---|
| The fog block, plain form | 30 pixel shaders | `fog` (with `fog_coords`, `table_1d`) | closed: 4,319 of 4,319 fog trials, 1,458 of 1,458 near trials bit for bit; largest relative difference 7.2e-14 |
| The fog block, blended form | 10 pixel shaders | `fog(..., scatter_scale=alpha)` | closed: 1,540 of 1,540, 460 of 460 near |
| of these: the two water shaders | `A17D...`, `F6D9...` | `fog` | closed, for the first time: 131 of 131 fog trials (of 400; 200 killed, 23 kept out of the block by the shaders' outer predicate, 46 near) |
| The trees' vertex form | 4 vertex shaders: `475E...`, `A584...`, `D7DD...`, `2D40...` | `vertex_fog` | closed: 576 of 576 fog trials, 224 of 224 near |
| The tree pixel shaders' use of it (`b132`) | `00E0...`, `58D3...`, `2FBE...`, `7CD5...` (6 pairs) | `fog_apply` on the family's formula | closed: 1,200 of 1,200 |
| The sky | `F760...` | `sky` (with `sky_latlong`, `sky_paths`, `sky_extinction`) | closed: 218 of 218 (18 named directions, 200 random), largest relative difference 8.2e-14 |
| The lake | `2B09...` | none | not of the form, not checked |
| Two more vertex shaders of the trees' form | `D57E...`, `129F...` | none (`vertex_fog` by the reading) | read only |

(`out/native-evidence/fog/task2_fog_check.txt`: `fog_check.py --trials 200`, exit code 0. Tolerance 1e-4 relative per channel for the fog checks, 1e-9 for the sky; table coordinates to 1e-9. The 38 shaders the research ran give its counts again, 5,728 of 5,728 fog trials: the trials are the same draws.)

What differs from the research note and the spec, found by this reading:

1. *The water shaders are of the plain form*, world position in interpolator 0, output scale `c20.w` alone. The research could not run them.
2. *`F760...` needs nothing new from the interpreter except at the two poles*, where its arctangent multiplies 0 by 1 / 0 (instructions 17 and 19). Everywhere else the existing interpreter runs it.
3. *The research's trials hardly exercised the blend from `c68` to the in-scatter table*: with its `c67.z` between 0.02 and 1 the blend is complete within 50 units of the start, and a formula without the blend passed the check at 40 trials on `8D90...`, `401A...` and `A17D...` (seen when the tests were first run). Half the trials here get a slope that leaves the pixel inside the blend (2,124 of the 5,859 fog trials are inside it); the same wrong formula now fails on every shader tried (`tests/test_fog_formulas.py`).
4. *The vertex form has no amount.* `vertex_fog` has vertex `c8.x` in the place of pixel `c28.y` and nothing in the place of `c28.x`. It is the pixel block only where `c28.x` = 1 (every fogged row of the 91 captures of the research).
5. *A two-row tf17 would not be the pixel form.* tf17 is fetched as a 2D texture at (angle, near blend). By the addressing below, a texture of two rows read linearly gives the second row the weight `sat(2 t - 0.5)`, not `t`. So "tf17 = {`c68`, tf4}" (spec, ruling 4) can hold only as a table with enough rows to carry the ramp, or with other texel positions. **What the captures show** (frame-map section 16, "Discovery": all 28 frames of `native_discovery_20261009_092459`, `_092633`, `_092833` and `_092959`; `fog_tables.py trees`): slot 17 of the tree draws holds a **1D** texture, tf4 itself (the same address and content hash as the frame's tf4, texel difference 0), and slot 16 holds tf5 itself. A 1D texture has no second coordinate, so `tf17(a, t)` = `tf4(a)` for every `t`: the trees have **no blend from `c68`**. Ruling 4's premise is false; the controller's ruling B (frame map, same place) takes the trees' own form.
6. *The sky's coordinate is not exactly the lat-long of the direction*: the shader's literals for 1 / (2 pi) and 2 / pi are 0.15915963 and 0.63663852 (the exact values are 0.15915494 and 0.63661977), so `u` runs from -0.0000147 to 1.0000147 and `v` from -1.0000295 to 1.0000295 ("The sky").

### The addressing of a 1D table

`table_1d(texels)` is the fetch every formula here reads its 1D tables with: texel `i` of `N` has its centre at `u = (i + 0.5) / N`; between two centres the result is their linear blend; `u <= 0.5 / N` reads texel 0 alone and `u >= 1 - 0.5 / N` the last one alone (clamp to edge); a coordinate that is not a number reads texel 0.

This is the emulated game's addressing, read from the SDK's translator (`thirdparty/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp`; read only):

- A 1D texture is bound as a 2D array (lines 455 to 459: "1D and 2D textures (including stacked ones) are treated as 2D arrays for binding and coordinate simplicity"), and the fetch's coordinate is padded with zeros for the second axis and the layer (line 1150: "Pad to 2D array coordinates").
- With normalised coordinates (the fog fetches have no `UnnormalizedTextureCoords`) the shader's coordinate goes to the host's `sample` unchanged but for the translator's own offset (from line 1087): `rounding_offset = 1.5f / 1024.0f` of a texel is added to every fetch (lines 698 to 716), divided by the texture's width. For tf5 that is 0.0015 of a texel, 0.011 units of distance; for tf4 a cosine step of 0.00002. It is an emulator measure and is **not modelled** in `table_1d`.
- The filter and the address mode are the sampler's, made from the fetch constant: linear and clamp to edge for tf4 and tf5 in every captured row (research section 1). The host sampler is Direct3D 12's, whose texel centres lie at `(i + 0.5) / N`.

So the convention is the host's, not something the translator computes; that the console's own filter has its centres in the same place is taken from the translator passing the coordinate through (inference). In distance: with `c67.w` = 0.0005 the 256 texels of tf5 span 2,000 units, texel `i` is centred `(i + 0.5) * 7.8125` units past the fog start, the first 3.9 units past the start read texel 0 alone, and everything beyond 1,996 units reads texel 255. tf4's 64 texels are steps of 1 / 64 in the cosine of the angle to `c66`; every view direction more than 90 degrees from the axis reads texel 0 (within half a texel: a cosine below 0.0078).

`tests/test_fog_formulas.py`, `Table1D`, holds each of these; `SyntheticDump.test_a_wrong_formula_fails` and `RealDumps.test_a_wrong_formula_fails_on_the_real_shaders` show that a transmittance table read half a texel off fails the check on the real shaders. That shows the check compares the coordinates the fetches are made at; it does **not** test where a table's texel centres lie: the interpreter hands the fetch's coordinate to the same `table_1d` the formula reads through, so any centre convention would pass. The convention rests on the translator's source cited above, on the host sampler and on `Table1D`'s hand cases. Against the game it is not tested yet either: the replay of the fog and sky functions on the read-back scene colour (frame-map section 16, "Discovery"; `fog_tables.py replay` on `native_discovery_20261009_092959.jsonl`) cannot tell half a texel, because at the bridge every sky pixel and every fogged ground pixel looks away from the axis `c66` (tf4's coordinate is 0 in 10,135 of 10,135 sampled sky pixels and 14,633 of 14,633 fogged ground pixels of frame 1, where the clamp reads texel 0 under any convention), and for tf5 half a texel is 3.9 units of distance, less than the unknown colour before fog allows to be seen. A view towards the sun, or the native target against the oracle (plan Task 4), is what can test it.

### The fog block

`fog(C, P, eye, c28, c66, c67, c68, tf4, tf5, scatter_scale=1)`, with `fog_coords(P, eye, c66, c67)` for the three coordinates:

```
V = eye - P;  d = |V|;  dD = d - c67.x
dD > 0 false:  C unchanged, no table read
a = sat((-V / d) . c66.xyz);  S = tf4(a).rgb
s = c67.w * dD;               T = tf5(s).rgb
t = sat(c67.z * dD);          F = c68.rgb + t * (S - c68.rgb)
G = C * T + (1 - T) * c28.y * scatter_scale * F
C' = C + c28.x * (G - C)
```

`C` is the family's colour (the functions above: linear, before the output scale), `P` the pixel's world position, `eye` pixel `c19.xyz`. `c67.y` is not read. Alpha is not touched. After the block the shader multiplies by `c20.w`, and by `c26.xyz` where it reads `c26`.

Instruction numbers in the far terrain shader `7DDA...` (every shader of the list has the same steps; the check, not this table, is what holds them): V (8), `d^2` (11), `1 / d` and d (14 and 13, scalar), the view direction (15), `dD` (15, scalar), the predicate (23, scalar; the jump at 2.1), a and s (25), the two fetches (26, 27), `1 - T` (28), `S - c68` and t (29), F (30), `(1 - T) * c28.y` (31), `G - C` (32, 33), `C'` (34), `* c20.w` (35).

**The blended form** (10 shaders, `"alpha"` in `FOG_SHADERS`): the in-scatter term is multiplied by the pixel's alpha, `scatter_scale` = the output alpha divided by `c26.w`. In `401A...`: the two fetches (217, 218), `S - c68` (219), `1 - T` and `c28.y * alpha` (220), their product and t (221), F (222), `G - C` (223, 224), `C'` (225), `* c20.w * c26.xyz` and `alpha * c26.w` (226, 227). These shaders are drawn blended one / inverse source alpha (research section 1); the native pass does not light them.

**In the native pass** (frame map section 16, "Native fog", "Unlit draws and the reviews' fixes" and "After the reviews: the fix wave"). A lit draw of the ground form is `fog(C, ...)` with `scatter_scale` = 1 on the family's lit colour, before the exposure that stands in for `c20.w`; a lit draw whose own pixel shader is not in `FOG_SHADERS` and is no tree's (`26052DEF...`, a plant form) is not fogged, as in the game. A draw the pass does not light whose pixel shader is in `FOG_SHADERS` is `fog(U, ..., scatter_scale = exposure)` on its unlit colour `U` (`UnlitFogMirror`; pins by `fog_pins.py`): the unlit value carries no exposure, so the in-scatter term takes it, and the result has the in-scatter a lit pixel at the same place has. For the ten shaders of the blended form `scatter_scale` = exposure * alpha with the native draw's own alpha (the material's: the texture's alpha times the draw's scale constant), which is not the game's output alpha before `c26.w`: an approximation; a flat draw has no alpha of its own and takes 1. The shader bounds the fogged unlit value like a lit one (not negative, at most 64); the mirror does not. An unlit trunk or leaf is `fog_apply` of the trees' form, `U * T + (1 - T) * S * (c8.x * exposure)` (`UnlitTreeFogMirror`), when its light inputs carry the scale. `U` is a placeholder (flat clay, or the texture with a facet shade), not the game's colour: only the haze over it is the game's. The lake's far surface `2B09...` (`FOG_NOT_OF_THE_FORM`) is not drawn by the pass.

**The cross-fade of `2F8A...`** multiplies after the block: `out = C' * sat(d^2 * c31.x + c31.y) * c20.w` (the fade at 12, the products at 36 and 37). The check gives it a `c31` that puts the fade strictly between 0 and 1 in 103 of its 149 fog trials (the research's trials had it at 1 throughout); `RealDumps.test_the_cross_fade_of_2f8a_is_exercised` holds that such trials exist.

**Guards** (review focus 2; `Fog` in the tests). `fog` never raises on numbers. A fog input that is not finite (the position, the eye, `c28.xy`, `c66`, `c67.x`, `.z`, `.w`, `c68`, the alpha of the blended form, a texel either fetch returns) or a result that is not finite leaves the pixel **unfogged**: the colour comes back as it went in, which is how the product draws a frame without a valid fog block. A colour channel that is not finite counts as 0, as in the families. A zero `c66` reads tf4 at 0. A pixel at the eye is inside the start for any `c67.x >= 0`; with a negative start it has no view direction and the angle reads 0. `c67.w * dD` far beyond 1 reads the last texel of tf5. The microcode has none of these guards.

### The fogged pixel shaders

`FOG_SHADERS` in `light_formulas.py`: 40 entries of (hash, family, world, form), for the capture's fog flag (plan Task 3). They are the dumps' pixel shaders that fetch tf4 and tf5 with `tfetch1D` and read `c67` (`fog_check.scan`: 41 of 114), less the lake `2B09...` (`FOG_NOT_OF_THE_FORM`); a test and the command line compare the list with the dumps.

| World position | Form | Shaders | Family (`light_check.FAMILIES`) |
|---|---|---|---|
| interpolator 1 | plain | `7DDA...`, `2F8A...` | `terrain-far` |
| interpolator 1 | plain | `014F...` (the fur) | none |
| interpolator 4 | plain | `9C21...`; `4524...`, `04F2...` | `terrain-layer`; `terrain-layer-multi` (the `C30A...` layers) |
| interpolator 4 | plain | `E099...` | none |
| interpolator 2 | plain | `4755...`; `AF1E...`, `5783...` | `terrain-layer`; `terrain-layer-multi` (the `FB68...` layers) |
| interpolator 2 | plain | `8D90...`, `2FD2...`; `648B...`, `7892...` | `object`; `object-multi` |
| interpolator 2 | plain | `A7E4...`; `E99F...`; `165E...`; `F525...` | `plant`; `plant-multi`; `plant-lit`; `plant-lit-multi` |
| interpolator 2 | plain | `0981...`, `09CB...`, `1421...`, `31AE...`, `8C51...`, `A229...`, `C071...`, `F96A...`, `F984...`, `FC09...` | none |
| interpolator 0 | plain | `A17D...`, `F6D9...` (water) | none |
| interpolator 2 | alpha | `2005...`, `3275...`, `401A...`, `44B5...`, `827B...`, `90B3...`, `95ED...`, `A7A8...`, `C010...`, `F2F1...` | none |

Interpolator N is register rN of the pixel shader. The family is the pixel shader's in `light_check.FAMILIES`; a test holds the two together. The tree pixel shaders (`00E0...`, `58D3...`, `2FBE...`, `7CD5...`, and the billboards' `4B1D...`) are not in the list: they read no fog register and no fog table, so a draw of theirs is no place to read the frame's fog block from. `FOG_VERTEX_SHADERS` has the six vertex shaders of the trees' form with the interpolators of the world position, the factor and the add.

### The check of the pixel form

`fog_check.py --only pixel` (`check_pixel`): a differential per trial. Run A has the predicate off (`c67.x = 1e9`) and gives the colour before fog times whatever follows the block; run B has fog on and must be `fog` of run A's colour (divided by `c20.w`, by `c26.xyz` where the dump reads `c26`, by the cross-fade for `2F8A...`) times the same scale. Also per fog trial: alpha bit for bit as in run A; the angle and the distance of the interpolator `FOG_SHADERS` names are the two table coordinates the microcode fetched (so the wrong interpolator fails). Per near trial (a quarter of the trials: every interpolator within a few units of the eye and the start beyond): no table fetch and run B bit for bit run A.

The trials are those of the research script (`out/tests/fog-research/fog_check.py`: seed 1234 per shader, the same draws in the same order), which is why its counts come out again. What differs:

- **The tables are tables**: 64 and 256 random texels (tf4 up to 3, tf5 in 0..1), read with `table_1d` by the interpreter's sampler and by the formula. The research drew a random colour per coordinate.
- Every other texture is `light_check.tex` (the same in every process; the research's was salted per process).
- **The literal registers are all 1.** The research drew 1 or 0.875 for all of them, to show that the 1 of `1 - T` is a literal; the formula's 1 is 1. The 38 shaders' own literal tables are not used, which the differential does not need: whatever the rest of the shader computes with wrong literals is the same in both runs.
- Half the trials get `c67.z` between 0.001 and 0.02 (finding 3).
- `2F8A...` gets a `c31` that puts its cross-fade between 0.2 and 1.

**The water shaders** `A17D...` and `F6D9...` (80 instructions each; they differ in instruction 14 alone) compare literals in their kill (16) and in their outer predicate (18: `setp_ne_push` with `c254.x` as the counter), so a common literal value keeps every trial out of the block. They run with their own table: `c254` = (0, 2, 0, 0), `c255` = (0.5, -1, 1, -2), the registers at every one of their 24 executions each in frame 1116 of the GPU log (`00000000 40000000 00000000 00000000`, `3F000000 BF800000 3F800000 C0000000`). `c141` is drawn so that some pixels are killed, some pass the kill and fail the outer predicate, and the rest reach the block. Their fog block is 76 to 87 (`dD` 76, the predicate 77 `setp_gt_push`, a and s 78, the fetches 79 and 80, `1 - T` 81, `S - c68` and t 82, F 83, `(1 - T) * c28.y` 84, `G - C` 85 and 86, `C'` 87, `* c20.w` 88, the output with the alpha of 13 at 89); the world position is interpolator 0 (37). Of 200 trials each (`task2_fog_check.txt`): `A17D...` 69 fog trials agree, 26 near, 94 killed, 11 outside the block; `F6D9...` 62, 20, 106 and 12. A killed trial and a trial kept out of the block are no evidence of anything but themselves (run B equals run A in all 23 of the latter).

**Which literal is the 1.** The register and component each shader subtracts T from, and its value in the game (`out/native-evidence/fog/task2_fog_one_literal.txt`, made by `task2_fog_one_literal.py` from the dumps and four GPU logs: `gpu_shadow_bridge2.log`, `gpu_shadow_trees.log`, `gpu_shadow_third.log`, `gpu_shadow_bridge.log`, 136 frames): located in all 40 dumps (`c254` or `c255`, any component); **24 of the 40 are drawn in those logs and the component reads 1.0 at every one of their executions**. The other 16 are not drawn there (`0981...`, `2005...`, `31AE...`, `3275...`, `827B...`, `8C51...`, `90B3...`, `95ED...`, `A229...`, `A7A8...`, `C010...`, `C071...`, `E099...`, `F2F1...`, `F96A...`, `F984...`): for them the 1 is the reading's.

Mutations (`tests/test_fog_formulas.py`): the two amounts exchanged, the axis reversed, the start not subtracted, no near blend, the two tables exchanged, the transmittance table half a texel off, the alpha ignored, the wrong interpolator, `c26` left out of the scale, a block that fogs near pixels, and the plain form taken for the blended one and the reverse: each fails.

### The trees' vertex form

`vertex_fog(P, eye, c8x, c110, c111, tf16, tf17)` gives the two interpolators, `fog_apply(C, factor, add)` is what the pixel shader does with them:

```
(a, s, t) = fog_coords(P, eye, c110, c111)        (vertex registers; eye = vertex c9.xyz)
dD > 0 false:  factor = (1, 1, 1), add = (0, 0, 0), no table read
T = tf16(s).rgb;  S2 = tf17(a, t).rgb             (tf17: a 2D texture)
factor = T;  add = (1 - T) * S2 * c8.x
pixel, under bool b132:  colour = colour * factor + add          (then * c20.w * c26)
```

`P` is the world position the vertex shader exports, after the wind. Instruction numbers: `475E...` the vector to the eye and `d^2` (81, 82), d (83), `dD` (85), the predicate (86), t and `1 / d` (87), the view direction (88), a and s (89), the fetches (90, 91), `S2 * c8.x` (92), `1 - T` (93), add (94), the near values (95, 96), exported as `o6`, `o7` with the world position in `o2` (101, 105, 106). `A584...`: `dD` and the predicate (136, 137), s, a and t (138, 139), the fetches (140, 141), `S2 * c8.x` (142), `1 - T` (143), add (144), the near values (145, 146), exported as `o4`, `o5` with the world position in `o0` (149, 151, 152). `D7DD...` (143, 144, 147 to 155) and `2D40...` (161, 168 to 176) have the block with the predicate inverted.

The check (`--only vertex`, `check_vertex`): every vertex register random, the literal tables those of `light_check.py` (captures), tf16 a random 256-texel table, tf17 a random smooth function of its two coordinates; the two exported interpolators and the coordinates of both fetches against the formula evaluated at the exported world position. 147 + 143 + 143 + 143 fog trials and 53 + 57 + 57 + 57 near trials agree (200 each). A formula without `c8.x` and one with tf17's coordinates exchanged fail.

The pixel side (`--only apply`, `check_apply`): `light_check`'s case of each tree pair with `b132` set and a random factor and add in the two interpolators against `fog_apply` of the family's formula: 200 of 200 for each of the six pairs (`00E0...` 16 and 44, `7CD5...` 10 and 25 are the bool and the product; `c49` is on the colour, not on the add). The earlier check of these families ran `b132` only with factor 1 and add 0.

**What ruling 4 rests on.** `vertex_fog` followed by `fog_apply` equals `fog` at the vertex if, and only as far as, tf16 is tf5, `tf17(a, t)` is `c68 + t * (tf4(a) - c68)`, vertex `c8.x` is pixel `c28.y`, vertex `c110`, `c111`, `c9` are pixel `c66`, `c67`, `c19`, and `c28.x` is 1 (`VertexFog.test_it_is_the_pixel_form_if_the_tables_are_what_ruling_4_takes_them_for`: 300 random cases to 1e-12). The registers agree in the research's rows. The tables (findings 4 and 5, settled by the captures): tf16 is tf5, tf17 is tf4 without the blend from `c68`, so the vertex form is `T = tf5(s)`, `add = (1 - T) * tf4(a) * c8.x`: the pixel form with `t` = 1 and `c28.x` = 1. With `c67.z` = 1 the two differ only within one unit past the fog start, where tf5 reads its first texel, (1, 1, 1) in every captured frame, so both give the colour unchanged there. What stays a difference in any case is the interpolation of factor and add across a triangle against the per-pixel evaluation.

Read only, by `FOG_VERTEX_SHADERS`: `D57E...` (a leaf shader whose literal table is in no file here; its block, 209 to 217, is the leaves' line for line) and the distant-tree billboards `129F...` (75 to 85), whose block has **no `c8.x`** and takes the eye from vertex `c47`, not `c9`; their pixel shader `4B1D...` applies the pair without a bool (58).

### The sky

`sky(n, block, tf4, tf13, tf14)` is pixel shader `F760...` (instructions 10 to 58 and 100; 91 in all), linear and before the output scale `c20.w`; alpha is 1:

```
n      = the interpolator, normalised                                          (10 to 12)
a      = sat(n . c66.xyz);  S = tf4(a).rgb                                     (14, 15, 40)
(u, v) = sky_latlong(n)                                                        (13, 14 to 39)
s13    = tf13(u, v);  s14 = tf14(u, v)                                         (41, 42)
col    = s13.rgb^2 + c47.x * (s14.rgb^2 - s13.rgb^2)                           (44, 45, 47)
alpha  = s13.a + c47.x * (s14.a - s13.a)                                       (43, 46)
k      = |n.z|^0.2;  h = c67.y * (1.05 - k)                                    (20 to 22, 24, 28)
l1     = h + (c31.x - h) * k;  l2 = h + (c46.x - h) * k                        (31, 33, 48)
E      = exp2(-1.4426950 * (l1 * c64.rgb + l2 * c65.rgb))                      (49 to 54)
base   = (1 - E) * S                                                           (55, 56)
result = base + alpha * (col - base)                                           (57, 58)
oC0    = (result * c20.w, 1)                                                   (100)
```

`sky_latlong`, the shader's own arithmetic:

```
q  = min(|x|, |y|) * (1 / max(|x|, |y|))                                       (14, 16, 17, 19)
at = q * (0.9998660 - 0.3302995 q^2 + 0.1801410 q^4 - 0.0851330 q^6 + 0.0208351 q^8)     (20 to 25, 27 to 29)
|y| > |x|:  at = pi/2 - at                                                     (17, 30, 31, 34)
x < 0:      at = at - pi                                                       (26, 35)
min(x, y) < 0 <= max(x, y):  at = -at                                          (15, 18, 19, 26, 29, 36, 37)
ac = sqrt(1 - |z|) * (1.5707288 - 0.2121144 |z| + 0.0742610 |z|^2 - 0.0187293 |z|^3)     (13, 16, 18, 25 to 31)
z < 0:      ac = pi - ac                                                       (21, 26, 32 to 34)
u  = 0.5 + at * 0.15915963;   v = (pi/2 - ac) * 0.63663852                     (36, 38, 39)
```

So `u = 0.5 + atan2(n.y, n.x) / (2 pi)` and `v = asin(n.z) * 2 / pi` to within 1.6e-5 and 4.3e-5 (20,000 random directions; the difference is the two polynomials' error and the two rounded literals). The upper hemisphere is `v` in 0..1 with the zenith at 1.

**The literals** `c250..c255`, the emulated GPU's registers at each of the shader's six executions in frame 1116 of the log (two in the main scene, four in the reflection passes); `SKY_LITERALS` has them as the exact singles:

| Register | Hex | Value |
|---|---|---|
| `c250` | `BC996E30 3E3876E2 3F000000 3F866666` | (-0.0187293, 0.1801410, 0.5, 1.05) |
| `c251` | `C0490FDB BDAE5A36 40490FDB C0000000` | (-pi, -0.0851330, pi, -2) |
| `c252` | `3FC90FDB 3FC90DA4 BE593484 3F7FF738` | (pi / 2, 1.5707288, -0.2121144, 0.9998660) |
| `c253` | `3F800000 00000000 3D981627 BEA91D04` | (1, 0, 0.0742610, -0.3302995) |
| `c254` | `3E4CCCCD 3CAAAE5F 3F22FABE 3E22FABE` | (0.2, 0.0208351, 0.63663852, 0.15915963) |
| `c255` | `3FB8AA3B 00000000 00000000 00000000` | (1.4426950, 0, 0, 0) |

**The seam.** `u` jumps by one whole turn across the half plane `y = 0`, `x < 0`. On the half plane itself and just north of it (`y >= 0`) `u` is 1.0000147; just south it is -0.0000147: the test "of different sign" counts `y = 0` as positive (18: `sge` of `max(x, y)`; 26: `sgt` on `min(x, y)`). What a sampler makes of the two ends is the fetch constant's (wrap or clamp), which no capture row holds yet. The two fetches (41, 42) carry no attribute, so the level of detail is the computed one: where the textures have mips, the pixels on the seam see a gradient of a whole texture width, and a renderer that samples with screen-space derivatives of `u` gets its smallest mip along that line. This is the game's own behaviour on the console as far as the microcode says; whether the game's textures have mips is for the capture.

**The poles.** At `x = y = 0` the ratio `q` is 0 times `1 / 0` (17: `rcp` of `max(|x|, |y|)`; 19: the product). In plain arithmetic that is not a number; by the console's rule as the SDK has it, a product with a zero factor is +0 (`interpreter.cpp`: "Direct3D 9 behavior (0 or denormal * anything = +0)"; `dxbc_translator_alu.cpp`: "Shader Model 3: +-0 or denormal * anything = +0"), so `q` = 0 and `u` = 0.5 there; `v` = 1.0000295 at the zenith and -1.0000295 at the nadir. `sky_latlong` takes the rule; the check runs the microcode with the interpreter's `zero_product` switch, and `RealDumps.test_at_the_poles_the_microcode_needs_the_zero_product` holds that without it exactly the two pole trials fail. A direction beside the pole (1e-6 off) needs no rule and agrees. At the pole itself one texel row is read whatever `u` is.

**The octant boundaries.** Where `|x| = |y|` the shader changes from `atan(q)` to `pi / 2 - atan(q)`; its polynomial exceeds `pi / 4` by 1.14e-5 at `q = 1`, so `u` jumps by 3.6e-6 there (0.004 texel of a 1,024-texel texture).

**Below the horizon** the paths are the mirror image (`|n.z|`), `v` is negative, and at the horizon `k` = 0: both paths are `1.05 * c67.y` (1,995 with the captured 1,900) whatever `c31.x` and `c46.x` are; at the zenith they are `c31.x` and `c46.x` themselves. The log of 0 at the horizon (20) is minus infinity, times 0.2 (21), and `exp2` of it is 0 (22): no rule is needed there.

**Guards.** `sky` never raises on numbers and is finite for any input: a register, a direction or a texel that is not finite counts as 0 (there is no unfogged colour to fall back to). `tf13 = None` gives the clear-sky term `(1 - E) * S` alone, `tf14 = None` the first texture alone (as with `c47.x` = 0): the two fallbacks of spec design 5.

**The check** (`--only sky`, `check_sky`): every register random within what the one logged frame shows (`c31.x`, `c46.x` 50 to 300; `c47.x` 0, 1 or between; `c64` up to 0.002, `c65` up to 0.01; `c67.y` 500 to 3,000; `c20.w` 16, 1 or 2.5), the literals as above, tf4 a random 64-texel table read with `table_1d`, tf13 and tf14 random smooth functions with four channels, bool `b129` clear, the interpolator the direction times a random length. Compared: the colour to 1e-9 relative, alpha = 1, and the three fetch coordinates to 1e-9. 18 named directions (zenith, nadir, beside each, the horizon, five on and beside the seam, three axes, the four octant boundaries, one below the horizon) and 200 random ones: 218 of 218, largest relative difference 8.2e-14. Six wrong forms fail: exact `atan2` / `asin` in the place of the polynomials, the two path constants exchanged, the textures not squared, the second texture ignored, the clear-sky term alone, the axis reversed.

For the port (plan Task 5): a shader that computes `u` and `v` with its own `atan2` and `asin` is within 1.6e-5 and 4.3e-5 of the game's coordinate, not equal to it; the mirror on the CPU can be held to `sky_latlong` only if it uses the polynomials.

### The interpreter

`ucode_eval.py` gained what the water shaders and the sky's poles need (`tests/test_fog_formulas.py`, `InterpreterForTheWaterAndTheSky`: hand-computed):

- **`setp_ne_push`**, **`setp_gt_push`** (vector opcodes; `ucode.h`, `kSetpNePush`, `kSetpGtPush`; the SDK's `interpreter.cpp` 484 to 499): p0 = (`a.w == 0` and `b.w != 0` / `b.w > 0`); the result, in every component, is 0 where (`a.x == 0` and `b.x != 0` / `b.x > 0`), else `a.x + 1`: a counter of nested conditions.
- **`kill_gt`**: the pixel is discarded where any component of the first operand is greater than the second's; the result is 1 there, else 0. The interpreter sets `killed` and goes on.
- **`zero_product=True`**: a product with a zero factor is +0 in `mul`, `mad`, the dot products, `muls`, `mulsc` and `muls_prev`. Off by default, so every earlier check runs as before (`light_check.py --all` and the other suites unchanged).

`setp_eq_push`, `setp_ge_push` and the other three kills are still not interpreted: none of the shaders here has them.

### Not checked

Named by shader and instruction, so that nothing above is read as wider than it is:

1. **The ground-fog volume under bool `b129`**: `F760...` 59 to 99 (and the same block in the tree pixel shaders, "Tree trunks" above). The bool is clear in every trial, as it is in every draw of the GPU log (research section 1).
2. **The lake `2B09...`**, all 149 instructions. It fetches tf4 with the predicate off, so the differential does not apply (`RealDumps.test_the_lake_is_not_of_the_form`).
3. **The sky's vertex shader `8262...`** (3 to 11): that the interpolator is the cube's offset before the eye is added is the research's reading. `sky` takes a direction; nothing here says where the pass gets it.
4. **`D57E...`** 209 to 217, **`129F...`** 75 to 85 and **`4B1D...`** 58: the trees' form by the reading only.
5. **The water shaders outside their fog block** (10 to 75): the check takes whatever they compute as the colour before fog. Their kill (16) and outer predicate (18) are run, not described by a formula.
6. **The literals of the 38 other pixel shaders**: not theirs in the trials. For the 16 named under "Which literal is the 1" nothing but the reading says the block's literal is 1.
7. **The cube map under `b134`** of the object and blended shaders is never set (the interpreter has no cube fetch); those instructions lie before the fog block.
8. **Single precision, denormals** and the translator's 1.5 / 1024 texel offset: not modelled; the arithmetic is double precision.
9. **The zero product at the poles** is the emulator's rule from its source, not something measured on a console.
10. **The tables' content and decode** (endianness, the half floats of format 29, tf5's swizzle), tf16 and tf17 against tf5 and tf4, the sky textures' format and address mode: answered by the discovery captures (frame-map section 16, "Discovery"). Still open: where a table's texel centres lie against the game (above), and every register's value in a scene other than the bridge and the third spot by day.

### Evidence

In `out/native-evidence/fog/` (git-ignored): `task2_fog_check.txt` (`fog_check.py --trials 200`: the 40 pixel shaders, the four vertex shaders, the six tree pairs, the sky; exit code 0), `task2_fog_one_literal.txt` with its script `task2_fog_one_literal.py`. To rerun: `python tools/xdk_sigmatch/fog_check.py`, `python -m unittest discover -s tests -p "test_fog_formulas.py"`.

## The post chain

Native renderer sub-project 10 (exposure, composite, bloom, sun layer), plan Task 3, 2026-10-09. The eight pixel shaders of the game's post chain as closed formulas, each checked against its microcode; the replay of the chain on the CPU against the game's own final picture; the numbers the C++ mirrors are pinned to. Nothing here was run in the game when this part was written; what the composite and the ramp do in the game since (phase 10a: the native picture against the replay's prediction at the bridge and at a third spot, and the two shaders on stored scenes of the game's on the GPU) is in `frame-map.md` section 17, "Native composite", "Oracle feed" and "Validation (10a)". Every statement rests on the shader dumps, on the four read-back captures of the research (`out/build/win-amd64-release/logs/native_discovery_20261009_092959.jsonl`, `_121331`, `_132250` at the bridge, `_134312` towards the sun; 4 frames each, made with `--readback_resolve=full`), on the SDK's translator source where it is cited, and on the research note `.superpowers/sdd/post-research.md`, whose reading this part checks and whose section 1 has the pass list.

### Result

| What | Shader | Function of `light_formulas.py` | State |
|---|---|---|---|
| Reduce (2 x 2) | `0A96B3FD5121E331` | `post_reduce` | closed: 2,000 of 2,000 values |
| Downsample (4 x 4) | `87E85199E1E9B260` | `post_downsample` | closed: 2,000 of 2,000; which four texels: inference ("What stands on inference") |
| Luminance | `5821366EE8FDE893` | `post_lum` | closed: 2,000 of 2,000 |
| Bright pass | `EE922CFB4426EA83` | `post_bright` | closed: 2,000 of 2,000 |
| Blur along x, along y | `EA83C7E7E24EC4C8`, `28DBE18F313350BF` | `post_blur13` | closed: 2,000 of 2,000 each |
| Radial blur | `4BE4344C27016E04` | `post_radial` (`post_radial_taps`) | closed: 2,000 of 2,000, the 14 fetch coordinates in 500 of 500 trials |
| Composite | `37B186F4BD3288F7` | `post_composite` (`post_table_texel`) | closed: 2,000 of 2,000, the table's coordinate and its texel in 500 of 500 trials |
| The scan-out ramp | none (the SDK's swap blit) | `post_ramp`, `post_ramp_index` | by the SDK's source; hand cases |
| An unlit draw's scale | none (spec ruling 9) | `post_unlit_scale` | a rule of the design; hand cases |

(`out/native-evidence/post/task3_post_check.txt`: `post_check.py`, 500 trials a shader, 4 values a trial, tolerance 1e-9 relative, exit code 0; the largest relative difference is 1.1e-15, the composite's. `POST_SHADERS` has the eight hashes with their roles.)

What differs from the research note, the spec and the plan, found by this reading:

1. **The table is read at the texel nearest `u * 512`, not at `floor(u * 512)`.** The composite's fetch is `tfetch1D r0.x___, r1.w, tf1, MagFilter=point, MinFilter=point, OffsetX=0.5` (instruction 7): by the interpreter's rule for a point fetch and by the SDK's translator the half texel is added before the texel is taken ("The addressing of the table"). The research's replay, spec Design 6 ("below scene luminance 1 / 128 reads the table's texel 0") and plan Task 5's sample index `min(511, uint(512 * saturate(c46.x * L)))` have the floor. With the captured `c46.x` = 0.25 texel `k` serves scene luminance `(k -+ 0.5) / 128`, and texel 0 (0 in every captured table: black) the luminances below 1 / 256. Against the game's front buffer the nearest texel is the better reading in the darks: over the pixels in the table's texels 0 to 3 the replay's median difference falls from 1.36 to 0.32 levels towards the sun and from 1.19 to 0.85 at the bridge ("The replay").
2. *The research's check could not have failed on the table's addressing*: both sides read a smooth function of the coordinate. The tracked check reads a table of 512 random texels through `ucode_eval.point_texel` on the microcode's side and through `post_table_texel` on the formula's.
3. *The replay's figures move a little with the addressing* (below): variant A's whole-picture median is 3.14, 3.05, 3.13 levels where the research has 3.18, 3.10, 3.17. With the research's floor (`--table-offset 0`) the tracked tool gives the research's figures to the last printed digit.

### The formulas

With `lum(c) = 0.2125 r + 0.7154 g + 0.0721 b` (`POST_LUM`: the three shaders that need it hold (0.0721, 0.2125, 0.7154) in literal `c255` and compute `dot(c.zxy, c255.xyz)`):

```
0A96 reduce       out = t(uv + (1, 1) texels), linear                                      (2, 3)
                  target half the source: pixel (x, y) reads the source at (2 x + 1, 2 y + 1), the mean of a 2 x 2 block
87E8 downsample   P = 4 * |pixel position|          (param_gen; unnormalised coordinates)  (2)
                  out = 0.25 * (t(P + (2.5, 2.5)) + t(P + (2.5, 0.5)) + t(P + (0.5, 0.5)) + t(P + (0.5, 2.5))), linear   (3 to 10)
5821 luminance    out.rgba = lum(t(uv + (0.5, 0.5) texels))                                (2, 3)
EE92 bright pass  d = tf0, a1 = tf1, a2 = tf2, each at uv + (0.5, 0.5) texels              (2 to 4)
                  L = lum(d)                                                               (5)
                  out.rgb = (d * sat(L * c77.y - c77.x) / L * c31.x + a1 * 6 * c46.x + a2) / c47.x;  alpha 1   (6 to 12)
EA83, 28DB blurs  out = sum over k = -6..6 of w[|k|] * t(uv + (0.5 + k, 0.5) texels)       (4 to 16, 17 to 29)
                  (28DB: (0.5, 0.5 + k)); point; w = (c46.x, c46.y, c46.z, c46.w, c47.x, c47.y, c47.z)
4BE4 radial blur  s = 1 + (4/7) * (c47.x - 1);  g = 1 + (4/7) * (c46.x - 1)                (6 to 9)
                  p_k = c31.xy + (uv - c31.xy) * s^k,  k = 0..13                           (7, 9 to 25, ...)
                  out = (4/7) * sum of 0.5 * g^k * t(p_k + (0.5, 0.5) texels), point       (26 to 66)
37B1 composite    scene = tf0.rgb, bloom = tf2.rgb, each at uv + (0.5, 0.5) texels         (3, 6)
                  L = lum(scene)                                                           (4)
                  e = c77.y * T(c46.x * L)          (tf1: 1D, 512 texels, point)           (5, 7, 8)
                  c = sat(e * (L + sat(c47.x * L) * (scene - L)))                          (9 to 11)
                  b = sat(bloom * c31.x);  s = c + b * (1 - c)                             (8, 12, 13)
                  t = s + c77.x * (s - lum(s))                                             (14 to 16)
                  out.rgb = |t| ^ c78.x  (exp2(c78.x * log2 |t|));  alpha = the blue value (17 to 23)
```

The literals (`c255`; capture `092959` frame 1, the `"ps_lit"` of each shader's first row, read for this part): `87E8` (0.25, 4, 0, 0); `5821` and `37B1` (0.0721, 0.2125, 0.7154, 0); `EE92` the same with w = 6; `4BE4` (0.5, 1, 4/7, 0). The filter of a fetch that names none in the dump (`0A96`'s and `87E8`'s say linear, the blurs' and the table's say point) is the sampler state's: the scene and the bloom in the composite are linear by their fetch constants (research section 1).

What the terms do with the captured registers is in the research (section 2): the table's coordinate is a quarter of the scene's luminance, so the table covers luminance 0 to 4; `sat(50 L)` fades the colour to grey only below `L` = 0.02; `c77.x` = -0.0052 is half a percent of desaturation; `c78.x` = 1 / 2.2; the bright pass with `c77.y` = 1 / `c20.w` keeps the part of the pre-scale luminance above 0.087.

**How the functions take their inputs.** A texture is a callable: `tex(px, py)` of unnormalised coordinates in texels, texel `i`'s centre at `i + 0.5` (`post_texture(rows, linear)` makes one, clamped to the edge); the radial blur's `tex(u, v)` takes the normalised coordinate its taps are computed in. The functions take the pixel's integer position and add the fetch's half texel themselves: `post_blur13(tex, x, y, weights7, vertical)` reads the texel centres `(x + 0.5 + k, y + 0.5)`, `post_reduce(tex, x, y)` reads `(2 x + 1, 2 y + 1)`, `post_downsample(tex, x, y, bias)` reads around `4 * (|x| + bias)`. `post_composite(scene, bloom, table, c31x, c46x, c47x, c77, c78x)` and `post_bright(d, tf1, tf2, c31x, c46x, c47x, c77)` take the texels at the pixel; `table` is the frame's 512 floats.

### The addressing of the table

`post_table_texel(u, n=512)` = `min(n - 1, max(0, floor(u * n + 0.5)))`; a coordinate that is no number, or not above 0, reads texel 0.

- The fetch carries its own offset of half a texel (`OffsetX=0.5`, as nearly every fetch of these shaders does), and it is a point fetch. The interpreter's rule for a point fetch is `floor(coordinate * size + offset)` (`ucode_eval.point_texel`, from the SDK's translator: `dxbc_translator_fetch.cpp`, "Get offsets applied to the coordinates before sampling": the offset in texels, divided by the size, is added to the normalised coordinate, and the host's point sampler takes the texel the result lies in). The translator's own 1.5 / 1024 of a texel on top is not modelled.
- The address mode is clamp to edge: the clamp field of the table's fetch constant (word 0 of `04000956 1FAA00A4 ...` in the composite's row, bits 10 to 12, is 2; `fog_tables.describe_fc` prints it; research section 3). Both sides of the check clamp by their own code, so the check does not hold it; the constant does: a coordinate from `(n - 0.5) / n` on reads the last texel.
- Pinned (`tests/test_post_formulas.py`, `TableTexel`; `post_pins.py`): `u = k / 512 - 1e-6`, `k / 512` and `k / 512 + 1e-6` all read texel `k` for k = 0, 1, 2, 12, 128, 511, and texel 511 for k = 512 (the plan's points: under this convention they lie in the middle of a texel, so they do not tell the two readings apart); the boundaries are at `(k + 0.5) / 512`, pinned on both sides for k = 0, 1, 2, 12, 128, 510 (the pins: 0, 1, 12, 127, 128, 510, 511); 2,000 coordinates from -0.05 to 1.05 against `point_texel`.
- The machine check holds it against the interpreter: the composite's table is 512 random texels on both sides, and a formula that reads `floor(u * 512)` fails (`RealDumps.test_the_composites_table_texel_is_the_microcodes`).
- Against the game (the front buffer of the emulated game, whose shaders are the translator's): `out/native-evidence/post/task3_table_addressing.txt` (the research's numpy replay with the offset as a parameter; the script is beside it). Median absolute difference, in 8-bit levels, of variant B over the pixels whose scene luminance lies in the table's texels 0 to 3: 1.19, 1.17, 1.27 with the floor against 0.85, 0.82, 0.91 with the half texel at the bridge (`092959`, `121331`, `132250`: 4,476, 4,380 and 3,963 pixels), and 1.36 against 0.32 towards the sun (`134312`: 11,772 pixels). In texels 4 to 12: 0.74, 0.73, 0.78 against 0.63, 0.61, 0.65, and 0.56 against 0.48. In texels 13 to 39 the two readings agree to 0.01; the whole-picture figures are under "The replay".

What this does not settle: the console itself. The front buffer is the emulator's picture, made with the translator's shaders, so the comparison above is the emulator against itself: it confirms the reading of the emulator, not of the console. That the console's point fetch with an offset of half a texel reads the nearest texel is **inference** (the emulator's implementation of it; the game's author would hardly have written the offset otherwise).

**The unlit scale** (spec ruling 9) `post_unlit_scale(c77y, table)` = `1 / max(c77.y * T[128], 1 / 64)` reads texel 128, which is scene luminance 1 under either reading (`u` = 0.25). **The ramp**: `post_ramp_index(v)` = `min(255, floor(v * 255 + 0.5))` is the front target's 8-bit level, `post_ramp(v, ramp)` one channel of the guest's 256-entry table at it.

### Guards and clamps

The microcode has neither; the game's render targets clamp. Each function clamps as its target does unless called with `clamp=False` (the machine check's way), and never raises on numbers (`tests/test_post_formulas.py` feeds each a NaN, both infinities, a negative texel and registers at the bounds of `MakePostBlock`):

- A scene texel's channel is read as `post_scene(v)` = `(v > 0) ? min(v, 31.875) : 0` in the composite, the downsample and the bright pass: a NaN and a negative value count as 0, an infinity as 31.875, the largest value of the game's `k_2_10_10_10_FLOAT` target (spec Design 5, ruling 15).
- The composite, the bright pass, the blurs, the radial blur and the reduction return `(v > 0) ? min(v, 1) : 0` per channel: the game's `k_2_10_10_10` targets (the composite's too: its `|t|^c78.x` exceeds 1 where the saturation step overshoots). The 10-bit rounding of those targets is not in the functions; the replay applies it (`post_replay.q10`), the native targets do it themselves (spec ruling 14).
- A texel of a 10-bit target, a bloom value, a table texel or a blur weight that is not finite counts as 0.
- A register that is not finite gives black (the product refuses such a block); so does a bright-pass divisor `c47.x` that is not above 0.
- **`L = 0` in the bright pass**: the microcode computes `sat(..) * (1 / 0)` and multiplies the pixel's zero colour by it (not a number in plain arithmetic, 0 by the console's zero-product rule as the SDK has it: "The poles" above); the reference's scene term is 0 there and the two layers remain.
- No index leaves its array: `post_table_texel` and `post_ramp_index` return 0 for a NaN and the last entry for anything beyond the end.

### The check

`post_check.py` (`check`, `check_text`; `CHECKS` names the trial's body and the formula per hash): each shader in `ucode_eval.Machine` on random inputs, 500 trials, the four components of `oC0` against the formula to 1e-9 relative. The trials are the research script's (`out/tests/post-research/post_check.py`: seed 20261009 per shader, the same draws in the same order), which is why its counts and largest differences come out again but for the composite's, whose table and `c77.x` changed (1.1e-15 where the research has 4.4e-16). Half the composite's trials draw `c77.x` from 1.5 to 4, which drives a channel of `t` below zero: the research's range of -0.3 to 0.3 never did, and a formula with `max(t, 0)` in the place of `|t|` passed the check in all 2,000 values (found by the review of this task; it now fails).

- Textures are smooth analytic functions of (slot, coordinate + the fetch's offset) on a 64 x 64 texel grid, so a wrong coordinate or offset gives a wrong value. The formulas get the same function as an unnormalised texture and the interpolator `u` as the position `u * 64` (`u * 32` for the reduce).
- The radial blur's 14 fetch coordinates are compared with `post_radial_taps` (500 of 500), the composite's table coordinate with `c46.x * lum(scene)` (500 of 500).
- Wrong forms that fail (`RealDumps.test_wrong_formulas_fail`): the composite without the grey fade, with the bloom added instead of screened and without the absolute value; the bright pass without its threshold and with its two layers exchanged; a blur along the other axis and with its weights reversed; a 16-texel box for the downsample; a reduce without its offset; the radial blur's two factors exchanged; a luminance of one channel. On synthetic microcode (`SyntheticDump`): another fetch offset and other weights fail.

Not reached by a trial: the targets' clamps and rounding, single precision, a fetch's filter (the field is continuous), `L = 0`, and whether the downsample's position is integral.

### What stands on inference

1. **The downsample's pixel convention** (discovery question 6). The microcode multiplies the position `param_gen` hands it by 4 and fetches at +0.5 and +2.5. If that position is the pixel's integer index (the SDK's convention for `param_gen`, not checked on data), the four linear fetches are the centres of texels (0, 0), (2, 0), (0, 2), (2, 2) of the pixel's 4 x 4 block: four texels of sixteen, no filtering. If it is the pixel's centre, they are two texels further on in each axis. `post_downsample`'s `bias` (0 or 0.5) is that choice; `post_check.py --bias 0.5` agrees as well as `--bias 0`, because the microcode cannot tell. The replay's variant B fits the game with `bias` 0; the game's own downsample buffer is in no capture (research section 4). Since plan Task 8 (frame map section 17, "Native bloom"): the emulated picture's shader is handed the pixel's integer index (the SDK's translator, `dxbc_translator.cpp`, `StartPixelShader`: `floor(SV_Position.xy)`), and with the bloom sampled at the composite's own fetch (item 4) `bias` 0 fits the front buffer best: median 0.565 to 0.578 levels on the three bridge captures against 0.753 to 0.765 for `bias` 0.5 and 0.608 to 0.620 for a box of the sixteen texels (`out/native-evidence/post/t8_bloom_fetch.txt`). The product draws with `bias` 0 (`kPostDownsampleBias`). That the console's rasteriser hands the same position stays inference.
2. **The interpolators of the full-screen rectangles.** The functions take pixel `i`'s interpolator as `i / N`: with the fetches' half texel that makes every point tap a texel centre and `0A96` the mean of a 2 x 2 block. No row was decoded to confirm the rectangles' coordinates.
3. **The clamps of the game's targets**: 31.875 for the scene (`k_2_10_10_10_FLOAT`, from the GPU log's target format: research section 3), 1 with 10 bits for the bright, blur and layer targets and for the composite's (`k_2_10_10_10`). From the formats, not from data: the small buffers' memory is reused within a frame.
4. **Where the composite samples the bloom.** The replay first sampled it linearly with texel centres aligned (pixel `x` of 1,120 reads the 280-wide bloom at `(x + 0.5) / 4`); under inference 2 the game's fetch lies 0.375 of a bloom texel further on, at `x / 4 + 0.5` (the composite fetches tf2 at the scene's coordinate with the same half-texel offset: instruction 6). Since plan Task 8 the replay (`post_replay.py --bloom-place fetch`, the default, also of variant C and of the stored excerpt), the mirror and the product sample there (controller ruling 27): against the front buffer of the three bridge captures variant B's median is 0.565 to 0.578 levels with the fetch's place and 0.716 to 0.732 with the centres aligned (`t8_bloom_fetch.txt`). `--bloom-place aligned` reproduces the table below, which was made with the centres aligned; with the fetch's place variant B reads, bridge regions / median / within 8: 0.997 to 1.027 / 0.58 / 94.5%, 0.997 to 1.022 / 0.57 / 94.8%, 0.994 to 1.009 / 0.57 / 94.8%, and `134312` 1.13 / 70.7% (`t8_reference_B_<stamp>.txt`). It remains inference 2's reading of the interpolator; the fit is the emulator against itself.
5. **Which of the bright pass's two layers is which** (discovery question 3): `tf1` has the weight `6 * c46.x`, `tf2` 1; both are 0 in the replay.
6. **The console's point fetch** ("The addressing of the table"). The radial blur's 14 point taps stand on the same inference: each reads the texel `floor(coordinate * size + 0.5)` (as `post_pins.py`'s radial pins take it), and the check, whose textures are continuous fields, can tell neither that rounding nor a point tap from a linear one, for the blurs' taps either.

### The replay

`post_replay.py replay <capture> <frame>`: the scene colour the frame's composite row names (`0x199E0000`, by content, untiled with `fog_tables.untile_scene`), the frame's table (`0x1FAA0000`, by content) and registers, and the functions above, pixel for pixel in pure Python, against the front buffer of the same capture (`0x1ECFF000`: the composite's output with the HUD, before scan-out; brought from 1,280 to 1,120 columns). Variant A: `post_composite` with bloom 0. Variant B: with the bloom (`post_downsample`, `post_bright` with both layers 0, `post_blur13` twice, each stage rounded to 10 bits). Variant N: today's native mapping, `sqrt(sat(scene / c20.w * 2.22))`. Frame 1 of each capture (`out/native-evidence/post/task3_replay_<stamp>_f1.txt`; with the research's floor: `..._f1_offset0.txt`), the 26 bridge regions' luminance ratio replay / final and the whole picture outside the HUD in 8-bit levels:

| Capture | A regions | A median / within 8 | B regions | B median / 90% / within 8 |
|---|---|---|---|---|
| `092959` | 0.817 to 1.018 | 3.14 / 56.0% | 0.976 to 1.029 | 0.73 / 4.11 / 94.2% |
| `121331` | 0.818 to 1.022 | 3.05 / 56.2% | 0.974 to 1.022 | 0.72 / 3.93 / 94.6% |
| `132250` | 0.819 to 0.999 | 3.13 / 56.0% | 0.976 to 1.010 | 0.73 / 3.98 / 94.5% |
| `134312` (towards the sun) | - | 4.92 / 48.9% | - | 1.52 / 26.57 / 69.9% |
| the same with `--table-offset 0`: `092959` | 0.815 to 1.017 | 3.18 / 55.9% | 0.975 to 1.029 | 0.75 / 4.13 / 94.2% |
| `121331` | 0.816 to 1.021 | 3.10 / 56.0% | 0.973 to 1.022 | 0.73 / 3.95 / 94.6% |
| `132250` | 0.818 to 0.998 | 3.17 / 55.9% | 0.975 to 1.010 | 0.74 / 4.00 / 94.5% |
| `134312` | - | 4.97 / 48.8% | - | 1.57 / 26.62 / 69.9% |

The last four rows are the research's figures (its section 4 and `out/tests/post-research/replay_<stamp>_f1.txt`), digit for digit; `092959`'s 26 region ratios and hue differences of both variants equal `replay_092959_f1.txt`'s columns as printed. Between the two addressings no region's ratio moves by more than 0.002. Variant B's hue: within 5 degrees on 25 of the 26 regions in each bridge capture (the 25th: 2.1, 4.7, 2.7 degrees), the averted pole side 14.5, 19.4 and 18.1. The bridge regions mean nothing in `134312` (another view) and are not given.

`post_replay.py predict` gives per region the variant after the ramp over variant N, the movement the native shots should show when post is switched on (spec criteria 7 and 13); `out/native-evidence/post/task3_predict_092959_f1.txt` is `092959` with the measured stand-in for the ramp (the eight pairs of spec criterion 4 joined by straight lines: the game's own table is the discovery's to capture): A / N from 0.754 (the averted pole side) to 1.093 (sky), B / N from 0.788 to 1.165. `export` writes the oracle feed's two files (plan Task 6), `compare` reads two pictures in the pass's dump format, `excerpt` wrote `tests/post_replay_excerpt.json` (a 16 x 16 block of `092959` frame 1 at (776, 576): scene, bloom, table, registers, the three variants and the front buffer), on which `tests/test_post_replay.py` holds the arithmetic.

### The pins

`post_pins.py` prints the numbers the C++ mirrors of plan Tasks 5, 8 and 14 are held to: the table's texel for 41 coordinates, the unlit scale, the ramp's index, the composite and the bright pass for two register sets (the excerpt's real ones and a made-up set that reaches the grey fade, a strong saturation step and another table coordinate) on points that include values that are no numbers, and the downsample, the two blurs, the radial blur and the reduction on small pictures given by integer formulas. Its table is the excerpt's (`--table` prints the 2,048 bytes as a C array). `out/native-evidence/post/task3_post_pins.txt` is its output.

### Not checked

1. The vertex shaders of the full-screen rectangles (`B2D9...`, `5181...`, `D5E1...`). (The furniture shaders of the sun layer and the sky and the quads' vertex shaders, listed here before plan Task 11, are checked now: "The sun layer and the sky furniture".)
2. Everything under "What stands on inference".
3. The scan-out ramp's 256 entries: no file here holds them. `post_ramp` is the SDK's rule for an 8-bit front buffer by its source (`d3d12/command_processor.cpp`, the swap blit), and the log line `post_replay.py` parses (`[native] ramp: read at clay frame N: valid 1, <256 x rrrgggbbb>`) is the plan's description of what discovery will write, not a line seen.
4. A replay of a frame other than each capture's first against its own final picture (one front buffer a capture), of a normal run (its scene colour is not in guest memory), of a dark view, night, a town or an interior.
5. Single precision, and the 7 mantissa bits of the game's scene target against the native target's 6 and 5.

### Evidence

In `out/native-evidence/post/` (git-ignored): `task3_post_check.txt`; `task3_replay_<stamp>_f1.txt` and `..._f1_offset0.txt` for the four captures; `task3_table_addressing.txt` with `task3_table_addressing.py`; `task3_predict_092959_f1.txt`; `task3_post_pins.txt`; `task3_compare_B_final_092959_f1.txt` (`compare` of two `--write` pictures); `oracle/task3_trial_092959_f1.scene` and `.block` (a trial of `export`). To rerun: `python tools/xdk_sigmatch/post_check.py`, `python tools/xdk_sigmatch/post_replay.py replay 092959 1`, `python -m unittest discover -s tests -p "test_post_*.py"`.

## The sun layer and the sky furniture

Native renderer sub-project 10, phase 10c, plan Task 11, 2026-10-10. The eleven pixel shaders and three vertex shaders of the game's sky furniture (moon, clouds, glare), of its half-size "sun layer" (the layer's depth, its occluders, the beam source, flares, moon glow, sun disc) and of the previous-frame blend, as closed formulas, each checked against its microcode; the list of quads the capture is to record; and the replay's variant C, the sun layer rasterised on the CPU. Nothing here was run in the game. Every statement rests on the shader dumps, on the research's captures (`native_discovery_20261009_134312.jsonl` towards the sun, `_092959`, `_092633`, `_120818` for the states) and on the research note's sections 1 and 5, whose reading this part checks.

### Result

| What | Shader | Function of `light_formulas.py` | State |
|---|---|---|---|
| Moon | ps `18D39DCEBEE4C2FC` | `post_moon` | closed: 2,000 of 2,000 values |
| Clouds | ps `61976295517D7015` | `post_cloud` (`post_cloud_uv`, `post_cloud_fade`) | closed: 2,000 of 2,000, both branches and every clamp of the microcode reached; 2,000 of 2,000 with the literals drawn; the size of its one-texel step is open ("What stands on inference", 8) |
| Glare | ps `04985A0F7296E130` | `post_glare` | closed: 2,000 of 2,000 |
| Beam source | ps `8E4DBB9E9A63EF67` | `post_beam_source` | closed: 2,000 of 2,000 |
| Flare | ps `D72FD0903CCE7DD6` | `post_flare` | closed: 2,000 of 2,000 |
| Moon glow | ps `4B58837D42F08C89` | `post_moon_glow` | closed: 2,000 of 2,000 |
| Sun disc | ps `3E77B1AD4BC5B900` | `post_sun_disc` | closed: 2,000 of 2,000 |
| Cloud occluder | ps `3FF79524FCDB71C5` | `post_cloud_occluder` | closed: 2,000 of 2,000 |
| Layer depth | ps `5D6768847B0602B8` | `post_layer_depth` | closed: 2,500 of 2,500 (colour and depth output) |
| Tree occluder | ps `F1EB2497108F159D` | `post_tree_occluder` (`post_tree_occluder_uv`) | closed: 2,000 of 2,000; its draws are not quads and are not recorded |
| Previous-frame blend | ps `686DE15FC84D8B73` | `post_previous_frame` | closed: 2,000 of 2,000; its weight is the draw's blend constant |
| Quad | vs `81BADE63A169E3DC` | `post_quad_position` | closed: 3,000 of 3,000 (position and coordinate) |
| Cloud quad | vs `C304C6A1AF18B46F` | `post_quad_position` | closed: 4,500 of 4,500 (position, coordinate, world position) |
| Cube | vs `D1CC6DC28244C55A` | `post_cube_position` (`post_cube_point`) | closed: 2,000 of 2,000, both faces reached; identified ("What the reading found", 3) |

(`out/native-evidence/post/task11_post_check.txt`: `post_check.py`, 500 trials a shader, tolerance 1e-9 relative, exit code 0; the largest relative difference is 2.2e-16, the cube's; with `--literals random` 3.6e-15, `task11_post_check_random_literals.txt`. `POST_FURNITURE_SHADERS` has the fourteen hashes with stage and role.) The interpreter needed nothing new: `ucode_eval.py` is unchanged. So spec criterion 16 holds for every furniture shader, and none stays out of `POST_QUAD_SHADERS` by ruling 18.

### What the reading found

1. **Five of the eight quad kinds are drawn with the alpha test on**, which neither the research, the spec nor plan Task 12's record names. Register `0x2202` (colour control) is `0x0000000C` at their rows: function 4, "greater", and bit 3, the test's enable; the reference is register `0x210E`. In all four surveyed captures: the moon 1 / 255 (`0x3B808081`), the clouds 5 / 255 (`0x3CA0A0A1`), the cloud occluders 0.149 = 38 / 255 (`0x3E189899`), the moon glow 10 / 255 (`0x3D20A0A1`), the sun disc 0.502 = 128 / 255 (`0x3F008081`). The glare, the beam source and the flares have `0x00000004` (the test off). Consequences: **the sun disc is only its texture's core**, the texels whose alpha is above one half (in `134312` 178 of the quad's 3,233 layer pixels pass the test and the depth); the cloud occluders' "blend word with colour mask 0" (the research's open point) needs no explanation by blending, since the alpha test is what decides where they write depth; and a record that does not hold `0x2202` and `0x210E` cannot draw these five as the game does. `QuadRecord` of plan Task 12 had `blend`, `depth` and `colour_mask` only; by ruling 23 the record carries the draw's own alpha-test state (on or off, the function, the reference) as two more words. **The enable bit is what tells, never the reference**: at the rows with the test off (glare, beam source, flare, layer depth, cubes, previous frame) register `0x210E` still holds `0x3F008081`, a stale 0.502.
2. **`D72F` is also drawn outside the layer.** At the bridge (`092959`) a frame has 8 `D72F` quads into a target of pitch 280 before the main scene (surface word `0x04020118`, rows 1385 on) and 2 at the layer; at the third spot (`092633`) 16 and 4, by the trees (`120818`) 4 and 2, towards the sun (`134312`) none. The pitch-280 quads have no area (their first two corners have the same position); what they are is not identified here (visibility queries of the flares, by their place among the occlusion-query draws: inference). So a record is read only where its kind is drawn (ruling 24), kind by kind: moon, clouds and glare at the main scene's target, inside the scene (the table's `layer` 0); cloud occluders, beam source, flares, moon glow and sun disc at the sun layer's target (`layer` 1), which is half the scene's size: pitch 560 in these captures because the scene's is 1120, not a number to carry. `5D67` likewise appears twice a frame at pitch 320 (once sampling a 1024 x 1024 depth, once the scene's) besides its one draw at the layer.
3. **The three cubes are slabs under the eye.** `D1CC` (instructions 5 to 32) maps a unit cube's vertex `v` to `xy = (2 v.xy - 1) * 2850` and, where `v.z = 1`, to the point `(xy + c9.xy, c9.z)`, else to `(xy, c119.x)`, and transforms that by `c0..c3` (the quads' matrix). In every captured row vertex `c9` is the eye (90.10, 158.37, 47.21 in `134312`, the clouds' pixel `c79` of the same frame to single precision) and `c119.x` is 18.47 for the first cube and 20.00 for the other two. So each is a box 5,700 wide whose upper face lies at the eye's height about the eye and whose lower face lies at the height 18.47 or 20.00 about the world's origin; its 30 indices are the five faces other than the upper one. Seen from the eye it is the plane at that height out to 2,850 (the distance of the sun's and the moon's quads): it gives the layer a depth wherever the eye looks below that plane and the scene has drawn nothing. What stands at those heights in the world (the lake's surface is the candidate) is not identified. In `134312` the cubes change no texel (9,520 pixels each, all behind the scene's depth).
4. **`5D67` writes no colour**: its colour output is (0, 0, 0, 0) for every input (`sgts` of `-|x|`, which is never above 0) and its depth output is the scene depth's value, clamped. The colour mask is 0 at its rows anyway.
5. **The moon's alpha is `c31.w * tex.a` and `c31.w` is 0 in every captured row**: with its alpha test (above 1 / 255) the moon draws nothing by day whatever its place.
6. **The sun disc's `c20.w` is 1 at its rows**, not the scene's scale: a record must take the register at the draw's call (the research has it).
7. **Every quad has the same six indices**, 0 1 2 1 3 2 (`POST_QUAD_INDICES`): corners 0 and 1 are one edge, 2 and 3 the opposite one (all `"indxdata"` rows of the furniture shaders in `134312` frame 1, by `tests/test_post_sun_layer.py`).
8. The research's formulas of the short shaders are confirmed as written (its section 5), with their alphas added: the glare's, the beam source's and the flare's alpha is 1; the moon glow's and the sun disc's is the texture's.
9. **The clouds' `c78` is the view direction, not a light's**: it equals the quads' matrix row `c3.xyz`, a unit vector, to every digit of single precision in `134312` (looking towards the sun; (-0.82364738, 0.329788029, 0.461350918) in both registers) and, by the reviewer's scan, in the other three captures; `c2.xyz` is that row scaled by the projection (1.00002) and agrees to five digits only. So `a1 = sat(-N . c78)` is how far the cloud's normal faces the eye. `c77` is a point 2,000 from the eye in a direction that is the same in both captures to 2 degrees, (-0.65, 0.06, 0.76) and (-0.62, 0.02, 0.78), 16 degrees from the centre of the beam source's quad: the light's side (what exactly it is: not identified).

### The formulas

Instruction numbers of the dumps in brackets. `tex` is the fetch of slot 13 at the quad's interpolated coordinate (slot 0 for `F1EB`); P is the quad's interpolated world position (interpolator 0 of `C304`).

```
18D3 moon            rgb = tex.rgb * c31.rgb * tex.a * c20.w         alpha = c31.w * tex.a            (2 to 5)
0498 glare           rgb = tex.rgb * c31.rgb                         alpha = 1                        (2, 3)
8E4D beam source     rgb = tex.rgb * c255.x * c31.rgb                alpha = 1     c255.x = 1 / 6     (2 to 4)
D72F flare           rgb = tex.a * c31.rgb                           alpha = 1                        (2, 3)
4B58 moon glow       rgb = tex.rgb * c255.x * tex.a                  alpha = tex.a c255.x = 0.2       (2 to 4)
3E77 sun disc        rgb = tex.rgb * c31.rgb * tex.a * c20.w         alpha = tex.a                    (2 to 5)
5D67 layer depth     colour = 0;  depth = sat(tex.r)                                                  (2 to 4)
686D previous frame  out = tex at uv + (0.5, 0.5) texels, point                                       (2, 3)
F1EB tree occluder   t = tex(uv * c8.xy + c8.zw);  rgb = t.rgb;  alpha = t.a * c15.w                  (2 to 5)
3FF7 cloud occluder  a = tex(uv * c31.xy + c46.xy).a                                                  (2, 3)
                     fade = c255.w - sat(c255.z * (sqrt(|(P.xy - c47.xy)^2 + c255.y|) - c255.x))      (4 to 9)
                     colour = 0;  alpha = a * c77.x * fade              c255 = (1000, 0, 0.001, 1)    (6, 10)
6197 clouds          t = tex(uv * c31.xy + c46.xy)                                                    (6, 7)
                     fade = c254.w - sat(c254.y * (sqrt(|(P.xy - c79.xy)^2 + c254.x|) - c254.z))      (8 to 13)
                     alpha = t.a * c69.x * fade                                                       (10, 14)
                     alpha <= c254.y:  rgb = (-|P.x| > c254.x) ? 1 : 0     (0 with the literal)       (15 to 17)
                     else:  g = (t.a at +1 texel in x - t.a at -1, the same in y)                     (18 to 21, 24)
                            N = (g.x, g.y, c70.x) / sqrt(|c70.x^2 + g.x^2 + g.y^2 + c254.x|)          (25 to 28)
                            Dn = (P - c77.xyz) / |P - c77.xyz|                                        (22 to 24, 27)
                            a1 = sat(-N . c78.xyz);  b1 = sat(-Dn . N);  b2 = sat(Dn . N)             (29, 31, 33)
                            l = b1^2 * (c254.w - t.a) + b2 + b2 * |c254.w - a1|^c255.x                (30 to 36)
                            rgb = (l * c47.rgb + c71.x) * c51.x * t.rgb                               (37 to 39)
                     out = (rgb * c20.w, alpha)         c254 = (0, 0.001, 1000, 1), c255.x = 32       (40, 41)
81BA quad            oPos = (c0 . p, c1 . p, c2 . p, c3 . p), p = (x, y, z, 1);  o0.xy = uv           (3 to 9)
C304 cloud quad      the same;  o1.xy = uv;  o0.xyz = the position                                    (3 to 10)
D1CC cube            xy = (v.xy * c254.x + c254.w) * c254.z                c254 = (2, 0, 2850, -1)    (5 to 9)
                     v.z == c255.x (1):  q = (xy + c9.xy, c9.z);  else q = (xy, c119.x)               (9 to 12, 21, 22)
                     oPos = (c0 . q, c1 . q, c2 . q, c3 . q), q.w = 1, plus c254.y (0) on two         (13 to 32)
                     components (x and y at the first face, y and w at the others)
```

The clouds in words: a layer's texture holds its colour and, in alpha, its density. The density times `c69.x` and a fade (1 within 1,000 of the eye on the ground plane, 0 from 2,000 on) is the alpha; where that is at most 0.001 the colour is black. Elsewhere the density's slope over two texels gives a normal with `c70.x` as its height; the light is a part that comes through the cloud from behind (`b1^2`, weighted by how thin the cloud is, `1 - t.a`) and a part from the front (`b2`, once plainly and once more by the 32nd power of `1 - a1`), where `Dn` is the direction from the far point `c77` (at (-1210, 275, 1563) in `134312`: 2,000 from the eye, on the sun's side) to the pixel and `c78` is the view direction ("What the reading found", 9); then the sun's colour `c47`, an ambient `c71.x`, the layer's brightness `c51.x`, the texture's colour and the output scale. The four neighbour fetches are one texel to each side (`OffsetX`, `OffsetY` of +-1 on fetches that are linear with mips); a texel of which level is not settled by anything here: "What stands on inference", 8.

### The draws' state

From the rows of `134312`, `092959`, `092633` and `120818` (4 or 8 frames each); every row of a shader has the same words. Depth word `0x00708766`: test "greater or equal" (the depth is reversed) and write; `...62`: test without write; `...60`: no test. Blend `0x07060706`: source alpha, one less source alpha; `0x01010101`: one, one; `0x00010001`: none. Every texture is fetched linear with mips and wraps (the fetch constants' filter and clamp fields), in slot 13.

| Kind (`POST_QUAD_SHADERS`) | ps / vs | Where | Depth | Blend | Colour mask | Alpha test | Registers | Texture |
|---|---|---|---|---|---|---|---|---|
| moon | `18D3` / `81BA` | scene, after the sky | `62` | `0x07060706` | F | above 1 / 255 | c20, c31 | 1024 x 128 DXT4_5 (an atlas; coordinates 0.5 to 0.625) |
| cloud (3 a frame) | `6197` / `C304` | scene, after the moon | `66` | `0x07060706` | F | above 5 / 255 | c20, c31, c46, c47, c51, c69, c70, c71, c77, c78, c79 | 512 x 512 DXT4_5 (two textures) |
| glare (1 or 2 a frame) | `0498` / `81BA` | scene, its last draws | `60` | `0x01010101` | F | off | c31 | 256 x 256 DXT4_5 |
| cloud-occluder (3) | `3FF7` / `C304` | layer, depth | `66` | `0x07060706` | 0 | above 0.149 | c31, c46, c47, c77 | the clouds' |
| beam-source | `8E4D` / `81BA` | layer, picture 1 | `62` | `0x00010001` | F | off | c31 | the glare's |
| flare (0, 2 or 4) | `D72F` / `81BA` | layer, picture 2 | `62` | `0x01010101` | F | off | c31 | 256 x 256 `k_8_8_8_8` |
| moon-glow | `4B58` / `81BA` | layer, picture 2 | `62` | `0x07060706` | F | above 10 / 255 | none | the moon's atlas |
| sun-disc | `3E77` / `81BA` | layer, picture 2 | `62` | `0x01010101` | F | above 0.502 | c20, c31 | 256 x 256 DXT4_5 |
| (layer depth) | `5D67` / `D5E1` | layer, depth | `66` | `0x00010001` | 0 | off | none | the scene depth, point |
| (cubes, 3) | none / `D1CC` | layer, depth | `66` | `0x00010001` | 0 | off | vertex c0..c3, c9, c119 | none |
| (tree occluders) | `F1EB` / `4325` | layer, depth | `66` | `0x00010001` | 0 | on, with alpha to mask (`0xAA00001C`), reference 0 | c8, c15 | 2048 x 1024 DXT1, slot 0 |
| (previous frame) | `686D` / `D5E1` | the front target, last | `60` | `0x0F0E0F0E`, constant `0x3DD0D0D2` = 0.10196 | F | off | none | the front buffer |

The rows in brackets are not recorded quads: the native layer's depth comes from the native depth, the cubes and tree occluders are plan Task 17's, the previous-frame blend is plan Task 15's (`POST_PREVIOUS_WEIGHT`).

### The quad table

`POST_QUAD_SHADERS` lists the eight kinds in the order of the record's kinds (plan Task 12), each with its vertex shader, the pixel registers its microcode reads without the literals (ascending: the record's `ps` rows in that order; `FurnitureRealDumps.test_the_quad_lists_registers_are_the_ones_the_microcode_reads` holds the list to the dumps), its texture slot, where it is drawn, its blend and whether the alpha test is on. The literals are the shaders' own and are the functions' defaults. `gen_post_quad_table.py --out src/native/capture/post_quad_table.inc` writes one `FABLE2_POST_QUAD(ps, vs, kind, layer, target, blend, alpha_test, slot_count, slot0..2, register_count, r0..r11)` line a kind (the tool's text has the arguments' meaning); it refuses a shader without a machine check (the pixel shader among the checked pixel shaders, its vertex shader among the checked vertex shaders: a pixel shader's hash is no vertex shader), a shader of the post chain, a kind at a place the game does not draw it (`KIND_TARGETS`) and the blend "depth" anywhere but at the layer's depth; `generate()` has no open default. The table says what may be read: a reader takes "not in the table" as "not read" and uses the two counts, since a padded register 0 and slot 0 are valid numbers. The file itself is not written by this task: it is plan Task 12's to generate and commit with the code that includes it, and `tests/test_gen_post_quad_table.py` compares it with the list from then on.

### The check

`post_check.py` runs the fourteen beside the post chain's eight, with what they need besides ("The post chain", "The check", for the rest):

- Every register a shader reads is drawn with four different components and the texel's four channels differ, so a wrong component, channel or slot gives a wrong value. A third of the quad shaders' trials read texels whose channels go below 0 and above 1 (`_texels_wide`): no quad shader clamps a texel (the `maxs x, x` of `18D3`, `4B58` and `3E77` is x), and a formula with a clamp or an absolute value on one now fails. The vertex shaders are given their vertex through the interpreter's vertex-fetch callback (position at offset 0, coordinate at offset 3).
- The clouds' trials are spread over the shader's branches and clamps, and the result counts what they reached (`reached`; `task11_post_check.txt`): of 500, 228 with alpha at or below the threshold (46 of them exactly at it: a texture whose alpha is the literal 0.001, `c69.x` = 1 and a pixel inside the fade's start, where the shader takes the dark branch) and 272 lit; the pixel inside the fade's start in 195, across it in 230, beyond its end in 75; the light term `a1` clamped at 1 in 95, at 0 in 106, between in 71; `c69.x` below 0 in 71 and `c70.x` below 0 in 113 (the captured values are 0.5 and 0.5 or 0.2; the other sign is drawn so that an absolute value on either fails). The cloud occluder: fade 1 in 164, between in 257, 0 in 79. The cube: 272 vertices on the face the shader singles out, 228 off it. The layer depth: the depth below 0 in 125, above 1 in 116.
- The literals are the capture rows' (`FURNITURE_LITERALS`; `tests/test_post_sun_layer.py`, `RealCapture`, reads the rows of `134312`). With `--literals random` they are drawn as well, because the microcode reads them as registers: that holds what a literal of 0 or 1 hides. Six wrong forms of the clouds pass with the game's literals and fail with drawn ones: the square root, the reciprocal square root and the power without their absolute values (with the literals their operands are never negative), and three of the thin branch, whose colour `(-|P.x| > c254.x) ? 1 : 0` is 0 with the literal `c254.x` = 0 whatever is compared: a branch that is always 0, one that reads `-|P.y|`, and one that takes "at least" for "above". For the last two a third of the drawn trials put `c254.x` between `-|P.x|` and `-|P.y|` (75 of 500) or exactly at `-|P.x|` (79) under a thin cloud. They are no errors for the game's literals; the formula follows the microcode in them all the same. (The reverse case, the threshold's strictness, is held only with the captured literals, where a trial sits on the threshold; both modes are run.)
- **Wrong forms that fail** (`FurnitureRealDumps.test_wrong_formulas_fail`, 93 forms, and the six above with drawn literals; `out/native-evidence/post/task11_mutations.txt` has each with its count at 500 trials: 99 of 105 rows caught, the six that pass being those six with the captured literals): for every check at least two, and `test_every_furniture_check_has_a_wrong_form_it_catches` holds that none is without. Moon: alpha without the texture's, without `c31.w`, colour without the texture's alpha, without `c20.w`, the register's or the texture's colour reversed. Glare: alpha from the texture, times the texture's alpha, the register reversed, the beam source's sixth. Beam source: no sixth, alpha from the texture, times the texture's alpha, the texture's colour reversed. Flare: the texture's red for its alpha, the texture's colour, alpha from the texture, the register reversed. Moon glow: colour without the alpha, alpha 1, no fifth, colour reversed. Sun disc: the moon's alpha, no `c20.w`, colour without the alpha, alpha 1. Layer depth: not clamped, from green, as colour, one less the depth. Previous frame: red and blue exchanged, alpha 1. Tree occluder: no `c15.w`, colour times `c15.w`, no colour. Cloud occluder: no fade, no `c77.x`, the distance in space, tiling and scroll exchanged, the fade not clamped, the texture's red, one texel off. Quad and cloud quad: the matrix transposed, no translation, rows exchanged. Cube: the faces exchanged, the eye under every face, under none, the heights exchanged, one height, not centred, the matrix transposed, the vertex's y and z exchanged. After the review, on the widened draws: a clamp on the texture's colour (moon, glare, tree occluder, previous frame), the texture's colour or alpha not below 0 (beam source, moon, moon glow), a clamp on the texture's alpha (flare, moon glow, sun disc) and its absolute value (sun disc); for the clouds `|c69.x|` and `|c70.x|`; in the dumps' text a `maxs_sat` for the `maxs` of `18D3` and `3E77`, and `-|P.y|` and `sge` in `6197`'s instruction 17 (with drawn literals). Clouds, 27 forms from a second writing of the formula in the test (`cloud_form`, which itself passes): the coordinate's tiling and scroll exchanged, the distance in space, the fade not clamped or absent, no `c69.x`, the threshold taken with "at least" (caught only by the 46 trials at the threshold: 1,862 of 2,000), no branch, a step of two texels, the slope reversed or its axes exchanged, the normal's height 1, the eye for the far point, a direction not normalised, the light term not clamped, the two sides exchanged, the power 16 or absent, the back term not squared or without the density, the ambient added first or outside the brightness, the sun's colour reversed, no output scale, no texture colour, the texture's alpha as the alpha.
- **Wrong microcode that fails** (`test_wrong_microcode_fails`, the dump's text changed in one place): the previous frame's half texel in x or y, a cloud neighbour's offset, the clouds' eye register, the light term's clamp, the occluder's clamp, the tree occluder's coordinate offset, the layer depth's clamp, the cube's `c9` components, the cloud quad's position components.

Not reached by a trial: the draws' state (blend, depth, alpha test and reference, colour mask); the sampler's filter, mip level and addressing, and so the size of the clouds' texel step; a density (the clouds' and their occluder's texture alpha) below 0 or above 1; single precision; inputs for which the microcode gives no number (a pixel at the clouds' far point; a slope and `c70.x` both 0), where the functions return 0 by their guards.

### Guards and clamps

As the post chain's: a texel's channel that is not finite counts as 0; a register, a position or a coordinate that is not finite gives zeros; a normal or a direction without length is (0, 0, 0). With `clamp` (the default) a result is `(v > 0) ? min(v, top) : 0` with `top` 31.875 for what is drawn into the scene (moon, clouds, glare) and 1 for the layer's `k_2_10_10_10` targets and for every alpha. The microcode has none of this; the check compares with `clamp=False`. Tests feed each function values that are no numbers (`FurnitureQuads`, `Cloud`, `Occluders`).

### Variant C: the reference with the sun layer

`post_sun_layer.py replay <capture> <frame>` (a tool of its own beside `post_replay.py`, whose functions it uses; `post_replay.py replay <capture> <frame> --variant C` hands over to it): the frame's layer draws from the capture's `"indx"` and `"indxdata"` rows (corners as the game filled them, registers, literals, state words), the scene depth the layer's depth draw names (`0x1A02A000`, by content), and the textures the rows name, rasterised at 560 x 360 with the formulas above: the layer's depth, the cloud occluders (alpha test), the cubes, the beam source into picture 1, flares, moon glow and sun disc into picture 2 with their blends, alpha tests and depth tests; both pictures reduced (`post_reduce`), picture 1 twice through `post_radial`, and variant B's bloom with `tf1` = the blurred beams and `tf2` = the reduced picture 2. `post_sun_layer.py quads` lists a frame's furniture quads.

`134312` frame 1 (`out/native-evidence/post/task11_variant_c_134312_f1.txt`; the sun at (0.3785, 0.3914) of the picture, behind leaves):

| | median | mean | 90% | 99% | within 4 levels | within 8 levels |
|---|---|---|---|---|---|---|
| variant B (no sun layer) | 1.52 | 8.70 | 26.57 | 86.70 | 57.6% | 69.9% |
| **variant C** | **0.72** | 3.17 | 7.26 | 41.44 | 76.2% | **88.5%** |
| variant C with `tf1` and `tf2` exchanged | 0.85 | 3.34 | 8.37 | 36.16 | 72.3% | 86.1% |

(absolute difference from the front buffer `0x1ECFF000` in 8-bit levels, whole picture outside the HUD.) Spec criterion 18's first part asks for at least 10 points above variant B within 8 levels **on run 3's oracle frame, which does not exist**; on this frame, `134312` frame 1, the figure is +18.6 points (by the reviewer's runs it does not hang on the tool's own choices: 88.4% with the sample point at the pixel's centre, 88.5% with every quad at texture level 0). The criterion is not judged until run 3's frame is replayed; the tool's line says which capture and frame its figure is of. Its second part (the regions "sun" and "beams" of `post-regions.json` within 15%) cannot be judged: that file is plan Task 10's and does not exist. In its place, and labelled as this tool's: the quarter of the picture about the blur's centre has 0.939 of the final picture's mean luminance in variant B and 1.015 in variant C. What the layer did: the beam source drew 11,435 of its 79,355 pixels (the others behind the scene's depth), the sun disc 178 (1,984 behind the depth, 1,071 below its alpha reference), the moon glow none (its quad lies outside the picture); the cloud occluders wrote depth at 11,949, 17,198 and 4,191 pixels; the cubes changed nothing. The pictures (`task11_134312_f1_C.png` beside `_final.png`, viewed): variant C has the beams fanning down and to the right from the sun through the leaves, across the railing and the planks, and the white-yellow sun behind the upper rail, as the game's picture has them; without the layer (`_B.png`) there is neither. What still differs is beyond 8 levels on 11.5% of the pixels: the HUD's text is outside the statistics, the beams' edges are a little harder than the game's, and the tree occluder draws are missing.

Since plan Task 8 the bloom of both variants is sampled at the composite's own fetch ("What stands on inference", item 4), and the table above, made with the centres aligned, reads: variant B median 1.13, mean 8.50, 90% 26.53, 99% 86.65, 60.6% within 4 and 70.7% within 8 levels; variant C 0.48, 2.72, 5.98, 40.93, 82.9% and **90.2%** (+19.5 points; `out/native-evidence/post/t8_variant_C_134312_fetch.txt`). The row with `tf1` and `tf2` exchanged was not made again.

The exchanged assignment fits less well on every figure but the 99th percentile; the difference is small (2.4 points within 8 levels), so this does not settle discovery question 3: it is one frame, and the tree occluders are missing from both.

### What stands on inference

1. **Which picture is `tf1` and which `tf2`**, and that both radial passes are of picture 1 (the research's reading by the weights and the engine's names; plan Task 10). Variant C's figures above are one frame's hint for the research's assignment, no more.
2. **A pixel's sample point.** The rasteriser samples pixel `i` at its integer position (the post chain's inference 2), so the layer's depth is the scene's texel (2 x, 2 y) and a quad's edge lies where that convention puts it; half a layer pixel of doubt.
3. **The depth between the two pictures**: taken as kept (the clears between them as colour clears).
4. **The cubes' culling**: drawn without it. Their cull word is `0x00218002`; seen from inside a box, five faces face the eye if any does. In `134312` it makes no difference (they change nothing).
5. **Textures**: read as stored (the sign fields of the fetch constants are 0: no gamma decode); the levels are made by means of four, since a dump holds the base level only, and a quad reads the level of its mean footprint (the sun disc: level 2.15); the cloud occluders read level 0.
6. **What the cubes' heights are** and what the pitch-280 `D72F` quads are ("What the reading found", 2 and 3).
7. The console's fetches, as in the post chain's inference 6.
8. **The clouds' one-texel step** (`6197`'s four neighbour fetches, `OffsetX` / `OffsetY` of +-1 with mips in use). Two readings, and which the console has is not measured here or anywhere in this project:
   - *a texel of the base level, whatever level is sampled*: what this emulator does. The SDK's translator multiplies the coordinate by the fetch constant's size and adds the offset (`thirdparty/rexglue-sdk/src/graphics/pipeline/shader/dxbc_translator_fetch.cpp`, "Get offsets applied to the coordinates before sampling", lines 686 to 694, and the `OpMAd` of coordinate, size and offsets at line 963). So every emulated shot, every capture and the oracle's scene colour have this step;
   - *a texel of the level being fetched*: the console's behaviour by the same file's note ("FIXME(Triang3l): Offsets need to be applied at the LOD being fetched, not at LOD 0"): inference from a comment, not from data.
   The slope `g`, and so the normal's tilt, is about 2^k times larger at mip level k under the second reading: the two differ wherever a cloud layer is minified. `post_cloud` leaves the choice to its caller's `tex(u, v, dx, dy)`. **For plan Task 16 the choice is open**: its measurable oracle is the emulated picture (the sky regions of the emulated shots, the oracle's scene), which has the base-level step; a native shader that takes the level's texel would differ from that oracle and could be judged only by eye against a console.

### Not checked

1. The clouds, the moon and the glare against a picture: they are in the oracle's scene colour already, so variant C does not draw them, and `post_cloud` stands on its machine check alone. What is known of the clouds in `134312` frame 1 (measured after the review with the layer's occluders, whose alpha `t.a * c77.x * fade` is the clouds' own `t.a * c69.x * fade`: both registers are 0.5, the textures, tiling, scroll and fade the same): of the layer's 201,600 texels 47,726 are open sky (scene depth 0); over those the three layers' alpha has medians 0.047, 0.096 and 0.047, 90th percentiles 0.229, 0.217 and 0.143 and maxima 0.396, 0.349 and 0.263; it is above the occluders' reference 0.149 at 11,949, 17,198 and 4,191 texels, at 27,900 for at least one layer (58% of the sky, 14% of the picture). The clouds' own alpha test passes above 5 / 255. So **this capture has the three cloud layers blended over most of its open sky with weights up to 0.4**. What was not examined: what they look like there. `post_cloud`'s colour was not computed for these pixels and compared with the scene colour (the sky beneath a cloud is not in any dump), and in `task11_134312_f1_final.png` I can tell no cloud's shape apart from the bright haze by eye, which says nothing either way. The moon is below the horizon in every capture (its quad's z is -3406 to -2874 in `134312`) and its alpha is 0.
2. The tree occluder draws (`4325` / `F1EB`): the shader is checked, the draws are not replayed (no row holds their vertices).
3. A flare: `134312` has none; the bridge captures have two a frame with the sun behind the camera.
4. Variant C on any frame but `134312` frame 1; on a frame of plan Task 10's run 3 when it exists.
5. The moon glow and the moon in a picture (below the horizon in every capture).
6. The size of the clouds' texel step ("What stands on inference", 8), and single precision.

### Evidence

In `out/native-evidence/post/` (git-ignored): `task11_post_check.txt` and `task11_post_check_random_literals.txt`; `task11_mutations.txt` with `task11_mutations.py`; `task11_variant_c_134312_f1.txt` and `..._exchanged.txt`; `task11_134312_f1_{B,C,final,layer1_x4,layer2,tf1,tf2}.png`. Tests: `tests/test_post_formulas.py` (the furniture's classes), `tests/test_gen_post_quad_table.py`, `tests/test_post_sun_layer.py`.
