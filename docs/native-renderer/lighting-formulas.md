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
