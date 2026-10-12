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
| baked | tf3 at the base coordinate (already a material input: `base_slot`), **as the fetch gives it**: every terrain row's fetch constant has the gamma sign on r, g, b (sign fields 3, 3, 3, 0), so the value is the filtered texel through the console's piecewise curve (`pwl_gamma_to_linear`; frame-map section 17, "The base map's gamma sign"). The check below feeds the microcode a random texture and cannot see this; the native pass read the texel as stored until the post sub-project's Task 8b |
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

The check (`--only vertex`, `check_vertex`): every vertex register random, the literal tables those of `light_check.py` (captures), tf16 a random 256-texel table, tf17 a random smooth function of its two coordinates; the two exported interpolators and the coordinates of both fetches against the formula evaluated at the exported world position. 147 + 143 + 143 + 143 fog trials and 53 + 57 + 57 + 57 near trials agree (200 each); for `129F...` (since sub-project 12, against `impostor_fog`, the eye in `c47`) 142 and 58. A formula without `c8.x` and one with tf17's coordinates exchanged fail.

The pixel side (`--only apply`, `check_apply`): `light_check`'s case of each tree pair with `b132` set and a random factor and add in the two interpolators against `fog_apply` of the family's formula: 200 of 200 for each of the six pairs (`00E0...` 16 and 44, `7CD5...` 10 and 25 are the bool and the product; `c49` is on the colour, not on the add). The earlier check of these families ran `b132` only with factor 1 and add 0.

**What ruling 4 rests on.** `vertex_fog` followed by `fog_apply` equals `fog` at the vertex if, and only as far as, tf16 is tf5, `tf17(a, t)` is `c68 + t * (tf4(a) - c68)`, vertex `c8.x` is pixel `c28.y`, vertex `c110`, `c111`, `c9` are pixel `c66`, `c67`, `c19`, and `c28.x` is 1 (`VertexFog.test_it_is_the_pixel_form_if_the_tables_are_what_ruling_4_takes_them_for`: 300 random cases to 1e-12). The registers agree in the research's rows. The tables (findings 4 and 5, settled by the captures): tf16 is tf5, tf17 is tf4 without the blend from `c68`, so the vertex form is `T = tf5(s)`, `add = (1 - T) * tf4(a) * c8.x`: the pixel form with `t` = 1 and `c28.x` = 1. With `c67.z` = 1 the two differ only within one unit past the fog start, where tf5 reads its first texel, (1, 1, 1) in every captured frame, so both give the colour unchanged there. What stays a difference in any case is the interpolation of factor and add across a triangle against the per-pixel evaluation.

Read only, by `FOG_VERTEX_SHADERS`: `D57E...` (a leaf shader whose literal table is in no file here; its block, 209 to 217, is the leaves' line for line). The distant trees' impostors `129F...` (70 to 85), whose block has **no `c8.x`** and takes the eye from vertex `c47`, not `c9`, were read only until sub-project 12 and are checked since (`VERTEX_CHECKED` has five; "The impostors" below: `impostor_fog`); their pixel shader `4B1D...` applies the pair without a bool (58).

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

Native renderer sub-project 10 (exposure, composite, bloom, sun layer), plan Task 3, 2026-10-09. The eight pixel shaders of the game's post chain as closed formulas, each checked against its microcode; the replay of the chain on the CPU against the game's own final picture; the numbers the C++ mirrors are pinned to. Nothing here was run in the game when this part was written; what the composite and the ramp do in the game since (phase 10a: the native picture against the replay's prediction at the bridge and at a third spot, and the two shaders on stored scenes of the game's on the GPU) is in `frame-map.md` section 17, "Native composite", "Oracle feed" and "Validation (10a)"; the bloom's (its steps on the game's stored scenes on the GPU, bloom on against off in the game against the replay's prediction) in "Native bloom" and "Validation (10b)". Every statement rests on the shader dumps, on the four read-back captures of the research (`out/build/win-amd64-release/logs/native_discovery_20261009_092959.jsonl`, `_121331`, `_132250` at the bridge, `_134312` towards the sun; 4 frames each, made with `--readback_resolve=full`), on the SDK's translator source where it is cited, and on the research note `.superpowers/sdd/post-research.md`, whose reading this part checks and whose section 1 has the pass list.

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
4. **Where the composite samples the bloom.** The replay first sampled it linearly with texel centres aligned (pixel `x` of 1,120 reads the 280-wide bloom at `(x + 0.5) / 4`); the game's fetch lies 0.375 of a bloom texel further on: the composite fetches tf2 at the scene's own coordinate with the same half-texel offset (instruction 6, as instruction 3 fetches the scene), each in its own texels, so the scene is read at `(s + 0.5) / 4` bloom texels and the bloom at `s / 4 + 0.5` whatever the interpolator `s` is; with inference 2's `x / N` that is `x / 4 + 0.5`. Since plan Task 8 the replay (`post_replay.py --bloom-place fetch`, the default, also of variant C and of the stored excerpt), the mirror and the product sample there (controller ruling 27): against the front buffer of the three bridge captures variant B's median is 0.565 to 0.578 levels with the fetch's place and 0.716 to 0.732 with the centres aligned (`t8_bloom_fetch.txt`). `--bloom-place aligned` reproduces the table below, which was made with the centres aligned; with the fetch's place variant B reads, bridge regions / median / within 8: 0.997 to 1.027 / 0.58 / 94.5%, 0.997 to 1.022 / 0.57 / 94.8%, 0.994 to 1.009 / 0.57 / 94.8%, and `134312` 1.13 / 70.7% (`t8_reference_B_<stamp>.txt`). The place does not hang on inference 2 (review of plan Task 8, Minor 4); what remains inference is that the emulator's fetch offsets are the console's, and the fit is the emulator against itself.
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
2. **A pixel's sample point.** The rasteriser samples pixel `i` at its integer position (the post chain's inference 2), so the layer's depth is the scene's texel (2 x, 2 y) and a quad's edge lies where that convention puts it; half a layer pixel of doubt (measured since: see "Since plan Task 11", 2).
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

### Since plan Task 11 (closing note of plan Task 18, 2026-10-10)

This section is plan Task 11's; what the later tasks found is in `docs/native-renderer/frame-map.md` section 17 ("Sun layer discovery" to "Validation (10c)"). In short, against the lists above:

1. **Run 3's frame exists** (capture `20261010_052158` frame 1) and variant C meets spec criterion 18 on it: 82.7% of the pixels within 8 levels against 63.9% without the layer; "sun" 1.021, "beams" 1.132 and 1.100. The beams' excess is at their root (1.16), cause open.
2. **A pixel is sampled at its integral position** by the game at the layer's draws and in the scene (`PA_SU_VTX_CNTL` = 4 in the captures' command words): the replay's convention, first an inference ("What stands on inference"), is a measurement.
3. **The tree occluder (`4325` / `F1EB`)** redraws a leaf draw the game issues after its depth copy and fades to half (`c15.w` 0.5); it still cannot be replayed (a draw row holds 512 of the mesh's 18,722 indices). The reference and the native layer both lack it.
4. **The cloud occluders' fetch**: the reference reads level 0, the game's fetch constant names mips 0 to 5. A per-pixel level in the reference changes layer 1's drawn set by about fifty texels of 12,300 (`out\native-evidence\post\t18_occluder_level_052158_f1.txt`); the tool keeps level 0.
5. **The clouds against a picture** ("Not checked", 1): held by hue against the game's own scene at the bridge: place, drift and sign, one pair confirmed by the tool's rule, two at its edge (51% against 50%); strength not decided (controller ruling 43; `tools/xdk_sigmatch/post_cloud_hue.py`, `tests/test_post_cloud_hue.py`).
6. **Still not checked**: a flare in front of the depth, the moon and its glow in a picture, anything at night.
7. **The clouds' texel step** ("What stands on inference": "for plan Task 16 the choice is open"): plan Task 16 took a texel of the base level (`CloudTexelStep`), the emulator's reading; which the console has is still not measured.

## Wind

(Since plan Task 11: the native pass computes these formulas in one vertex program, validated in the game; frame-map section 18, "Native wind" and "Validation (11a)". The program's HLSL text is held to this section's reference by value in `tests/test_wind_hlsl.py`.)

Native renderer sub-project 11 (foliage), phase 11a, plan Tasks 5 to 7, 2026-10-10. The wind of the game's vertex shaders as closed formulas of the position, each checked against its microcode: what the native pass will compute where it draws a "deformed" record at rest today (frame-map section 12, "Wind and displacement"; section 18). Nothing here was run in the game. Every statement rests on the shader dumps and on the capture rows of `native_discovery_20261010_182130.jsonl` (towards the sun), `_182313` (the bridge) and `_121036` (the third spot).

### Result

| Family | Shader | Role | Function of `light_formulas.py` | State |
|---|---|---|---|---|
| Plant | `8123C16DBF583F92` (grass, ferns) | scene | `wind_plant` | closed: 2,000 of 2,000 made draws; 1,496 capture rows, 90,966 of 90,966 vertices |
| Plant | `B636821F95DC9D8E` (bushes) | scene | `wind_plant` | closed: 2,000 of 2,000; 844 rows, 54,016 of 54,016 |
| Plant | `48D30ECCF684F488` (near) | scene | `wind_plant` | closed: 2,000 of 2,000, the push loop at 0; 99 rows, 6,226 of 6,226 |
| Plant | `FC4F2EF6930768BE` (near) | scene | `wind_plant` | closed: 2,000 of 2,000, the push loop at 0; 69 rows, 4,416 of 4,416 |
| Plant | `33C00226C154C1F2` (near) | scene | `wind_plant` | closed: 2,000 of 2,000, the push loop at 0; 48 rows, 3,072 of 3,072 |
| Plant | `A8A5B2A89F70336A` | shadow caster | `wind_plant` | closed: 2,000 of 2,000; 279 rows of its own streams (two captures), 17,856 of 17,856, of which 2,305 are beyond the cut |
| Trunk | `475EC9F795E5EDBB` (trunks, branches) | scene | `wind_trunk` (`wind_tree_offset`, `wind_tree_terms`) | closed: 2,000 of 2,000; 1,464 rows, 92,128 of 92,128 |
| Trunk | `FBD39C64463E180B` | shadow caster | `wind_trunk` | closed: 2,000 of 2,000; 1,110 rows of its own streams, 69,968 of 69,968 |
| Leaf | `A5846836C90E1192` (leaf clumps) | scene | `wind_leaf` (`wind_leaf_lift`) | closed: 2,000 of 2,000; 866 rows, 55,424 of 55,424 |
| Leaf | `D7DD31B9BF194DE8` (two lights) | scene | `wind_leaf` | closed: 2,000 of 2,000; 91 rows, 5,824 of 5,824 |
| Leaf | `2D40B53C926109BE` (four lights) | scene | `wind_leaf` | closed: 2,000 of 2,000; 3 rows (one capture), 192 of 192 |
| Leaf | `432563420047C96C` | shadow caster, tree occluder | `wind_leaf` | closed: 2,000 of 2,000; 1,008 rows of its own streams, 64,512 of 64,512 |

(`wind_check.py formulas` and `rows`; the bound is spec criterion 1's, `1e-4 * max(1, |oPos|)` in every component; the largest relative difference on made draws is 1.4e-14 and on capture rows 1.4e-14: the two sides are the same arithmetic in double precision. The final run of all twelve, `out/native-evidence/foliage/t7_wind_check.txt`: 24,000 of 24,000 made draws; 464,600 of 464,600 vertices of 7,377 capture rows, 305,343 of them moved; every identity exact. The eight captures made later the same day (`190920`, `191043`, `191202` to `191341`: other directions of the wind, `|c17.xy|` up to 12.5) at 16 vertices a row: 172,016 of 172,016, `t7_wind_check_more.txt`. `WIND_SHADERS` has the hashes with family, role, registers, literal places and elements.) `36B543CECAE5C781` (the push alone) and `6AD4108C3FF07966` (nothing) have no row. All twelve are closed: spec criteria 1 and 2 hold for each (criterion 1 names captures `120435` and `120714`; the two with whole index files and the casters' own streams, `182130` and `182313`, stand in for them).

### The plants' formula

`wind_plant(p, z, a3, b3, c7xy, c10x, c21, c22, c25x, c26xy, lit)`, the block every one of the six shaders holds (instruction numbers of `8123`; frame-map section 12, "Instancing", has the fetches and the cut before it):

```
f     = frac(c7.x + c7.y + B3 + A3)                     39 to 42    the copy's seed
theta = 1 + f - c10.x * c25.x * (1 + gain * f)          39, 43 to 45
theta = frac(theta * inv_two_pi + half) * two_pi + lo   47, 48, 50   wrapped to [-pi, pi)
h     = sat(height * z)                                 52
p'    = p + h * cos(theta) * (c26.x * c21 + c26.y * c22)   51 to 58
```

- `p` is the world position of the vertex at rest: the mesh position through the copy's three rows plus the draw's offset `c7.xyz` (`r5` at instr 31), exactly what the instance entry of `vs-transforms.json` draws today. The distance cut (32 to 37) has tested `p` before: a vertex beyond it never reaches the block, and the cut stays the entry's.
- `A3`, `B3` are the fourth stored components of the copy's first two rows (its x and y translation), `z` the mesh vertex's third coordinate: a blade stands at its root and sways whole from `z = 1 / height` (0.2 of the mesh's units) up.
- `c10.x` is the clock in seconds, `c25.x` the rate (1, or 0.2 for some bushes and casters), `c21` the wind's direction in the world (a unit vector that turns in steps), `c22` a second direction ((-0.35, 1.53, 0) in every row), `c26.xy` the two amplitudes ((0.025, 0.0637)). One cosine a copy; its period is `2 pi / (c25.x * (1 + gain * f))`, 5.7 to 6.3 s at rate 1.
- The largest offset is `wind_reach_plant(c21, c22, c26xy)` = `|c26.x c21 + c26.y c22|`: 0.108 units at the bridge.
- With `c26` = 0 the plant stands. The bridge's caster rows with `c25.x` = 0 have `c26` = 0 (ledger ruling 26: the bound of `c25.x` is [0, 64]).
- The four near shaders then push `xy` away from up to four points in a loop over integer constant `i0` (`c[56 + a0]` on). The push is not drawn (spec ruling 10) and not in the formula; the check runs the loop 0 times and says so.

### The trunks' formula

`wind_trunk(p, w4, c15, c16, c17, lit)` = `p + wind_tree_offset(w4, c15, c16, c17, lit)`: instr 17 to 58 of `475E` build the offset that instr 65 adds to the fetched position (`FBD3`: 8 to 50, added at 51). The research had the block's structure only ("42 instructions with co-issued scalar ops"; spec risk 4); it closes. It was transcribed register by register with the interpreter as the oracle at each step (`wind_check.py trace 475E --instr N`; `TrunkAgainstMicrocode.test_trace_gives_the_transcriptions_steps` holds two of the steps) and then folded.

`p` is the fetched position read as `yxw1` (the tree's own frame: `c0..c3` hold the tree's place). `w4` = (b, a, d, c) is the 8_8_8_8 of dword 7 in the fetch's **source** order x, y, z, w: the shader's register holds it as `yxwz` in `475E` and in `FBD3` alike, the capture's swizzle word for it is 1217 in both. With `t = c15.x` (the clock plus the tree's own phase), `g = c17.w * gust` (`c17.w` is twice the clock) and `s(q)` the sine of the wrapped phase `frac(frac(q) + half) * two_pi + lo`, that is `sin(2 pi q)`:

```
s0 = s(rate0 * t)                                                        17, 18, 22 to 27
s1 = s(rate0 * b + d * c16.x + (c16.y + rate1) * t + g)                  17 to 26
s2 = s(d * c16.x + (c16.y + rate2) * t + g)                              17 to 28
along  = d^2 * c17.z * along * (amp * s1 * (|amp * s0| * quarter + base) + c17.z * lean)     26 to 31
across = d^2 * c17.z * across * s2                                                           26 to 31
s3 = s(phase_y * t + b)                                                  35 to 41, 46
s4 = s((c16.z * t + g + phase_x) * c + b)                                40 to 48
s5 = s((c16.z * t + g + phase_y) * c + b)                                40 to 49
S  = (amp * s3 + half) * a * (half * c17.z) * c15.z                      50 to 54
u  = amp * s4 * S,   v = amp * s5 * S                                    50, 55
x = along * c17.x - across * c17.y + u                                   32 to 37, 58
y = along * c17.y + across * c17.x + v
z = -quarter * along^2 - half * (u^2 + v^2)                              38, 40, 49, 56, 57
```

- Two motions. `along` and `across` are the sway of the whole tree along the wind's direction `c17.xy` (the direction in the tree's own frame, per instance) and across it, growing with the square of `d` (the vertex's height weight): a slow sine (`rate1`, 0.035 a second, with the instance's `c16.y`) under a slower envelope (`rate0`), about a constant lean `c17.z * lean`. `u` and `v` are a branch's own wobble, scaled by `a`, with a phase that `b` and `c` set: without `b` and `c` its phase is 0 and it is still.
- The drop of `z` is second order: it keeps a swaying branch near its length (the research's reading of instr 56, 57; instr 38, 40 and 49 are the same for the sway).
- A vertex whose four values are zero stands, exactly. `c16.w` (-1 in every trunk row), `c15.y` and `c15.w` are not read.
- The selects at instr 32 and 34 take `c17.xy` and its quarter turn because the literals `c253.z` and `c253.w` are 0 and 1; `c254.w` (0) is added at instr 56 and is the third component of the vector at 32. The formula is the microcode with those three at 0, 1, 0 (`WIND_STRUCTURE`: `sel_zero`, `sel_one`, `zero`); the rows hold them so that an object with another table is not drawn with it.
- `wind_reach_tree(w4_max, c15, c16, c17, lit)` bounds the offset's length with every sine at 1 for a mesh whose four values are at most `w4_max`: 0.57 units for the bridge's registers with `w4_max` = 1 (`wind_pins.py`); a sweep of the clock reaches more than a quarter of the bound (`Trunk.test_the_offset_never_leaves_its_reach`).

| Name | Value | `475E` | `FBD3` | | Name | Value | `475E` | `FBD3` |
|---|---|---|---|---|---|---|---|---|
| `half` | 0.5 | `c251.x` | `c251.x` | | `phase_y` | 0.11 | `c254.y` | `c254.y` |
| `base` | 0.7 | `c251.y` | `c254.w` | | `lo` | -pi | `c254.z` | `c254.z` |
| `rate1` | 0.035 | `c252.x` | `c252.x` | | `amp` | 0.3849 | `c255.x` | `c255.x` |
| `rate2` | 0.472 | `c252.y` | `c252.y` | | `along` | 0.3 | `c255.y` | `c255.y` |
| `two_pi` | 2 pi | `c252.z` | `c252.z` | | `across` | 0.019245 | `c255.z` | `c255.z` |
| `quarter` | 0.25 | `c252.w` | `c252.w` | | `rate0` | 0.1 | `c255.w` | `c255.w` |
| `lean` | 0.04 | `c253.x` | `c253.x` | | `zero` | 0 | `c254.w` | `c253.z` |
| `gust` | 0.025 | `c253.y` | `c253.y` | | `sel_zero` | 0 | `c253.z` | `c253.z` |
| `phase_x` | 0.23 | `c254.x` | `c254.x` | | `sel_one` | 1 | `c253.w` | `c253.w` |

The caster keeps 0.7 where the scene shader has its 0 (`c254.w`) and takes its zero from `c253.z`. The element: `("weights", fetch, 6, 7, "xyzw", 4)`, fetch ordinal 4 in `475E` and 1 in `FBD3` (`TrunkAgainstMicrocode.test_the_elements_are_the_dumps_fetches`).

**Checked** as the plants are ("The check"): made draws with `c15..c17` inside Design 5's bounds and their edges (a clock up to 1e7, `c17.w` up to 2e7), the position's halves and the four bytes over their ranges, one draw in 25 without weights: 2,000 of 2,000 for both shaders (worst relative difference 4.9e-15; 1,587 and 1,616 moved). Capture rows: `475E`: 1,464 rows of the three captures (the main scene's and the reflection views'), 92,128 of 92,128 vertices, 64,350 moved; `FBD3` on its own streams (two captures): 1,110 rows, 69,968 of 69,968, 38,144 moved; worst relative difference 3.4e-15. Identity: `FBD3` on `475E`'s streams and bank: 1,176 main-scene rows, 18,816 vertices, largest difference **0.0** (the scene shader's `oPos` is up to 0.44 to 0.54 from rest in them); the same on made draws (`test_the_caster_is_the_scene_shader_on_made_inputs`). Mutations, each failing 114 to 162 of 200 made trials: `c16` for `c15`, the first two values of fetch 4 exchanged, the sign of `c17.xy`, `amp` changed by 1%; and each of the fifteen numeric literals, in both shaders, changed by 1% alone is caught and changed together with its place in the microcode's constants agrees again.

### The leaves' formula

`wind_leaf(p, q, w4, v3, c15, c16, c17, lit)`: instr 22 to 98 of `A584` (`D7DD`: to 100; `2D40`: to 102; the caster `4325`: 14 to 87). It closes too, in three parts:

```
Q' = Q + wind_tree_offset(w4, c15, c16, c17, lit)          22 to 74    the pivot sways as a trunk vertex does
D  = P - Q,   L = |D|                                      26, 35, 40
D' = (D.x, D.y, D.z + wind_leaf_lift(...))                 75 to 93
position = Q' + L * D' / |D'|                              94 to 98
```

The first part is the trunks' block instruction for instruction (the same eighteen literals, the same four bytes of dword 7 in the same source order), so a leaf's pivot moves exactly as the branch vertex with its weights does. `Q` is the half4 of dword 5, read as the position is (`yxw`). The flutter, with `t`, `g`, `b`, `d` as above, `T = t + c17.w * rate_f` and `v` the 8_8_8_8 of dword 8 in source order:

```
s6 = s(rate_f * b + (c16.y + rate1) * t + g + c16.x * d + phase_f)       29, 73, 76 to 82
s7 = s(T * flutter_x + v.z + hundred * v.x)                              72 to 79, 83, 86, 88
s8 = s(T * flutter_y + v.y)                                              75, 77 to 87
k  = amp * s6 where s6 >= 0, back * s6 where it is below                  84, 85
push = amp * s7 * max(L + slack, 0) * c16.w                              73, 89 to 92
lift = c15.w * (|k| * amp * s8) * k                                      88 to 91
wind_leaf_lift = push * (push_gust * c17.z + one) + lift * (lift_gust * c17.z + one)     72, 79, 93
```

- The vertex keeps its distance from its pivot (instr 98 brings the turned direction back to `L`), and only the direction's third coordinate is pushed: a leaf card nods up and down about its branch. `push` grows with the distance beyond `-slack` (0.3 units); `lift` is one value a clump (it has no per-vertex input but `v.y`).
- **Fetch 5 gives three values, not two** (the research's perturbation of real vertices saw source y and z): source x enters at a hundredfold (`hundred`), so a byte of 0 or 255, which 95% of the captured vertices hold, adds a whole number of turns and nothing else; the other 108 byte values that occur (28 in 318 vertices of every leaf mesh) do move the phase. So a leaf's wind reads **ten** values a vertex beside the position: the pivot's three, the four of dword 7, three of dword 8.
- **Seven literal registers, not five**: `A584` reads `c249` to `c255` (28 named components), `D7DD` `c248` to `c254`.
- `c16.w` is the push's strength: 0.035 in every leaf row, -1 in every trunk row (which does not read it).
- Where `D'` has no length (a vertex at its pivot and no lift) the microcode multiplies 0 by 1 / 0; a product with a zero factor is +0 on the console (`ucode_eval`'s `zero_product`, the SDK interpreter's rule), which gives `Q'`, and so does the formula. **Vertices at their pivot are in the game's drawn meshes** (review of Tasks 5 to 7, I1; this text first said "no captured vertex is there", which was not looked for and is wrong). Three leaf vertices of each capture have position and pivot equal bit for bit: vertex 230 of a 600-vertex mesh, and vertices 91 and 7863 of a 14,334-vertex mesh (3 of 54,582 vertices in 24 leaf streams of capture `182130`, 3 of 56,839 in 28 of `182313`, 3 of 51,550 in 14 of `121036`). In the two captures with whole index files every one of the three is indexed by every draw row that names its stream (the 600-vertex mesh: 24 rows in `182130` and 48 in `182313`, all of `A584`, 16 and 40 of them rows the capture marks as in the main scene; the 14,334-vertex mesh: 16 rows in each, 13 of `A584` and 3 of `D7DD` in `182130`, 8 of `A584` and 8 of the caster `4325` in `182313`); `121036` keeps 512 indices a row, among them vertex 230 (24 rows of `A584`) and vertex 91 (16 rows: 8 of `A584`, 8 of `D7DD`) and not 7863 (`LeafMeshesOfTheCaptures.test_vertices_at_their_pivot_are_drawn`). At such a vertex `L` = 0 and `push` = 0, so `D'` = (0, 0, `lift`): the lift alone gives it a length (the review measured -0.0165 to +0.0092 on these rows, changing sign with the sine), and `|D'|` passes through zero.
- **The zero-length rule (ledger ruling 36).** Where the moved offset `D'` has zero length in the program's own precision (`|D'|^2` computed there is 0, by a lift of exactly 0 or by underflow of the square), the position is the moved pivot `Q'`: the C++ mirror and the vertex program return `Q'` and do not divide. Without the rule IEEE arithmetic gives `L * D' * rsqrt(0)` = `0 * inf`, no number, and the vertex's triangles are dropped for that frame; the console gives +0 and `Q'`. The rule changes nothing elsewhere: at a vertex at its pivot the position is `Q'` whatever the direction (`L` = 0), so the result is continuous through the zero. The pin: the last row of `wind_pins.LEAF_MADE`, a vertex at its pivot with `c15.w` = 0 and `c16.w` = 0 (the lift exactly 0 in double and in single precision), expecting `Q'` (`LeafAtItsPivot`); the two older made rows at the pivot keep `c15.w` = 0.32, whose lift is not zero, and pass without the rule.
- `s8` is written in the microcode as `frac(half * (2 * frac(q)) + half)`; with `half` = 0.5 that is the wrap of the others, and the formula keeps the microcode's form (found by the literals' place check: a formula with the plain wrap fails it).
- `wind_reach_leaf(w4_max, length_max, c15, c16, c17, lit)` = the pivot's reach plus the turn, at most twice the lift's bound and never more than `2 L`: **the bound needs the mesh's largest `|P - Q|`**, which no register holds (ledger ruling 32: it is taken from the mesh when its wind words are built). Over the whole leaf streams of captures `182130` and `182313` (every vertex of them is indexed by a leaf draw) `|P - Q|` is 1.476 and 1.478 units at the median, 4.28 and 4.43 at the 99th percentile and 19.299 and 19.908 at most; the "12.4 at most" of frame-map section 12 was of 8,890 sampled vertices of one frame and is not the largest (review M9). `wind_pins.REACH_LENGTHS` = (0, 1.48, 19.91) are the lengths the leaves' reach pins are made at (`LeafMeshesOfTheCaptures.test_the_reach_lengths_are_the_whole_streams`).

The places of the ten literals the leaves add (the eighteen of the trunk are in the rows; `D7DD` holds everything one register lower than `A584`, `2D40` as `D7DD` but `base` at `c248.z` and `hundred` at `c248.w`):

| Name | Value | `A584` | `4325` | | Name | Value | `A584` | `4325` |
|---|---|---|---|---|---|---|---|---|
| `hundred` | 100 | `c249.z` | `c252.y` | | `one` | 1 | `c253.w` | `c254.w` |
| `slack` | -0.3 | `c250.x` | `c251.z` | | `back` | 0.28867501 | `c254.w` | `c255.w` |
| `phase_f` | 0.05 | `c250.y` | `c251.w` | | `push_gust` | 0.55 | `c255.x` | `c253.z` |
| `flutter_x` | 0.25 | `c250.z` | `c250.x` | | `lift_gust` | 0.9 | `c255.y` | `c253.w` |
| `flutter_y` | 1 | `c250.w` | `c250.y` | | `rate_f` | 0.15 | `c255.z` | `c252.x` |

**Checked**: 2,000 of 2,000 made draws for each of the four shaders (worst relative difference 3.8e-15; one draw in 25 without weights and flutter, one in 25 with the vertex at its pivot). Capture rows: `A584` 866 rows, 55,424 of 55,424 vertices; `D7DD` 91 rows (two captures), 5,824 of 5,824; `2D40` 3 rows (capture `182130`, frame 8), 192 of 192; `4325` on its own streams 1,008 rows (two captures), 64,512 of 64,512; worst relative difference 8.4e-15; 111,923 of the 125,952 moved. Identities: `4325` on the streams and bank of `A584`: 674 main-scene rows, 10,784 vertices, largest difference **0.0**; of `D7DD`: 91 rows, 1,456 vertices, 0.0; of `2D40`: 3 rows, 48 vertices, 0.0. **The tree occluder** (`4325` at the layer target `0x09000230`, its own bank) on its twin's streams against the twin's own shader: 56 of 56 rows find a twin in the three captures (8, 24, 24), 896 vertices, **0.0**; in the eight newer captures 72 of 72 rows, 0.0. Mutations fail 141 to 190 of 200 trials; each of the 25 numeric literals of each shader changed by 1% alone is caught and with its place agrees again.

### The table, the pins, positions through the table

- **`vs-transforms.json`**: each of the twelve entries has a `"wind"` member, `light_formulas.wind_table_member(hash)` of its row: `family`, `role`, `reg`, `lit` (name to single-precision value), `lit_at` (name to `"c253.x"`), `lit_reg`, `elements` (`name`, `fetch`, `format`, `offset`, `source`), `evidence` (`Table.test_the_tables_members_are_the_rows`).
- **`gen_transform_table.py --wind-out src/native/capture/wind_table.inc`** writes one `FABLE2_WIND(hash, family, caster, five registers, three elements)` a shader and one `FABLE2_WIND_LITERAL(hash, slot, register, component, bits)` a literal, `slot` being the literal's place in the family's order (the file's head has the forms). It refuses a member that is not exactly its family's (`WindTableTest`: an unknown family, a literal too many or too few, a value that is not a single-precision number, two values at one place, a wrong register list, element, format or source, a tree with an instance, an entry that is not deformed). The transform table has no line for the member and is byte for byte what it was.
- **`wind_pins.py`** prints the lines the C++ mirrors are to be pinned to (plan Task 10): sections `// wind plant pins`, `// wind trunk pins`, `// wind leaf pins`, `// wind reach pins`; 21 real vertices (every one of the twelve shaders; capture, line and vertex beside each) and eleven made ones (the eleventh: a leaf vertex at its pivot with the lift exactly 0, ruling 36); inputs as the native side packs them and the reference's position rounded to single, all as float bits. The same expressions with every operation rounded to single precision are within 7e-6 of a unit of them (`Pins.test_a_single_precision_evaluation_is_near` holds 2e-5, the tolerance of ruling 35).
- **`position_check.py --wind`** (criterion 4): the entry's position with the wind (`wind_positions`: `replay`, then the member's formula on the row's registers and elements) against the microcode's `oPos` in pixels of 1120 x 720. On the three captures (`out/native-evidence/foliage/fix57_position_check_wind.txt`; the figures are those of `t7_position_check_wind.txt`, line for line): every shader with a vertex in view met, 10 of 10 (`182130`), 10 of 10 (`182313`) and 8 of 8 (`121036`); the largest 99th percentile is 5.72e-4 pixel (`FC4F`, capture `121036`; this text first gave 4.7e-4, which is `33C0`'s in `182313`: review M1) and the largest single difference 7.5e-4 (the plants, whose replay rounds the copy's transform to single precision as the runtime does; the trees 4e-6). **The pixels are measured on the vertices the microcode puts inside the clip volume**: a vertex off the picture or behind the eye has no pixel (the near plants stand at the camera's feet, where x / w has no bound: a first run that divided there read 5 pixels for a difference of 1e-5 units), so its two clip positions are held to 1e-4 relative instead (largest 8.5e-6). Over the three captures 245,565 vertices are judged in pixels, 165,739 in clip space alone and 53,296 are beyond the cut on both sides. **A shader with rows and no vertex in view is "not judged"** and is not counted as met (review M2; the tool first printed "met" for it): `FC4F` and `48D3` in `182130` (0 vertices in view of 24 and 20 rows; 1,536 and 1,280 outside, within 7.1e-6), both judged in the other two captures; `33C0` is judged there on one vertex. Every one of the twelve shaders is judged in at least one capture. The vertices are those of each row's first 512 indices.
- **`vs_check.py --casters`**: `4325` has left `CASTER_UV_ONLY`; its position is compared with the wind on through its entry's member (1,200 of 1,200 random vertices). `A8A5` stays in `CASTER_SWAY_OFF` (its test of the cut wants the entry at rest to disagree with the sway on).

### The literals are data

The shaders of one family hold the same numbers in other registers and components, so a formula takes them by name (`lit`) and `WIND_SHADERS[hash]["lit_at"]` says where each shader holds each (spec ruling 9). `WIND_LITERALS[family]` has the single-precision values, `WIND_LITERAL_ORDER[family]` the names in the order the generated table and the native constants keep them. The plants':

| Name | Value | `8123` | `B636` | `48D3`, `FC4F` | `33C0` | `A8A5` |
|---|---|---|---|---|---|---|
| `lo` | -pi | `c253.x` | `c253.x` | `c253.x` | `c253.x` | `c253.y` |
| `two_pi` | 2 pi | `c253.y` | `c254.z` | `c253.z` | `c253.w` | `c253.x` |
| `inv_two_pi` | 1 / (2 pi) | `c253.z` | `c253.z` | `c253.w` | `c253.z` | `c253.z` |
| `gain` | 0.1 | `c254.w` | `c255.w` | `c254.w` | `c254.w` | `c254.w` |
| `height` | 5 | `c255.w` | `c253.y` | `c255.w` | `c255.w` | `c255.w` |
| `half` | 0.5 | `c254.z` | `c254.w` | `c254.z` | `c254.z` | `c254.z` |
| `unit` | 1 | `c254.y` | `c254.y` | `c254.y` | `c254.y` | `c254.y` |

`unit` is structure (`WIND_STRUCTURE`): the 1 of the phase is also the `w` the shader gives its position (instr 25), so the formula has it as the number 1 and the row keeps it only so that a shader object whose table differs there is not drawn with the formula. `half` is also the bias of the instancing index (the entry's `"bias"`). The other five are the formula's alone. The trunks' table is with their formula above.

### The check

`tools/xdk_sigmatch/wind_check.py` (its module text has the method; `tests/test_wind_formulas.py` runs a reduced count):

- **One trial** is a draw (microcode, constant bank, vertex streams as bytes) and one vertex index. The microcode gives `oPos`. Beside it, from the same bytes and registers but through nothing of the interpreter's, the elements are decoded at the places the entry names, the formula moves the position at rest and `c0..c3` make a clip position. Agreement is criterion 1's bound in every component. A trial "moved" when the microcode's `oPos` is more than 1e-3 from the clip position at rest, and a check passes only if at least half of its trials moved (review focus 3).
- **Made draws** (`formulas`): the wind registers inside the spec's Design 5 bounds as the ledger amended them (`c25.x` from 0: ruling 26; `|c17.xy|` to 16: ruling 28), each component at its lower or upper edge in about three trials of ten (the clock's size spread over eight decades up to 1e7), every other register drawn, the elements over their whole ranges; one trial in 25 is a draw without sway (`c26` = 0, half of them with `c25.x` = 0) and one in 25 a vertex beyond the cut (the microcode's position is then no number, and so must the check's be). Every value is rounded to single precision first. The literal registers of a made draw hold **only what the row names, at the row's places**: a literal that a row misplaces or leaves out fails.
- **Capture rows** (`rows`): every wind draw row that has streams, 64 vertices a draw (all of a smaller one): the main scene's rows and, since plan Task 1, the casters' own. The row's registers are rounded to single precision (the capture prints nine digits); the row's own `"vs_lit"` must hold the table's literals bit for bit, and its `"fetches"` the table's elements. All 176,552 vertices of the 2,835 plant rows of the three captures agree (run of plan Task 5; 90,926 of them moved; the rest are at or below their root, beyond the cut or in a draw without sway). The caster's rows at the bridge hold 2,305 vertices beyond the cut, where `oPos` is no number in x, y, z and 1 in w (the shadow projection's fourth row is 0 0 0 1): the check requires exactly that.
- **Identities** (`identities`, criterion 2): the caster's microcode on a scene draw's streams and bank against the scene shader's `oPos`. `A8A5` on `8123`: 1,496 rows, 14,082 vertices, largest difference **0.0**; on `B636`: 844 rows, 9,842 vertices, **0.0**; the scene shader's own `oPos` is up to 0.089 to 0.106 from rest in the same vertices, so the comparison can fail. The caster's literals are its row's, at its row's places (`A8A5` holds `two_pi` and `lo` exchanged against `8123`).
- **Mutations that must fail** (`MUTATIONS`; `test_mutations_fail`): `c22` for `c21` (`register`), the copy's seed read from its rows' third components (`bytes`), the direction's sign (`sign`), `gain` changed by 1% (`literal`): 109 to 147 of 200 made trials disagree for each of the six shaders (the trials that stand or are cut cannot tell). On real rows of the bridge the `sign` mutation fails too (`OnACapture`).
- **Every literal is where the row says** (`test_every_literal_is_where_the_row_says`): each of `lo`, `two_pi`, `inv_two_pi`, `gain`, `height`, `half`, in each shader, changed by 1% in the formula alone is caught, and changed in the formula and at the row's place in the microcode's constants the two agree again.
- `trace <hash> --instr N` prints the interpreter's registers after an instruction for a made draw (the transcription's oracle: `test_trace_gives_the_register_after_an_instruction`).

### What stands on inference, and what is not checked

1. Single precision. Both sides of the check compute in double precision on single-precision inputs. At the captured clocks (56 to 73 s) the game's own single-precision phase differs from this by about 1e-5 of a turn; at a clock of 1e7 the game's arithmetic has no phase left at all, and neither would a native mirror: the check there is of the formula's shape, not of a picture. What the native side does about it is "Single precision (ledger ruling 35)" below.
2. The push of the near plants (not drawn), and the loop's count (in no capture row).
3. A vertex texture: none is on the position's path of these shaders.
4. What the registers mean (the clock's unit, the direction's turning) is frame-map section 18's, measured there; the formula only says how they are used.
5. A literal's value without a capture: the rows' values are held bit for bit by every capture row's own `"vs_lit"` (`wind_check.py rows`) and, without a capture, by the literal tables earlier sub-projects read from captures (`Literals.test_the_values_are_the_tracked_literal_tables`); `FBD3` has no such table and is held by its capture rows alone (its one table, the same in every row of frame 1 of captures `182130` and `182313`, holds the row's eighteen literals; `vs_check.CASTER_LITERALS` does not take it because `tests/test_gen_caster_table.py` holds `FBD3` outside that table).
6. The leaves' reach needs the mesh's largest `|P - Q|`. A leaf vertex at its pivot with no lift rests on the console's zero-product rule, which is the interpreter's (`ucode_eval`), not measured on a console; such vertices are drawn, and the native side takes the rule over as ruling 36 (the leaves' formula above).

### Single precision (ledger ruling 35)

The native wind, the C++ mirror and the vertex program alike, computes **in single precision, in the reference's order of operations, marked `precise`**; the clock is not carried in double. Why: the clock arrives as a single-precision guest register (so there is nothing wider to carry), and the game's own picture has the same loss: the target is the game's picture, not the exact sine. A mirror is pinned at the captured clocks only, with a tolerance of **2e-5** of a unit against the pins (which are the double reference rounded to single; measured distance 7e-6 for the plants, 2e-6 for the trees).

How far single precision is from the double reference as the session grows (the review's measurement, its script `prec.py` in its scratch folder, every operation rounded to single, the pins' real rows with the clock replaced and `c17.w` = 2 t; largest position difference in world units, the sway itself being 0.1 to 0.3; not re-run for this text):

| Clock | Trunk | Leaf | Plant |
|---|---|---|---|
| 60 s | 6e-7 | 2e-6 | 6e-6 |
| 1 h | 2e-5 | 6e-5 | 2e-5 |
| 4 h | 2.4e-4 | 2.3e-4 | 1.1e-4 |
| 24 h | 4e-4 | 7e-4 | 8e-4 |
| 48 h | 2.6e-3 | 1.6e-3 | 7e-4 |
| 100 h | 4e-3 | 6e-3 | 9e-4 |
| 1e6 s | 9e-3 | 1.8e-2 | 6e-3 |
| 1e7 s | 7e-2 | 8e-2 | 8e-2 |

A single-precision phase is down to 1 / 256 of a turn at 2^15 turns: the leaf's `s8` after about 7 h, the trunk's `s2` after 15.6 h, the wobble after 20 h, a plant (radians, rate 1) after about 73 h; the clock register itself steps by 7.8 ms from 18.2 h on. That is the game's loss too, already in the register the capture reads, and both pictures degrade alike. A reduction in double of the phases that are constant per draw would make the native sway smoother than the game's after a day and would be a deviation needing its own ruling; it is not made. The one place where single precision needs a rule of its own is the zero-length case (ruling 36).

### Evidence

`out/native-evidence/foliage/` (git-ignored): `t7_wind_check.txt` (`formulas --mutations`, `rows`, `identities` on the three captures), `t7_wind_check_more.txt` (the eight newer captures), `t7_position_check_wind.txt` (criterion 4, as first printed) and `fix57_position_check_wind.txt` (the same run with the "not judged" verdict), `t7_mutations.txt` (the mutation check of the tools). Tests: `tests/test_wind_formulas.py`, `WindTableTest` of `tests/test_gen_transform_table.py`, `WindTest` of `tests/test_position_check.py`.

## Covered samples (alpha-to-mask)

Sub-project 11, phase 11b (plan Task 12). The samples a pixel covers under alpha-to-mask on the main scene's two samples; reference `light_formulas.covered_samples`, mirror `CoveredSamples` in `src/native/render/clay_logic.h`.

### The rule

`RB_COLORCONTROL` (0x2202): bit 4 alpha-to-mask, bits 24 to 31 four 2-bit offsets, one a pixel parity, index `(x & 1) * 2 + (y & 1)` from bit 0. On a surface of two samples (`RB_SURFACE_INFO` bits 16 to 17 = 1; the main scene's `0x11810460`):

```
t0 = 0.5 - offset / 8          the first sample's threshold
t1 = 1.0 - offset / 8 = t0 + 0.5   the second sample's
covered = (alpha >= t0) + (alpha >= t1)        0, 1 or 2; an alpha that is no number covers nothing
```

`alpha` is the pixel shader's output alpha, not clamped: for family A (leaf `7CD5`, trunk and branch `00E0`, `58D3`, `2FBE`) the albedo's w times pixel `c26.w` times `c15.w`, the fade word (`ps_albedo_table.inc`: scale refs 107 and 63; `c26.w` is 1 in every captured row); for the plants' pixel shaders the albedo's alone, after an alpha test "greater than 0.50196".

| offsets | index 0 (x even, y even) | 1 (even, odd) | 2 (odd, even) | 3 (odd, odd) |
|---|---|---|---|---|
| `87` (fading draws, and every plant) | 0.125 / 0.625 | 0.375 / 0.875 | 0.5 / 1.0 | 0.25 / 0.75 |
| `AA` (trees at fade 1; the late draws at fade 0.5) | 0.25 / 0.75 | 0.25 / 0.75 | 0.25 / 0.75 | 0.25 / 0.75 |

Since `t1 = t0 + 0.5`, a covered second sample implies a covered first: "one sample" is always the first, which is the sample the game's depth copy holds (frame-map section 18, "Discovery runs"). The fully covered set (`covered` = 2) and the half-covered set (`covered` = 1) part exactly the set the one-sample native target keeps today (`AlphaToMaskCovers` under two samples: `alpha >= t0`).

A draw whose alpha is at most `scale` has a fully covered pixel only if `scale >= 1 - max(offset) / 8` (`coverage_has_full`, `CoverageHasFull`): 0.625 under `87`, 0.75 under `AA`. The late draws (`AA`, 0.5) have none: every pixel they cover is half-covered.

### The check

`tests/test_coverage.py`: `covered_samples` against a transcription of the emulator's instruction sequence (SDK `src/graphics/pipeline/shader/dxbc_translator_om.cpp`, `CompletePixelShader_AlphaToMask` and `CompletePixelShader_AlphaToMaskSample`, lines 1674 to 1790: the float-to-unsigned of the position, the bit insert of x's low bit above y's, the 2-bit field of the constant `(RB_COLORCONTROL >> 24) | 0x100`, the multiply-add `offset * -scale + base` in single precision, the greater-or-equal) for all 256 offset bytes, the four parities and the alphas 0 to 1.25 in steps of 1 / 1024: 1,311,744 inputs, 0 differ. Four wrong rules (x and y exchanged, "greater" for "at least", one threshold for both samples, the offsets' bits shifted) each differ. `tests/native/test_clay_logic.cpp` cases 700 to 703 hold the C++ mirror to the same closed form and to `AlphaToMaskCovers` on the same grid.

### What stands on inference

That the console's hardware covers samples as the emulator does (the emulator is the oracle of every picture this renderer is compared with). Which of the console's two samples is "the first": the emulator's top sample; the game's depth copy holds the sample at (-0.25, -0.25) of the pixel (measured, plan Task 4), and whether that is the console's convention or the emulator's read-back is not known.

## The impostors

Native renderer sub-project 12 (distant trees), phase 12a, plan Tasks 4 and 5, 2026-10-11. The far woods' impostors as closed formulas, each checked against its microcode: vertex shader `129F7F2793DA9084` (81 instructions, numbered 10 to 90) and pixel shader `4B1D3E6E8E58D3AC`. Nothing here was run in the game, and **no capture yet holds a `129F` draw's storage or its vertex registers** (a `"verts"` row of the captures in hand has `c0` of the bank and no bytes): everything below is closed on made inputs and on the shader dumps alone. (Since: the five captures of plan Task 3 hold both, and the formulas close on them: "The formulas on the captures' own rows", below.) The captured rows are plan Task 6's.

### Result

| What | Function of `light_formulas.py` | State |
|---|---|---|
| The corner of a vertex | `impostor_corner(i)` | closed with the position |
| `oPos`, `o0`, `o1` of `129F` | `impostor_position(k, word, c)` | closed on made inputs: 2,000 of 2,000 made draws, largest relative difference 1.5e-13; seven mutations and four literals each fail; **on the captures' rows (plan Task 6): 18,976 of 18,976 corners of 116 rows, worst 1.7e-15** ("The formulas on the captures' own rows") |
| `oC0` of `4B1D` (bool `b129` clear) | `impostor_distance`, `impostor_pixel`, `impostor_out`; the state's `impostor_alpha_test` | closed on made inputs: 2,000 of 2,000 made pixels, and 2,000 of 2,000 with each of three captured views' constants; five mutations and two literals each fail |
| `o2`, `o3` of `129F` (the fog, once a tree) | `impostor_fog` | closed on made inputs: 1,900 of 1,900 compared made draws (1,413 begun, 487 before the start), the fetches' coordinates included; two mutations fail; also in `fog_check.py` |
| Which bit is `b129` | `IMPOSTOR_B129` = `0x00000002` | bit 1 of the pixel bool word, by the SDK's layout; clear in the captured word |

(`impostor_check.py formulas` and `pixel`, seed 1: `out/native-evidence/distant-trees/t4_formulas.txt`, `t5_pixel.txt`; the pixel's and the fog's bound is 1e-5, "The check of the pixel shader and of the fog" below. The position's bound is spec criterion 1's, `1e-4 * max(1, |value|)` in every component of `oPos`, `o0` and `o1`; the two sides are the same arithmetic in double precision, which a test holds below 1e-9.)

### The position: what `129F` computes

`word` = (x, y, z, s) is register `r6` after the fetch of instruction 12: one half4 a quad, the tree's place and its size. `c` are the fourteen vertex registers `IMPOSTOR_SHADER["reg"]`: `c8`, `c46`, `c47`, `c72..c75`, `c110`, `c111`, `c113..c117` (the interpreter's own list on made draws with the fog begun: `test_the_registers_are_the_interpreters`; the position reads all but `c110` and `c111`, and of `c47` only z). Literals: `c254` = (0.5, 0.25, 2, 0), `c255` = (0, 1, -1, 4) (frame-map section 12, "Literals").

With `P = (x, y, z + s * c8.z)` the **raised place**, `a = c46.xyz`, and `V` the 3 x 4 matrix whose rows are `c115`, `c116`, `c117` (`V(p)` = the three rows on `(p, 1)`; `V1`, `V2`, `V3` its components):

    top  = P + (0, 0, s)        bottom = P - (0, 0, s)          the upright axis
    fore = P + s * a            back   = P - s * a              the lying axis
    ev = (c114.x * |V2(top) - V2(bottom)|) ^ c114.y             the upright axis's extent in V's second row
    eh = |V2(fore) - V2(back)| ^ c114.y                         the lying axis's
    wv = ev / (ev + eh)         wh = eh / (ev + eh)
    above = P.z > c47.z                                         strictly; the raised height against the eye's
    the picture's top edge (corners 1, 2):     Q = wv * V(top)    + wh * V(back if above else fore)
    the picture's bottom edge (corners 0, 3):  Q = wv * V(bottom) + wh * V(fore if above else back)
    Q.x += -s (corners 0, 1) or +s (corners 2, 3)
    oPos = (c72 . (Q, f), c73 . (Q, f), c74 . (Q, f), c75 . (Q, f)),  f = 1 (0 where Q.x is no number)
    o0 = P                                                      the same at all four corners
    o1 = (u * c113.x + c113.y, v * c113.w + c113.z),  (u, v) = (0, 1), (0, 0), (1, 0), (1, 1) at corners 0 to 3

`a ^ b` is `2 ^ (b * log2 a)` with `log2 0` = -infinity and `2 ^ -infinity` = 0. In words: the quad's sideways axis is the first axis of `V`'s space, of half-length `s`; its other axis is a blend of the world's vertical and of the lying axis `c46`, weighted by how long each shows in `V`'s second row, the weights sharpened by the exponent `c114.y` and the vertical's favoured by `c114.x`. A camera that looks level sees an upright quad (`eh` = 0), one that looks straight down sees the quad lying along `c46`, its top end away from or towards the camera by whether the tree is above the eye (the tests `test_a_level_camera_sees_an_upright_quad`, `test_a_camera_looking_down_sees_the_quad_lying_along_c46`, `test_below_the_eye_the_lying_quad_is_turned_end_for_end`, `test_between_the_two_the_weights_are_the_powers_of_the_two_extents`, `test_the_size_goes_sideways_in_the_view_not_in_the_world` hold each sentence by hand-written numbers). What `V` and `c72..c75` are in the game (a view matrix and a projection, by the form) is **inference**: their values are in no capture; `Q` is in `V`'s space, so `c72..c75` cannot be the scene's whole view-projection `c0..c3` unless `V` is the identity (plan Task 6's identity is then `c0..c3` = `c72..c75` after `V`, not `c72..c75` alone).

Instruction by instruction (`+` is the co-issued scalar operation; "prev" is the previous scalar result, which `muls_prev` and `adds_prev` read; `lift` = `s * c8.z`):

| Instr | Register | Holds |
|---|---|---|
| 10, 11 | `r0.z`, `r0.y` | `i + 0.5`, `(i + 0.5) * 0.25`: the fetch's index |
| 12 | `r6` | the quad's word (x, y, z, s): `vfetch_full r6.yxwz`, the index floored by the fetch |
| 13, 14 | `r11.w`, `r10.w` | `x * c116.x + y * c116.y`, `x * c115.x + y * c115.y` (`dp2add` with the literal 0) |
| 15, 16 | `r4.x`; `r3.x`, `r2.w` | `-s * a.x`; `s * a.z`, `-s * a.y` (prev = `-s`) |
| 17 | `r1.w` | `lift` |
| 18 | `r2.x`, `r3.y` | `lift + s`, `-s * a.z` |
| 19 | `r4.z`, `r0.y` | `lift - s`; the quad's number, `trunc((i + 0.5) * 0.25)` |
| 20 | `r4.y`, `r4.w` | `s * a.y`, `s * a.x` (prev = `lift`) |
| 21 | `r1.z` | the corner `k = i - 4 * quad` |
| 22 | `r0`; `r2.y` | (`back.x`, `bottom.z`, `fore.y`, `fore.x`); `s * a.z + lift` |
| 23, 24, 25 | `r1.x`, `r1.y`; `r2.z` | `k != 1`; `k == 1 or k == 2`; `-s * a.z + lift` |
| 26 | `r4` | (`back.z`, `back.y`, `top.z`, `fore.z`): each is the unraised z plus a term that holds `lift` |
| 27 | `r9.xyz` | (`back.x`, `back.z`, `back.y`) |
| 28 | `r3.x`, `r3.z` | `top.z * c115.z`, `fore.z * c115.z` |
| 29 | `r5.y`; `r6.z` | `x c115.x + y c115.y + top.z c115.z`; **`z + lift`: the raise** (from here `r6` is `P`) |
| 30 | `r8.z`, `r8.w`; `r0.x` | `x * c117.x`, `y * c117.y`; `bottom.z` |
| 31 | `r2.w`; `r7.x` | `above` (`sgt`, on the raised z); `c117.z * bottom.z` |
| 32 | `r4.x`, `r4.y`; `r7.z` | `top.z * c116.z`, `fore.z * c116.z`; `c117.x * fore.x` |
| 33 | `r2.x`, `r2.z` | `u` (0, 0, 1, 1); "the picture's top edge" (0, 1, 1, 0) |
| 34, 35 | `r1.x`, `r1.y`; `r0.x` | `V2(bottom)` and `fore.x c116.x + fore.y c116.y`, each without its offset; `V2(top)` without it |
| 36, 37 | `r3.x`, `r5.x`, `r5.z` | `V2(bottom)`, `V2(top)`; `back . c115.xyz` (`dp3`, in the order z, y, x) |
| 38, 39 | `r3.y`, `r3.w`; `r1.z`, `r1.x` | `V1(bottom)` and `fore.x c115.x + fore.y c115.y`, without offsets; `V2(top) - V2(bottom)`, `V2(fore)` without its offset |
| 40, 41 | `r1.y`, `r1.x`, `r1.z` | `c114.x * |V2(top) - V2(bottom)|` (prev = the absolute value); `V2(fore)`, `V2(back)` |
| 42, 43 | `r1.y`, `r1.w`; `r2.y` | `log2` of the first; `log2 |V2(fore) - V2(back)|`; `v = 1 - top edge` |
| 44, 45, 46 | `r8.x`, `r8.y`; `r7.w` | `ev`, `eh` (`exp` of the logs times `c114.y`); `c117.y * fore.y` |
| 45, 46, 48 | `r3.z`, `r3.w`; `r4.x`, `r4.y` | `V1(bottom)`, `V1(fore)`; `V1(top)`, `V1(back)` |
| 47, 48 | `r5.y`, `r5.z`, `r5.w`; `r0.x` | `x c117.x + y c117.y`, `ev + eh`, `fore.x c117.x + fore.y c117.y`; `1 / (ev + eh)` |
| 49, 50 | `r7.y`; `r0.y`, `r0.z` | `back . c117.xyz`; `wv`, `wh` |
| 50 to 53 | `r3.y`, `r5.y`, `r1.y`, `r1.w` | `V3(bottom)`, `V3(top)`, `V3(fore)`, `V3(back)` |
| 53 to 57 | `p0`; `r1`, `r3.w`, `r5.w` | `above`: `r1` = (`V2`, `V3` of fore, of back), `r3.w` = `V1(fore)`, `r5.w` = `V1(back)`; not above: fore and back exchanged in all three |
| 58 to 63 | `r0.xyz` | the top edge: `Q` = `wv * V(top) + wh * V(r5.w, r1.zw)` |
| 64 to 66 | `r0.xyz` | the bottom edge: `Q` = `wv * V(bottom) + wh * V(r3.w, r1.xy)` |
| 67, 69 | `r0.w`, `r0.x` | `2 u - 1`; `Q.x + (2 u - 1) * s` |
| 68 | `r2.xy` | `o1` |
| 70, 71 | `r1.xyz`, `r0.w`; `r4.w` | `c47 - P` and its square (the fog's: below); `|Q.x| >= 0` |
| 72 to 76 | `r4.xyz`, `r3` | (`Q.x`, `Q.z`, `Q.y`); `oPos`, each row as `row.z Q.z + row.y Q.y + row.x Q.x + row.w f` |
| 86 to 88 | `oPos`, `o1`, `o0` | `r3`, `r2.xy`, `r6.xyz` |

Every sum and product of `impostor_position` is made in the microcode's order (the extents are differences of whole dot products, not `2 s` times a row), which is why the two sides agree to 1.5e-13 and not merely inside the bound.

**Where the plan's and the spec's reading differ from the interpreter:** nowhere in the three tests "written from the reading" (the raise, the picture's corners, `w` = 1 under unit rows): all three pass as written. Three statements of the documents are sharpened: (1) the spec's "two candidate points chosen by the corner (15 to 27)": instructions 15 to 27 build four points (top, bottom, fore, back) for every corner; the corner chooses only at 58 to 66, and the height comparison at 53 to 57. (2) The research's "`oPos` = the corner (x, y, z, 1) through `c72..c75`": the fourth factor is `|Q.x| >= 0` (71), which is 0 where `Q.x` is no number; under rows whose first three entries are 0 such a corner has `oPos.w` = +0, not "no number" (`test_a_corner_that_is_no_number_has_no_fourth_factor`). (3) The plan's made input "`c46` a unit horizontal vector": the microcode reads `c46.z` (16, 18, 22, 24, 26), so half of the made draws give it a third component; with a horizontal `c46` alone those terms would be unchecked.

### The degenerate points, and the console's product rule

The formula returns what `ucode_eval` returns, with the console's rule that a product with a zero factor is +0 whatever the other factor is (`zero_product`; the SDK's interpreter and translators say the same):

- **A quad of no size, or a view whose second row has no part of either axis**: both extents are 0, both powers `2 ^ -infinity` = 0, the reciprocal of their sum is infinite and the weights are `0 * infinity` = **+0 by the console's rule** (IEEE: no number). `Q` is then (-s or +s, 0, 0): the corners lie at the origin of `V`'s space. An IEEE interpreter disagrees in exactly these trials (100 of 2,000: the made kinds `size0` and `flat`) and in no other. Nothing is drawn of such a quad either way; **a native mirror must reproduce the rule or guard `ev + eh == 0`** (it would otherwise hand the rasteriser a position that is no number where the reference has a point, which is harmless on screen but fails a pin).
- **One axis without extent** (`level`, `down`): its power is 0, the other weight 1; no rule is needed.
- **A size or place that is no number** (`nan`), **an infinite size** (`inf`): `oPos` is no number wherever a row's entry is not 0; `o0` holds the no-number or the infinity.
- **`c114.y` = 0** (`still`): both powers are 1 and both weights one half, also for an axis without extent (`0 * -infinity` is +0 by the rule).

### The check

`tools/xdk_sigmatch/impostor_check.py` (its module text has the method; `tests/test_impostor_formulas.py` runs a reduced count).

- **One trial** is a draw (the microcode, a constant bank, the storage's bytes, one half4 a quad) and a vertex index. The microcode gives `oPos`, `o0`, `o1`; its fetch decodes through `capture_replay.element` and the shader's own swizzle. Beside it, from the same bytes and registers, `quad_words` decodes the word (a second decoder: the order and endian of `IMPOSTOR_SHADER`), `impostor_corner` gives the corner and `impostor_position` the outputs. `test_the_check_compares_the_machines_outputs_with_the_formulas` holds that the two sides are two computations.
- **Made draws**: the ranges are the constants at the module's head (the plan's, with `c46.z` in half of the trials and the eye's height about the raised place); seven trials in forty are the degenerate kinds above. Literal registers hold only the table.
- **Floors** (a check that cannot fail is no check): each corner 500 of 2,000; above 995, below 1,005; both powers positive numbers in 1,700; both weights inside (0.02, 0.98) in 1,273.
- **Mutations that must fail** (`POSITION_MUTATIONS`, of 400 made draws): `rows` (`c116` for `c115`) 360; `components` (the word's first two exchanged) 400; `sign` (the size's) 385; `corners` (0 with 1, 2 with 3) 400; `exponent` (`c114.y` by 1%) 173; `raise` (left out) 389; `matrix` (`c72` and `c73` exchanged) 380.
- **Every literal is where the table says**: `two` (`c254.z`), `one` (`c255.y`), `minus_one` (`c255.z`), `four` (`c255.w`), each changed by 1% in the formula alone, fails 187, 399, 326 and 285 of 400; changed in the formula and at its place in the microcode's constants the two agree again in 400 of 400. `half`, `quarter` (the fetch's index: a 1% change does not move a floor) and `zero` cannot be held this way; their values are the table's.
- **Mutations of the tools themselves** (a scratch copy, sixteen changes of `light_formulas.py` and `impostor_check.py`, `out/native-evidence/distant-trees/t4_mutations.txt`): fifteen turn at least one test red; the sixteenth (the reciprocal of 0 made "no number") changes no result, because both weights are then products with a zero factor.
- `trace --instr N` prints the interpreter's registers after an instruction for a made draw.

### Single precision

Both sides of the check compute in double precision on single-precision inputs (the word's on half-precision ones). `impostor_position(..., single=True)` rounds every register, the word and every operation's result to single precision, in the same order: what a single-precision mirror would compute. On the made draws that are not degenerate (1,650) it lies from the double reference by a median of 2.0e-7, a 99th percentile of 8.8e-6 and at most **1.76e-4** of `max(1, |oPos|)` (seed 1; 2.46e-4 with seed 7): the largest are draws whose rows `c72..c75` have entries up to 200 on places up to 2,000 units, where a clip coordinate is a small difference of large products. So **criterion 1's bound of 1e-4 holds between the formula and the microcode, not between a single-precision mirror and the double reference on inputs of this size**; plan Task 9's pins need either the single-precision reference (`single=True`) as their source or a tolerance stated from the captured registers' sizes. Reported, not a check.

### What stands on inference, and what is not checked

1. **No captured row.** The word's order and endian (`word_source` (1, 0, 3, 2), `word_endian` 2: register `r6` is then the four big-endian halves in the order they lie in memory) are read from `vfetch_full r6.yxwz` and from the endian every other captured stream has; plan Task 3's captures settle them, plan Task 6's `rows` holds them.
2. What the registers mean (`c46` the lying axis, `V` a view, `c72..c75` a projection, `c114` the blend's exponent and gain, `c8.z` the raise) is read from the form; no value of any of the fourteen is in a capture.
3. That `ucode_eval` is the console, the product rule included, as for every formula of this document.
4. The fetch's index rounding: the dump has no `RoundIndex`, so the index is floored; with the literals 0.5 and 0.25 the index of vertex `i` is `i / 4 + 0.125`, never nearer than 0.125 to a whole number.

### The pixel shader `4B1D`

(Plan Task 5.) Interpolators: `o0` the tree's raised place (one value a tree: all four corners export the same), `o1` the picture's coordinate, `o2` and `o3` the fog pair. Pixel registers `IMPOSTOR_SHADER["ps_reg"]`: `c12`, `c19`, `c20`, `c29`, `c31`, and no other with bool `b129` clear (`test_the_check_runs_the_pixel_shader_on_the_picture_at_o1`: the interpreter's own list). Literal `c255` = (0, 1, 0.1, 0.5), the same in every `129F` row of three captures (`out/native-evidence/distant-trees/impostor_consts.txt`); the formula reads `one` (`c255.y`) and `tenth` (`c255.z`).

    d      = |c19 - o0|                                              impostor_distance (instr 8, 9, 10)
    colour = texel.rgb * c31.x * (1 + sat(0.1 * (d - c12.y)) * (c12.w - 1))        impostor_pixel (10 to 15)
    alpha  = texel.a * sat((d - c29.x) * c29.y)                                    (11, 12, 15)
    oC0.rgb = (colour * o2 + o3) * c20.w;   oC0.a = alpha                          impostor_out (58 to 60)

| Instr | Register | Holds |
|---|---|---|
| 7 | `r4` | the picture at `o1.xy` (`tfetch2D`, `tf0`) |
| 8, 9 | `r1.xyz`, `r0.w` | `c19 - o0`; its square, summed z, x, y |
| 10 | `r1.w`; `r1.y` | `c12.w - 1`; `d` (`sqrt` of the absolute value) |
| 11 | `r0.w`; `r1.x` | `d - c12.y`; `d - c29.x` |
| 12 | `r2.w`; `r0.w` | `sat((d - c29.x) * c29.y)`: the dissolve; `sat(0.1 * (d - c12.y))` |
| 13 | `r1.xyz` | the picture's colour times `c31.x` (blue, green, red; turned back at 15); prev = the dissolve |
| 14 | `r0.w` | the light factor `sat(..) * (c12.w - 1) + 1` |
| 15 | `r4.xyz`; `r2.w` | `colour`; `alpha` = the picture's alpha times the dissolve (`muls_prev`) |
| (2.0) | | `cjmp !b129, L13`: instructions 16 to 57, the ground-fog volume, are skipped |
| 58, 59, 60 | `r0.xyz`, `r2.xyz`, `oC0` | `colour * o2 + o3`; times `c20.w`; the output, its alpha `r2.w` |

With the captured registers (`c12` = (20, 35, 2, 0.468 to 0.499), `c29` = (48, 0.125, 998.4, 25.6)): the light factor is 1 up to 35 units and `c12.w` from 45; the alpha is the picture's times `sat((d - 48) / 8)`. Under the draw's alpha-to-mask with offsets `AA` (thresholds 0.25 and 0.75 at every pixel, `covered_samples`) a tree whose picture alpha is 1 is absent below 50 units, covers one sample of two from 50 to 54 and both from 54: three steps a tree, no dither (`test_the_dissolve_is_three_steps_a_tree`). (Since: that is the alpha-to-mask alone. With the draw's alpha test, whose captured reference is 128 / 255, such a tree is absent below 52.02 units and has one sample of two from there to 54: "The formulas on the captures' own rows", point 3, below.)

**The alpha test** is the state's, not the shader's: colour control `0xAA00001C` has the test on (bit 3) with function 4, "greater" (bits 0 to 2). `impostor_alpha_test(alpha, ref)` is `alpha > ref`; **the reference is in no capture** (since: 128 / 255 in 116 of 116 rows, "The formulas on the captures' own rows", point 3, below) and travels with the draw (plan Task 3 reads it, Task 8 carries it). At any reference in [0, 1] an absent tree (alpha 0) fails.

**Bool `b129`** (`IMPOSTOR_B129` = `0x00000002`; as fixed by plan Task 5): a bool constant N is bit `N & 31` of register `0x4900 + (N >> 5)` (the SDK's `graphics/pipeline/shader/interpreter.cpp`: `bool_constants[bool_address >> 5] & (1 << (bool_address & 31))`; `register_table.inc`: `SHADER_CONSTANT_BOOL_000_031` at `0x4900`), the dumps print that N, and `ucode_eval` takes it as printed. So `b129` is **bit 1 of the pixel bool word** (register `0x4904`, `b128..b159`), as the plan inferred. The captured word, `0x00000100` (`b136` alone), has it clear in every `129F` row of the three captures, and the GPU's own register had `b129` clear in every compared main-scene colour execution of two GPU logs (frame-map section 12, "The bool word": 0 of 14,320 and 0 of 12,980; whether a `129F` draw is among those executions was not looked up). With the bool set the microcode goes on to the block's first fetch (`tf12`): `test_with_b129_set_the_microcode_reads_the_ground_fog`. **A caution for plan Task 8**: the rows' `"pbool"` is the device's shadow of the word, which the same section of the frame map refutes as the GPU's value for draws whose shaders come from `GpuLoadShaders` (the engine keeps its own word and has a setter for `b129` at `0x8226E060`); the `ground-fog` rule should test the word the GPU is given, not the shadow alone.

**The console's product rule at the pixel** (`zero_product`; the formula has it): an absent tree's alpha is +0 also for a picture alpha that is no number; a tree nearer than `c12.y` has light 1 also under an infinite `c12.w`; `c31.x` = 0 gives no colour also from an infinite texel. An IEEE interpreter disagrees in exactly the 150 made trials of these three kinds. A place that is no number makes both saturates 0 (`sat` of no number is 0): the colour is the picture's times `c31.x`, the alpha 0.

### The impostors' fog

`impostor_fog(place, eye, c110, c111, tf16, tf17)` is the block of `129F`, instructions 70 to 85 (the comment in `light_formulas.py` and this document said "75 to 85"; the block starts at 70 with the vector to the eye): **the trees' vertex form without the `c8.x` factor**, evaluated once a tree at the raised place with the eye from vertex `c47` (not `c9`):

    (a, s, t) = fog_coords(place, c47, c110, c111);  before the start: o2 = (1, 1, 1), o3 = (0, 0, 0), no table read
    o2 = tf16(s).rgb;   o3 = (1 - o2) * tf17(a, t).rgb

Instructions: the vector `c47 - place` and its square (70, 71; `r6` is the raised place since 29), `d` (74), `d - c111.x` (75), the predicate (76), `t` and `1 / d` (77), the direction (78), `a` and `s` (79), the fetches (80: `tf17` at (a, t); 81: `tf16` at s), `1 - T` (82), the add (83), the values before the start (84, 85), exported as `o2`, `o3` (89, 90). `c8.x` is not read: this shader's `c8` is the raise (`c8.z`).

Where the place, the eye or a register is no number the formula gives the pair of "before the start" (`vertex_fog`'s guard); the microcode fetches at a coordinate that is no number there. Such draws are not compared by the check (100 of 2,000 made draws: the kinds `nan` and `inf` whose place is no number), and the record's rules refuse them.

### The check of the pixel shader and of the fog

`impostor_check.py pixel` (seed 1; `out/native-evidence/distant-trees/t5_pixel.txt`); the bound is spec Design 5's, `1e-5 * max(1, |value|)`; both sides are the same arithmetic, held below 1e-9 by the tests (measured: 0.0 on the made pixels).

- **Pixel**: 2,000 of 2,000 made pixels, each of `oC0`'s four components; the picture is a smooth function of its coordinate and the microcode's one fetch must be `tf0` at `o1.xy`; any other texture is an error (so the `b129` block is shown not to run). Floors: a real fog pair in 1,498; nearer than the dissolve's start 658, inside the dissolve 575, beyond it 767; the light factor strictly between its ends in 528; 50 each of the four degenerate kinds.
- **With each captured view's constants** (`c12`, `c29`, `c31.x`, `c20.w` of captures `054648`, `074119`, `022921`, as `impostor_consts.txt` prints them, six digits): 2,000 of 2,000 each (absent 794, dissolving 563, whole 643, the light falling 479).
- **Fog** (`check_fog`): the position check's made draws; the microcode's `o2`, `o3` against `impostor_fog` at `impostor_position`'s raised place (not at the microcode's own `o0`): 1,900 of 1,900 compared; begun in 1,413, both fetches at the formula's coordinates in 1,413; before the start 487 (no fetch, exactly 1 and 0).
- **Mutations that must fail** (`PIXEL_MUTATIONS`, of 400): `start` (`c12.y` for `c29.x`) 196; `tenth` (the literal 0.1 by 1%) 102; `exposure` (`c31.x` left out) 400; `sign` (the light factor's) 297; `picture` (the alpha without the picture's) 265; of the fog `one_minus` (the `(1 - o2)` left out) 277; `c8x` (a `c8.x` factor put in) 277.
- **Literals by their place**: `one` (`c255.y`) and `tenth` (`c255.z`) changed by 1% in the formula alone fail 195 and 102 of 400; changed in the formula and at their place the two agree again.
- **`fog_check.py`** now holds `129F` among its checked vertex shaders (`VERTEX_CHECKED`, five; before this task it listed four and `FOG_VERTEX_SHADERS` called `129F` "read only", so no machine check held its fog): 142 fog trials and 58 near trials of 200 agree, the coordinates of both fetches included; the trees' form with `c8.x`, exchanged coordinates and a moved eye each fail (`test_the_impostors_form_has_no_c8x_and_its_eye_is_c47`). There the place is the one the shader exports; `check_fog` above takes it from the formula.
- **Mutations of the tools themselves** (a scratch copy; `t5_mutations.txt`): eighteen changes of `light_formulas.py`, `impostor_check.py` and `fog_check.py` (the dissolve not saturated; `c12.x` and `c12.z` for the light's two ends; the light without its 1; the alpha without the picture; the add outside the scale; eye and place exchanged in the fog; a wrong square in the distance; `b129` as bit 8; the alpha test as "greater or equal"; `sat` of no number; the IEEE product; `fog_check` with the eye in `c9` or with the trees' formula; four changes that make a check unable to fail): each turns at least one test red.

### The C++ mirrors (plan Task 9)

`src/native/render/clay_logic.h`, "Native impostors", between the markers `IMPOSTOR MIRRORS BEGIN` and `END`; pinned by `tests/native/impostor_pins.inc` (`impostor_pins.py --inc`: 16 position, 9 pixel and 6 fog lines, generated from the Python reference alone) in `tests/native/test_clay_logic.cpp` (cases 820 to 829), and held by text in `tests/test_impostor_literals.py`.

- `ImpostorPositionMirror(k, word, constants, pos, place, uv)`: `impostor_position(..., single=True)` term for term in single precision, floating-point contraction off; the two powers through a double `log2` and `exp2` rounded to single, as the reference rounds them. **On the 16 pinned lines it is the reference bit for bit** (largest error 0 of `max(1, |value|)`; bound 1e-4, criterion 1, against the single-precision reference: ledger ruling 26).
- **The degenerate points (ledger ruling 27)**: the console's product rule is not imitated. A quad of no size, a size below 0 or no number, and a corner number above 3 give a position that is no number (a guard); a view whose second row has no part of either axis gives one by IEEE arithmetic (`0 x infinity`). The reference lands those quads at the view's origin; here nothing is drawn, which is the same picture (a quad of no size has no pixel) without a point at the origin. A place that is no number is no position, as in the reference.
- `ImpostorFogCoords` / `ImpostorFogMirror`: `fog_coords` through the existing `FogCoords` with the record's own `c47` (eye), `c110` (axis) and `c111.x, .z, .w`; `o2` = the frame's transmittance table at `s`, `o3` = (1 - `o2`) x its in-scatter table at the angle, both through `FogTableRead` (the tables are the frame's block's: the capture's `fog-table` rule holds the record's slots 16 and 17 to them). (1, 0) with the fog off, before the start and for any value that is no number. Largest error on the 6 pinned lines: 0.
- `ImpostorPixelCore` / `ImpostorPixelMirror`: `impostor_distance`, `impostor_pixel`, then `(colour x o2 + o3) x out_scale` written as the lit draws write the linear target (`(v > 0) ? min(v, 31.875) : 0`; a texel that is no number gives 0), and the kept set. Largest error on the 9 pinned lines: 2.4e-7.
- Literals by name: `kImpostorTwo`, `kImpostorMinusOne`, `kImpostorOne`, `kImpostorTenth` (the pixel shader's `c255.z` as the table has its bits), each held to `impostor_table.inc` by a static assert; 0.5, 0.25 and 4 of the vertex shader only make the corner's number from the vertex index, which a mirror is given, and the pixel shader's 0.5 is read only under `b129`.
- Mutations of the mirrors, each red on a pin or a rule (29 made, 28 red at first; the one that stayed green showed a missing test, since added): `out/native-evidence/distant-trees/t9_mutations.txt`.

### What stands on inference, and what is not checked (pixel and fog)

1. The ground-fog block under `b129` (instructions 16 to 57) is not modelled; a draw with the bit set is refused (spec ruling 20).
2. The alpha test's reference and the registers' values at a `129F` draw other than the five pixel registers of three views: in no capture.
3. The two tables: that vertex slots 16 and 17 are the frame's fog tables (`tf5`, `tf4`) at a `129F` draw is plan Task 6's identity; the check's tables are made ones.
4. The picture's fetch: its filter and its level are the sampler's, not the microcode's (`tfetch2D` with no attribute); the formula takes the texel as fetched.
5. Single precision is not modelled on this side either; the pixel formula has no cancellation of the position's kind.

### Evidence

`out/native-evidence/distant-trees/` (git-ignored): `t4_formulas.txt` (`impostor_check.py formulas`), `t4_mutations.txt` (the tools' mutations, position), `t5_pixel.txt` (`impostor_check.py pixel`, then `fog_check.py --only vertex`), `t5_mutations.txt`. Tests: `tests/test_impostor_formulas.py`; `tests/test_fog_formulas.py` (`test_the_vertex_form`, `test_the_impostors_form_has_no_c8x_and_its_eye_is_c47`).

### The formulas on the captures' own rows (plan Tasks 3 and 6)

Added 2026-10-11, after the discovery captures D1 (`155547`, the bridge), D2 (`155731`, the third spot), D3T (`155929`, by the trees), D4a (`160104`) and D4b (`160251`, the held sunward view): 116 in-scene `129F` rows, 4,744 quads. What the sections above left to a capture:

1. **The word** [measured]. A quad's eight bytes are four big-endian half floats x, y, z, s in that order: `word_source` (1, 0, 3, 2) with `word_endian` 2 stands. The endian is the draw's own fetch constant's (2 in 116 of 116 rows, in the builder's command packet; the device's shadow of fetch slot 31 does not name the storage in any row), the order is the shader's fetch (`"fetches"`: swizzle 1217, yxwz, format 32, stride 2 dwords). The plan's census ("exactly one endian and order decodes every quad to a finite place within 10,000 units and a size in (0, 1000]") does **not** single it out: 36 to 48 of the 96 pairs pass, because x, y, z and s are all small positive numbers here. What does: the replay against the game's depth with the first two components exchanged (below). The draw reserves 16 bytes a quad; the shader reads 8 bytes a quad at an 8-byte stride from the storage's start: its first half. The second half of every storage is older bytes of the same ring.
2. **The registers** [measured], the same in every row of a frame but `c8` and `c113`, which are a tree model's own: `c8` = (a small x, a small y, the raise 0.69 to 0.96, 0); `c113` = (1, about 0.03, about 0.01, 0.887 to 0.984), the picture coordinate's turn; `c46` = the camera's horizontal right axis (unit, third component 0 in every row: the terms of `c46.z` are exercised by made inputs alone); `c47` = the eye, bit-equal to pixel `c19.xyz`; `c72..c75` = a pure projection (0.977419, 1.737634 on the diagonal, `c74` = (0, 0, 1.00002, -0.100002), `c75` = (0, 0, 1, 0)) and `c115..c117` the view's three rows, so that `c72..c75` times the view rows is the scene's `c0..c3` (within 3.8e-8 of the row's largest entry in 116 of 116 rows): the inference of "Deviations" 5 holds; `c110` / `c111` bit-equal to the frame's fog `c66` / `c67`; `c114` = (1.5, 2, 0, 0). Vertex slots 16 and 17 are the frame's `tf5` and `tf4` by base address and content in 116 of 116 rows. Pixel `c12` = (20, 35, 2, w), `c29` = (48, 0.125, 998.4, 25.6), `c31.x` 0.3595 to 0.3653, `c20.w` 7.53 to 9.16; `c31.x` times a `c20.w` of the frame's rows at pitch 320 (2.74 to 2.78: the pictures' own renders) is 1 within 1.4e-7, and `c31.x` times the row's own `c20.w` is not 1.
3. **The alpha test's reference** [measured]: `0x3F008081` = 128 / 255 = 0.50196 in 116 of 116 rows. With the alpha-to-mask offsets `0xAA` (first sample from 0.25, second from 0.75) a tree whose picture alpha is 1 is therefore **absent below 52.02 units** (the dissolve's half is not above the reference), has one sample of two from there to 54 and both from 54: the spec's "one sample from 50" assumed no test.
4. **The bool word** [measured]: the device's shadow (`"pbool"`) is `0x00000100` and the last writer's word (`"pbool_load"`, the engine's at `GpuLoadShaders`, which is what the GPU is given for these shaders: frame-map section 12) is `0x00000110` in 116 of 116 rows; bit 1 (`b129`) is clear in both. A capture that refuses a record on `b129` must test the engine's word, not the shadow.

`impostor_check.py rows` (criterion 1's second half; `out/native-evidence/distant-trees/t6_rows.txt`): every corner of every quad through the microcode and through `impostor_position`, with the row's own bank, its own literals (`"vs_lit"`) and its storage:

| Capture | Rows, quads, corners | Agree | Worst relative error | Above / below (instr 31) | Single against double, of max(1, \|oPos\|) | Fog pairs agree | Pixels agree (absent, dissolving, whole) |
|---|---|---|---|---|---|---|---|
| D1 `155547` | 28, 1,856, 7,424 | 7,424 | 1.7e-15 | 4,240 / 3,184 | 1.14e-5 | 7,424 of 7,424 | 1,856 of 1,856 (0, 2, 1,854) |
| D2 `155731` | 28, 2,152, 8,608 | 8,608 | 1.7e-15 | 4,720 / 3,888 | 1.68e-5 | 8,608 of 8,608 | 2,152 of 2,152 (0, 10, 2,142) |
| D3T `155929` | 20, 278, 1,112 | 1,112 | 1.7e-15 | 928 / 184 | 6.2e-6 | 1,112 of 1,112 | 278 of 278 (0, 14, 264) |
| D4a `160104` | 20, 234, 936 | 936 | 8.9e-16 | 672 / 264 | 6.8e-6 | 936 of 936 | 234 of 234 (0, 14, 220) |
| D4b `160251` | 20, 224, 896 | 896 | 5.3e-16 | 744 / 152 | 3.6e-6 | 896 of 896 | 224 of 224 (0, 14, 210) |

Each of the seven position mutations fails on every capture (`rows`, `components`, `sign`, `corners`, `raise`: every corner; `matrix`: all but 2 of 936 in D4a; `exponent`: 1,992 of 7,424 to 656 of 896). The registers the two machines read on the rows are the table's fourteen and five and the literal registers `c254`, `c255` and `c255`. The fog pairs are the microcode's `o2`, `o3` against `impostor_fog` at the raised place with the frame row's own tables (the fog is begun at every corner: the start is 19 to 20 units and no quad is nearer than 48). The pixel shader runs once a quad on the row's own pixel registers and `"ps_lit"`, with the bools of `"pbool_load"`, the vertex microcode's own interpolators and a made picture. **What this shows and what it does not**: both sides take the same captured registers and bytes, so it shows that the formulas are the microcode on the game's own values; no captured quad is nearer than the dissolve's start (the "absent" branch is exercised by made inputs alone), and that the bytes were read at the right moment and in the right order is the replay's (frame-map section 19, "The formulas against the captures": 98.99% of 52,578 judged pixels of D1 hold the game's own depth, and the game's own colour is the pixel formula's within 0.3% a channel).

**Single precision (ledger ruling 26).** On the captured registers `impostor_position(single=True)` lies at most 1.7e-5 of max(1, |oPos|) from the double reference (the 1.8e-4 to 2.5e-4 of "Single precision" above came from made rows with entries up to 200). The mirrors' pins (`tools/xdk_sigmatch/impostor_pins.py`: 10 captured quads and 6 made ones, 9 pixel lines, 6 fog lines; `t6_pins.txt`) hold the single-precision reference for the position and the double reference rounded to single for the pixel and the fog; `tests/test_impostor_formulas.py` holds every position pin to the microcode within 1e-4.

**Still on inference or not checked**: the terms of `c46.z` on a captured row (none has one); a quad nearer than 48 units; the `b129` block's arithmetic; that `ucode_eval` is the console.

## Water (sub-project 13)

Native renderer sub-project 13 (water), phase 13a, plan Tasks 2 and 3, 2026-10-12. The lake's surface as closed formulas, each checked against its microcode: vertex shader `BEAD84BD72072E0E` (17 instructions, numbered 3 to 19) and pixel shaders `A17D8AEC3A817D45` (the surface, 80 instructions numbered 10 to 89) and `F6D98C7B4D98438B` (the opaque twin; the two dumps differ in instruction 14 alone). Nothing here was run in the game and no captured row was read: the rows are plan Task 4's.

### Result

| What | Function of `light_formulas.py` | State |
|---|---|---|
| `oPos`, `o0.xyz`, `o1`, `o3.xy` of `BEAD` | `water_vertex(p, pair, c)` | closed on made inputs: 2,000 of 2,000 made draws, largest relative difference 4.6e-15; four mutations each fail in 400 of 400 |
| The alpha of `A17D` and `F6D9` | `water_alpha(m, c141)` | closed on made inputs: 2,000 of 2,000 made pixels for each shader, largest difference 0 (the same three operations); three mutations each fail |
| What becomes of a pixel: killed, skipped, shaded | `water_kept(alpha, c141, kind)` | closed with the alpha; every fate reached: `A17D` 605 killed, 830 skipped, 565 shaded; `F6D9` 1,395, 221, 384 |
| `F6D9` draws nothing | `water_opaque_dead(c141)` | part (a) of criterion 2, on the microcode: 2,000 of 2,000 made pixels killed at the masks 0, 1 and their own, 500 of them at the margin itself; 256 of 256 levels of a `k_8` mask with the captured `c141`; the control not killed in 500 of 500 ("The dead draw" below: what it assumes) |
| `oC0` of `A17D` | `water_pixel(q, P, coord_a, coord_b, m, c, tex, bools)` and its parts `water_normal`, `water_lookup`, `water_shadow_at`, `water_base`, `water_mix`, `water_sun`, `water_fog` | closed on made inputs: 2,000 of 2,000 made pixels (283 killed, 256 skipped, 1,461 shaded), largest relative difference 6.3e-14, every fetch's coordinates included; sixteen mutations and five literals each fail; the glint's sign and the saturated mix factor settled on the interpreter's registers |
| What the shaders read | `WATER_SHADER["vs_read"]`, `["ps_in"]`, `["ps_read"]`, `["ps_read_under"]`, `["ps_slots"]`; `water_check.UNREAD` | the interpreter's own account, one component changed at a time ("What the shaders read" below) |

(`water_check.py formulas`, `dead` and `used`, seed 1: `out/native-evidence/water/t2_formulas.txt`, `t2_dead.txt`, `t2_used.txt` for plan Task 2 and `t3_formulas.txt`, `t3_used.txt`, `t3_settle.txt` for plan Task 3. Both sides are the same arithmetic in double precision on inputs rounded to single precision, which is why they agree to rounding; since plan Task 3 every compared number is held to `1e-9 * max(1, |value|)`, far inside spec Design 2's bounds ("The check of the pixel" below), and it is the mutations that show a bound can tell.)

### The vertex shader `BEAD`

With `p` the fetched position (instr 3: `vf0`, three floats at dword 0 of a vertex of 5 dwords, the fourth component 1) and `pair` the two floats at dword 3 (instr 4):

| Instr | What it makes | In the formula |
|---|---|---|
| 5 to 8 | `oPos.x = c0 . (p, 1)`, `.y` with `c1`, `.z` with `c2`, `.w` with `c3`; each row's four products summed in the order z, x, y, w | `pos`: nothing is added to the position (no waves, no time) |
| 9 | a vertex fetch of slot 16 (the shore mask) at `pair` | not in the formula: its value reaches `o3.z` alone |
| 10, 16, 18, 19 | `o0.w = sat(c113.x + c255.x - (c9.z - p.z) / length(c9 - p))` | not in the formula: neither pixel shader reads `o0.w` |
| 11 | `o0.xyz = p` | `place` |
| 12 | `o1.xy = p.xy * c117.xy + c115.xy` | `coord[0:2]` |
| 13 | `o1.zw = p.xy * c118.xy + c116.xy` | `coord[2:4]` |
| 14, 15 | `o2` likewise with `c121`, `c119` and `c122`, `c120` | not in the formula: neither pixel shader reads `o2` |
| 16, 17 | `o3.xy = pair`; `o3.z = c255.x -` the mask fetched at instr 9 | `mask`; `o3.z` is read by neither pixel shader |

The height `p.z` reaches no coordinate. The registers the formula takes are `c0` to `c3` whole and the first two components of `c115` to `c118` (`WATER_SHADER["reg"]`, `["vs_read"]`); the shader also reads `c9`, `c113`, `c119` to `c122` and its literal `c255.x`, all into `o0.w`, `o2` and `o3.z`. That neither pixel shader reads those is the interpreter's own account and not a reading: `water_check.read_interpolators` changes each component of each interpolator register in turn on 16 shaded pixels, and the pixel moves for `r0.xyz`, `r1.xyzw`, `r3.xy` and `r4.xy` (the position register) and for nothing else, with both shaders (`t2_used.txt`). The native program therefore needs no vertex fetch of the mask, no `c9`, `c113` and no `c119` to `c122`.

### The alpha and the fate (`A17D` and `F6D9`, instr 10 to 18)

| Instr | What it makes | In the formula |
|---|---|---|
| 10 | `m` = slot 1 at `o3.xy`, the first component the fetch delivers | the argument `m` of `water_alpha` |
| 11 | `1 - m` | |
| 12 | `ramp = sat(-(1 - m) * c141.y + c141.x)` | |
| 13 | `alpha = 1 - ramp`, kept in `r4.w` to the end (instr 89 writes it out) | `water_alpha` |
| 14 | `A17D`: `flag = (alpha == 1)`; `F6D9`: `flag = (1 > alpha)`; beside it `r3.w = sat(max(c131.x, c131.x))` (the mix factor of the pixel, plan Task 3) | `water_kept`: `kind` "surface" or "opaque" |
| 15 | the predicate = `flag != 0`; `r2.xyz = (-abs(P.x) > 0)`, which is 0 whatever `P.x` holds | |
| 16 | under the predicate: `kill_gt c254.x, c255.y`, the literals' 0 > -1: the pixel is discarded | "killed" |
| 17 | `flag2 = (alpha > c141.w)` | |
| 18 | `setp_ne_push`: the predicate of everything to instr 76 = `flag2 != 0` (and the literal `c254.x` being 0); `r2.w` = 0 where it holds, else 1 (instr 77 asks for it again) | "shaded", else "skipped" |

A skipped pixel runs nothing predicated, jumps at instr 77 and leaves through instr 88 and 89 with the colour `r2.xyz * c20.w` = 0 (instr 15) and the alpha `alpha`: the check holds its colour to exactly 0. So `A17D` kills where the alpha is exactly 1 (the ramp at or below 0) and shades where it is above `c141.w`; `F6D9` kills wherever the alpha is below 1. With the captured `c141` = (2.003, 2, 0.0015, 0.0588) the alpha is `1 - sat(0.003 + 2 m)`: 0.997 in open water (`m` = 0), 0 from `m` = 0.4985 on. The saturate of no number is 0 in the interpreter: a mask sample that is no number gives alpha 1, which `A17D` kills and `F6D9` keeps.

### The dead draw (spec ruling 12, criterion 2 part (a))

**The claim.** If `min(c141.x, c141.x - c141.y) >= 1e-6` and `|c141.x|`, `|c141.y| <= 4` (`water_opaque_dead`), then `F6D9` discards every pixel whose mask sample is a number in [0, 1].

**Why.** By the table above `F6D9` kills exactly where `alpha < 1`, that is where the saturate's argument `X(m) = c141.x - (1 - m) * c141.y` is above 0. `X` is linear in `m`: `X(m) = (1 - m) * (c141.x - c141.y) + m * c141.x`, a weighted mean of its two ends `X(0) = c141.x - c141.y` and `X(1) = c141.x` for `m` in [0, 1]. Both ends are at least the margin, so `X(m) >= 1e-6 > 0` for every such `m`.

**On the microcode** (`water_check.py dead`, `t2_dead.txt`): 2,000 made pixels of `F6D9` whose `c141` satisfies the condition (`c141.y` anywhere in [-4, 4], `c141.x` from `max(0, c141.y) + 1e-6` up to 4, both single-precision numbers), each run at three masks, 0, 1 and its own (one of the 256 levels a `k_8` texture holds, or any value in [0, 1]): the interpreter kills all three in 2,000 of 2,000. 500 of the trials stand at the margin itself (`c141.x` the smallest single-precision number for which the condition holds for its `c141.y`). All 256 levels with the captured `c141`: 256 of 256 killed. The control, `c141.x - c141.y <= 0` with a mask of 0 (alpha exactly 1): not killed in 500 of 500, so the check can tell a live draw.

**In single precision.** The interpreter is double precision; the console computes in single. The three steps of instr 11 to 13 were therefore also made in single precision for the same 2,000 trials and three masks, with the multiply-add both rounded twice and fused: the alpha is below 1 in 2,000 of 2,000. By hand: the rounding of `1 - m` (at most 3.0e-8, times `|c141.y| <= 4`: 1.2e-7), of the product (at most 1.2e-7 at magnitudes up to 4) and of the sum (at most 2.4e-7 at magnitudes up to 8) together stay below 4.8e-7, so a true argument of 1e-6 is computed as at least 5.2e-7, and `1 - ramp` rounds to 1 only for a ramp of 3.0e-8 or less. That is what the margin 1e-6 and the bound 4 stand for; outside the bound the function says "not dead".

**What the proof assumes** (each is somebody's to hold):
1. *The mask sample is a number in [0, 1].* True of an unsigned normalised `k_8` texture under any filter that is a weighted mean of texels; not true of a fetch constant that delivers the texture signed, biased or with an exponent. The record's rule on the mask's fetch constant (plan Task 9) and the masks' census (part (c), plan Task 4) hold it. A sample that is no number gives alpha 1 and is NOT killed.
2. *`c141` is one value for the whole draw and the literals are the table's* (`c254.x` = 0, `c255.y` = -1, `c255.z` = 1: the kill is the comparison of two literals under the predicate). The rows (part (b), plan Task 4) and the record's literal rule (plan Task 9) hold it.
3. *The interpreter is the console* for `tfetch2D`, `subsc`, `mad_sat`, `sgt`, `setp_ne`, `kill_gt` (the SDK's `ucode.h` semantics, as `ucode_eval.py` states them), and a killed pixel writes neither colour nor depth.
4. Nothing here says what the draw's state does (the alpha test, depth): the research measured the game's depth copy at the lake bed in 17,840 of 18,536 footprint pixels [measured, `water-research.md`].

### The check (`water_check.py`)

One side is the dump's text run by `ucode_eval.Machine` on a made input; the other is the formula on the same input, through nothing of the interpreter's (the vertex is decoded from the buffer's bytes by `struct`, the machine's fetch goes through `capture_replay.element`). The formulas were written from the dumps; no term was fitted to the interpreter's output, and none had to be changed after the first run. Made inputs, the ranges and the reason for each are the module's text. Every register component that no formula reads is drawn at 1e21 to 1e22 with either sign (the third and fourth components of `c115` to `c118`; for the pixel shaders every unread component of the 24 registers), and every register outside the tables is random: a formula that read one would differ.

Mutations, each of which must make trials disagree (`tests/test_water_formulas.py`, `test_mutations_fail`; the counts of `t2_formulas.txt`, of 400 trials): vertex: `c116` for `c115` 400; `c118` for `c117` 400; the coordinates from `p.xz` 400; `c0` and `c1` exchanged 400. Alpha, `A17D` / `F6D9`: `c141.x` and `c141.y` exchanged 330 / 168; `m` for `1 - m` 275 / 88; the other shader's comparison 400 / 400.

One departure from the plan's made inputs, said here because a reader of the plan would look for it: the plan draws the threshold `c141.w` at or above 1 in 15 of 100 trials of one family of three. `F6D9` then "skips" (alpha exactly 1 under a threshold of 1 or more) in about 2.5% of the trials, below the tenth the test asks of every fate. The share is 40 of 100 in two families; no formula changed.

### The pixel of `A17D` (plan Task 3)

A shaded pixel (alpha above `c141.w`) runs instr 19 to 89. The interpolators are `P` = `o0.xyz` (the place), `o1` (the two coordinates), `o3.xy` (the mask's coordinate) and `q`, the position register `r4`, of which the magnitude is read. Literals: `c254` = (0, 2, 0, 0), `c255` = (0.5, -1, 1, -2). Block by block:

| Instr | What it makes | Function |
|---|---|---|
| 19, 20 | `a` = slot 2 at `o1.xy`, `b` = slot 2 at `o1.zw`: the first two components the fetch delivers | the callback `tex["normal"]` |
| 24, 25 | `s = (2a - 1) + (2b - 1)`, two components | `water_normal` |
| 33 to 39 | `z = sqrt(abs(1 - dot(2a - 1, 2a - 1))) + sqrt(abs(1 - dot(2b - 1, 2b - 1)))` | `water_normal` |
| 27, 35, 36, 40 to 43 | `N = n * rsq(abs(n . n))` with `n = (s.x / 2, s.y / 2, (c128.x / 2) * z)`; the squares summed in the order z, x, y | `water_normal` |
| 21 to 23, 26 to 28 | `u = (abs(q.x) * c0.z + c0.x - 1/2) * c31.x + 1/2`, `v = (abs(q.y) * c0.w + c0.y - 1/2) * c31.y + 1/2` | `water_lookup` (`water_screen` is `(abs(q.x) * c0.z, abs(q.y) * c0.w)`) |
| 21, 26, 29, 30 | slot 13 at `(u + (c129.x / 2) * s.x, v + (c129.y / 2) * s.y)`; slot 14 at `(u + (s.x / 2) * c130.x, v + (s.y / 2) * c130.y)`: the sum `s`, not the unit normal | `water_lookup` |
| 31, 32 | the two fetches, three components each (`tfetch2D r3.xyz_, r7.yx, tf13`: the register holds (v, u) and the fetch reads (u, v)) | `tex[13]`, `tex[14]` |
| 37, 38, 41, 42, 51 | `E = c19 - P`; `d = sqrt(abs(E . E))`; `V = E * rsq(abs(E . E))` | `water_base` |
| 44 to 46 | `F = sat(1 - V . N + c116.x)` | `water_base` |
| 39, 40, 43 to 47 | `base = (c126 - c127) * F + c127` | `water_base` |
| 34 | the shadow factor `k` = 1 (`maxs r1.w, c255.zz`) | `water_pixel` |
| 48 to 50 (bool `b137`) | `k = tf8 * c139.x + c139.y`, slot 8 read at `(abs(q.x) * c0.z + c0.x, abs(q.y) * c0.w + c0.y)`: the screen place itself, without the scale about the centre and without the normal | `water_shadow_at`, `tex[8]` |
| 14 | `share = sat(max(c131.x, c131.x))` | `water_mix` |
| 51 | `t = sat(F + c117.x)` | `water_mix` |
| 54, 55 | `far = (r13 * k - base) * share + base` | `water_mix` |
| 52, 53, 56, 57 | `near = ((r13 * k - r14) * t + (r14 - base)) * share + base`: the factor `k` is on slot 13 alone | `water_mix` |
| 58, 59 | `w = sat(c140.x * d)`; `colour = w * (far - near) + near` | `water_mix` |
| 60 to 65 (bool `b136`) | `N2 = n2 * rsq(abs(n2 . n2))` with `n2 = (N.x, N.y, c136.x * N.z)` | `water_sun` |
| 66, 67 | `2 (N2 . L)` with `L = -c142` (`c142` is not normalised) | `water_sun` |
| 68 | `W = L - 2 (N2 . L) N2`, which is `-R` for `R = 2 (N2 . L) N2 - L` | `water_sun` |
| 69 | `cosine = sat(-(V . W))` = `sat(+(V . R))` | `water_sun` |
| 70 to 72 | `power = exp2(c138.x * log2(cosine))` | `water_sun` |
| 72 to 75 | `colour += (c143.x * c137.x * power, c137.x * power * c143.y, c137.x * power * c143.z)` | `water_sun` |
| 76, 77 | the fog begins where `d - c67.x > 0` (and only for a shaded pixel: `r2.w` of instr 18) | `water_fog` |
| 78 to 87 | `a = sat(-V . c66)`, `s = c67.w * (d - c67.x)`, `t = sat(c67.z * (d - c67.x))`; `S` = slot 4 at `a`, `T` = slot 5 at `s`; `Fg = c68 + t * (S - c68)`; `colour += c28.x * (colour * T + (1 - T) * c28.y * Fg - colour)` | `water_fog` (= `light_formulas.fog`: `test_the_fog_block_is_the_plain_form`, and `fog_check.py` as before) |
| 88, 89 | `oC0 = (colour * c20.w, alpha)` | `water_pixel` |

`F6D9` runs the same instructions on the pixels it keeps (alpha exactly 1): `water_pixel(..., kind="opaque")`.

**The two readings the plan left to the machine** (`out/native-evidence/water/t3_settle.txt`, the interpreter's registers after each instruction of a made pixel):
1. *The glint's sign: the plan's reading stands, the spec's does not.* After instr 68 the registers hold `L - 2 (N2 . L) N2` (to the last digit of the hand value), and instr 69 gives 0 where `+(V . R)` is -0.736 and `-(V . R)` is +0.736. The cosine is `sat(+(V . R))` with `R = 2 (N2 . L) N2 - L`, `L = -c142`: the eye sees the glint where it stands in the mirror direction of the light. The spec's `sat(-(V . R))` (Design 2, "Sun") is wrong. The mutation "`c142` with the other sign" fails in 127 of 400 trials; `test_the_sign_of_the_glint_in_the_microcode` holds the machine's own colour to it.
2. *The mix factor is saturated: the plan's reading stands.* Instr 14 `maxs_sat r3.w, c131.xx` gives 0.5, 1, 1 and 0 for `c131.x` = 0.5, 1, 7 and -2. The spec's bare `c131.x` (Design 2, "Mix") differs above 1 and below 0; 0.5 in every captured row.

**What else the interpreter shows that the spec's and the plan's reading did not say** (none contradicts a formula of the plan; each is now in a function and under the check):
- The shadow factor of `b137` is read at the screen place itself, `(|q| * c0.zw + c0.xy)` (instr 48, 49), and multiplies slot 13's colour alone, not slot 14's (instr 53, 54).
- `c142` is not normalised by the shader (instr 66 to 68): a longer `c142` gives a larger cosine.
- The lookup's distortion uses the sum `s` of the two samples (instr 26, 29), as the spec says; the base colour and the glint use the unit normal.
- The magnitude of the position register is taken (instr 22: `r_abs[4]`); its signs reach nothing.
- A skipped pixel leaves with a colour of exactly 0 and its alpha (instr 15, 77, 88, 89); the output's alpha is never touched by the fog or the scale.
- Spec's point 6 of "What the research has differently" holds: `c128.x` multiplies the third component before the normalisation (instr 36, 40), and with 0.3 the normal tilts further (`test_the_factor_c128x_shrinks_the_third_component_and_so_tilts_the_normal`). Point 7 holds in each part: the second normal with `c136.x` (instr 60 to 65), `c0.xy` in the lookup (instr 21 to 23, 27) and again at the shadow fetch (48), the position register, and that the vertex shader's mask fetch, `o0.w` and `o2` reach neither pixel shader (the interpolators' account above).

### What the shaders read

By the interpreter alone (`water_check.read_components`, `read_interpolators`; `t3_used.txt`): each component of each register the dump uses is changed in turn on 64 shaded pixels with both bools set and the fog begun (and on 16 pixels of the alpha check, where a change of `c141` moves the fate), and it counts as read when the kill, `oC0` or any fetch's slot or coordinates differ in any of them.

- Both pixel shaders use 24 registers beside their literals: `c0`, `c19`, `c20`, `c28`, `c31`, `c66`, `c67`, `c68`, `c116`, `c117`, `c126` to `c131`, `c136` to `c143` (`WATER_SHADER["ps_reg"]`).
- Read, 50 components (`WATER_SHADER["ps_read"]`): `c0.xyzw`, `c19.xyz`, `c20.w`, `c28.xy`, `c31.xy`, `c66.xyz`, `c67.xzw`, `c68.xyz`, `c116.x`, `c117.x`, `c126.xyz`, `c127.xyz`, `c128.x`, `c129.xy`, `c130.xy`, `c131.x`, `c136.x`, `c137.x`, `c138.x`, `c139.xy`, `c140.x`, `c141.xyw`, `c142.xyz`, `c143.xyz`: the plan's list, component for component.
- Not read, 46 components (`water_check.UNREAD`): `c19.w`, `c20.xyz`, `c28.zw`, `c31.zw`, `c66.w`, `c67.y`, `c68.w`, `c116.yzw`, `c117.yzw`, `c126.w`, `c127.w`, `c128.yzw`, `c129.zw`, `c130.zw`, `c131.yzw`, `c136.yzw`, `c137.yzw`, `c138.yzw`, `c139.zw`, `c140.yzw`, `c141.z`, `c142.w`, `c143.w`. They hold garbage in the captures (`c129.z` is no number, `c139.w` and `c140.w` are 1.9e29): `water_pixel` touches none (`test_no_unread_component_reaches_the_pixel` makes them all no number), and no later mirror or HLSL may.
- Under a bool alone (`WATER_SHADER["ps_read_under"]`): with `b136` clear `c136`, `c137`, `c138`, `c142`, `c143` are no longer read; with `b137` clear `c139` is not.
- Slots, in the order fetched (`WATER_SHADER["ps_slots"]`): 1 (the mask), 2 twice (the normal map), 13, 14, 8 under `b137`, 4 and 5 beyond the fog's start.
- Interpolators: `r0.xyz`, `r1.xyzw`, `r3.xy`, `r4.xy` (`WATER_SHADER["ps_in"]`).

### The degenerate points, and the console's product rule

The formulas multiply as the console does (a zero factor gives +0 whatever the other factor is: `ucode_eval`'s `zero_product`, `_imp_mul`). **Four** points depend on it (three as first written; the fourth by the review of Tasks 1 to 5, I2, and ledger ruling 34), each made in 50 of 2,000 trials (`PIXEL_KINDS`): a flat normal with `c128.x` = 0 (`N` = 0 * infinity = 0); a pixel at the eye (`V` = 0); a power `c138.x` of 0 on a cosine of 0 (0 * -infinity = 0, so the power is 1 and the sun term is the whole `c137.x * c143`); and `c136.x` = 0 on a flat pair of samples under `b136` (the sun's second normal `N2 = (0, 0, c136.x * N.z)` is 0 * infinity = 0; what is mirrored is then `-c142` itself and the cosine `sat(V . c142)`: no glint with the eye and the sun both above the surface, where an IEEE machine has no number). Without the rule in the interpreter 168 of 2,000 trials disagree, every one of them among those 200 (48, 49, 50 and 21 of the four kinds; in the others the no-number passes through a saturate, which gives 0, and the colour comes out the same) (`out/native-evidence/water/t4_formulas.txt`). An HLSL program computes by IEEE, and the water is blended, so a no-number clamped to 0 would be a black pixel at alpha 0.997. What holds it, by ledger ruling 34: (1) the record's rule keeps `c128.x`, `c136.x` **and** `c138.x` strictly above 0 (`water_table.inc`, kind 3: in [1 / 65,536, 65,536]; a negative power would be infinite on a cosine of 0); (2) the HLSL writes the console's rule at the three reciprocal-square-root sites (`x == 0 ? 0 : x * rsqrt`: the normal, the view vector, the second normal) and the power as `cosine > 0 ? exp2(c138.x * log2(cosine)) : 0`; (3) a colour or alpha that is still not finite is a **discard**, never a clamp. `tests/test_water_hlsl.py` holds (2) and (3) on the header's text from plan Task 12 on (its tests skip, by name, until then); `water_pins.py` pins the reference at the four points.

### Single precision

Every function takes `single=True`: the inputs it reads and every operation's result rounded to single precision, a multiply-add as a rounded product and a rounded sum. That is the reference a native mirror is pinned to (sub-project 12's ledger ruling 26); the double form is what is checked against the microcode. On the made pixels that are shaded and not degenerate the single form lies from the double one, in `|difference| / max(1, |colour|)`: without the sun term (513 pixels) median 1.7e-7, 99th percentile 1.8e-5, largest 2.5e-4; with it (799 pixels) median 2.2e-7, 99th percentile 1.3e-5, largest 5.1e-5. Reported, not a check: a single-precision mirror held to the DOUBLE reference at 1e-5 would fail on about one made pixel in a hundred. Whether the console fuses its multiply-add is not known; the reference does not.

### The check of the pixel

2,000 of 2,000 made pixels agree (`t3_formulas.txt`): 283 killed, 256 skipped, 1,461 shaded; of the shaded 907 with the sun term (its cosine above 0 in 697, the term above 1% of the colour in 455), 342 with the shadow factor, 762 near and 699 far of the range `1 / c140.x`, 862 fogged; the largest relative difference of a colour 6.3e-14. Compared in every trial: the fate, the alpha, the colour, the order of the fetched slots and every fetch's coordinates (the two lookups, the shadow mask's place, the two fog coordinates) against the formula's. `pixel_floors` holds the counts to floors (each fate, each bool in both states, each side of the range and of the fog's start at a tenth of the trials; the cosine above 0 in a third of the sun trials).

The bound is `1e-9 * max(1, |value|)` for every compared number, not the spec's `1e-5 * max(1, |value|)` (and 1e-3 relative with `b136` set, 1e-6 for the alpha): both sides are the same double arithmetic, and with the spec's bounds the mutation of the power went unseen in 384 of 400 trials (16 failed; 38 with the bound as it is now on the same made inputs, 81 once the made suns were aimed at the surface's own mirror direction). The tighter bound implies the spec's wherever a colour is above 1e-6.

The sixteen mutations, each of which must fail (of 400 trials; about 297 of them are shaded): `c126` and `c127` exchanged 268; the lookup scaled about 0 297; `c130` for `c129` at slot 13 261; both lookups at (v, u) 297; `c128.x` left out 199; `c116.x` with the other sign 201; `c131.x` left out 268; the blend's ends exchanged 188; `c140.x` by 1% 89; slot 14 left out of the near branch 94; `c136.x` left out 99; `c138.x` by 1% 81; `c142` with the other sign 127; `c137.x` left out 122; `c28.x` left out 171; `c20.w` left out 297. A mutant is `water_pixel` itself on a changed input (a register, or a texture read at another place), so it is a mutant of the function that is checked. The literals are held by their place: `half`, `one`, `two`, `minus_one`, `minus_two` each changed by 1% in the formula alone fail (297, 344, 297, 168, 297 of 400) and changed in the formula and at their place in the machine's constants agree again (0); `zero` cannot be scaled.

### What stands on inference, and what is not checked

- That `ucode_eval` is the console (it is the repo's reading of the SDK's `ucode.h`; double precision; no denormal flush). [inferred]
- That the position register is `r4` and holds the pixel's integral position: the shaders read `|r4.xy|`; the SDK rounds the position down before it hands it over (`dxbc_translator.cpp`, the `OpRoundNI` of `in_reg_ps_position_`) [code]; which register a draw generates it into is the draw's state and is not in the dump. [inferred until a row shows it]
- The bits of `b136` and `b137` in the pixel bool word (`0x100`, `0x200`: bit `N & 31`, as `IMPOSTOR_B129`): by the SDK's layout; plan Task 4 reads both bool members of every row. [inferred]
- Which component of the mask's and the normal map's texel the fetch delivers first: the formulas take "the first component" and "the first two"; what the fetch constants of slots 1 and 2 make of the stored texel is plan Task 5's. [unknown here]
- The captured rows: nothing in this part was held to one. Plan Task 4. [not run]
- The block under bool `b137` (the shadow factor, `c139`, slot 8) is closed on made inputs alone: the one captured row read for the made ranges has the bit clear (`pbool_load` `0x00000110` in `out/native-evidence/water/b_water_d1_f1.txt`), so no captured row will exercise it unless plan Task 4 finds one. [microcode only]
- The centres of the made ranges are the registers of one row of one capture (`b_water_d1_f1.txt`; `c129.xy` reads 0.4208 there, where the plan's text has 0.84); the bounds over the six captures are plan Task 4's. [measured on one row]
- Whether the console fuses a multiply-add; the single-precision reference does not. [unknown]

### The formulas against the captured rows (plan Task 4)

`water_check.py rows` (`out/native-evidence/water/t4_rows.txt`) [measured]: every `A17D` and `F6D9` row of both frames of the five captures that hold water rows (`155547`: 48 rows; `155731`: 44; `221449`: 48; `155929` and `160104`: 12 each; **`160251`, the held sunward view, holds none**, where the plan expected six a frame). For each row: every vertex of its dump through the vertex microcode and `water_vertex` (the row's own bank); 200 points inside its mesh, each as a pixel whose interpolators are the place, the vertex formula's two coordinates and the mask's coordinate there and whose position register is the floor of its screen place, through the pixel microcode and `water_pixel` with the row's own `"pfull"`, its literal table `"ps_lit"`, the bools of `"pbool_load"` and **one sampler for both sides** that reads the capture's dumps (the mask, the normal map's base level, the row's own slot-13 picture, the slot-14 dump, the frame's two fog tables). Bounds as on made inputs: 1e-9.

| | Result |
|---|---|
| vertices | 21,784 of 21,784 agree (7,100 + 7,136 + 7,100 + 224 + 224); worst 1.0e-14 |
| pixel positions | 32,800 of 32,800 agree (61 of them outside the picture); worst 3.0e-14 |
| `F6D9` | **killed at 16,400 of 16,400** positions (criterion 2 on the rows' own registers and masks) |
| `A17D` | killed 0, skipped (land) 6,117, shaded 10,283; every shaded position far (`c140.x * distance` at least 2.06) and fogged |
| a component a formula reads that is not finite | **0 rows of 164** (counted apart; the 46 unread components are not looked at) |
| the single-precision reference against the double one | at most 2.1e-5 of max(1, \|colour\|) (the mirror's pins are the single reference's) |
| mutations | the four vertex and three alpha mutations fail in every capture; of the sixteen pixel mutations twelve fail somewhere and **four cannot fail on these rows**, each for a measured fact the command prints: `distort` (`c129.xy` and `c130.xy` are equal bit for bit in every row), `range` and `slot14` (the blend's weight is 1 at every position: no captured water lies within 20 units), `fog` (`c28.x` is exactly 1). Of the four sun mutations `sun_sign` fails in every capture (183 to 335 positions: the opposite sun does glint there); `second_normal`, `power` and `sun_scale` fail at the trees and on the sun side (165 to 240 of 1,200 positions) and at one position of `221449` (largest term 6.6e-4), and cannot at `155547` and the third spot, where no position has a sun term above the bound (largest 1.7e-17 and 7.3e-31): the command says so there and does not pass it over |

**What this check is not** (the review of sub-project 12's phase 12a): both sides read the same captured inputs, so it shows that the transcription holds on the game's own registers, garbage and literal table, and that no read component is garbage. It cannot show that a texture's bytes are read in the right order, that the picture is the frame's, or that the position register is `r4` with integral positions: `water_replay.py` against the game's own scene is what holds those (frame-map section 20, "The replays").

### The dead draw on rows and masks (criterion 2, parts (b) and (c))

`water_check.py dead <captures>` (`t4_dead.txt`) [measured]: (b) `water_opaque_dead(c141)` holds for **82 of 82** `F6D9` rows (`c141.xyw` = (2.00300002, 2, 0.0588235296) in all 164 water rows; the unread `c141.z` has two values); (c) for each of the 41 dumped masks (12, 11, 12, 3, 3) with its row's `c141`: **0 texels give an alpha of 1** in double precision or in single with the multiply-add rounded twice or fused; the largest alpha is 0.997 in every capture. Both parts hold **under the water's fetch rule** (ledger ruling 33): `water_textures.fetch(fc, "k8")`, the twin of `render::WaterTextureFetch`, now refuses a constant whose delivered component is signed, biased or gamma, whose number format is integer or whose exponent adjust is not 0 (the C++ side is the next builder's), and the command holds every captured slot-1 constant to it (41 of 41 inside). That rule is what makes assumption 1 of part (a), "the mask's sample is a number in [0, 1]", a property of an admitted draw instead of a hope.

### The mirrored camera (criterion 3)

`water_check.py views` (`t4_views.txt`) [measured]: **32 of 32** reflection views (both frames of five captures: 8, 8, 8, 4, 4) equal `diag(c31.x, c31.y, 1, 1) * M * Mirror(h)` within 1e-5 of max(1, \|entry\|) in every entry, the largest error 1.4e-6 (one single-precision step of a row's fourth entry); `M` is the main scene's `c0..c3` of the frame's first water row, `c31` that row's pixel register, and `h` **the height of the meshes that sample the view's picture** (the k-th view's picture is the k-th smallest slot-13 base; heights 20.1024, 40.474, 27.5203, 40, 32.0827 from the vertex dumps, every mesh flat within 1e-3). The spec author's script took `h` from the view's own matrix; this one does not. Mutations, each failing in every view: `factor` (the factor left out), `plane` (the mirror about z = 0), `height` (`h` + 0.01). It is not circular: one side is the game's own view rows.

### Bools, states, bounds, the table and the pins

- **Bools** (`t4_bools.txt`): both dumps name `b136` and `b137` and no other. In 164 of 164 rows the device's shadow `"pbool"` is `0x00000100` and the last writer's `"pbool_load"` `0x00000110`: **`b136` (bit 8) set and `b137` (bit 9) clear in both**, so these rows cannot tell the two words apart. The formulas take `"pbool_load"` (the word the GPU was given: spec Design 4). That bit N & 31 is bool N is the SDK's rule (`IMPOSTOR_B129`), not shown by a water row; the replay shows the consequence (with `b137` taken as set the reflection would be scaled by the shadow mask). **The `b137` block has no captured row.**
- **States** (`t4_states.txt`), one value in 82 of 82 rows of each shader: `F6D9` depth `0x00708766`, blend `0x00010001`; `A17D` depth `0x00708746`, blend `0x07060706`; both: colour control `0x0000000C`, cull `0x00018006`, colour mask `0x0000000F`, and the alpha reference **`0x3D70F0F2`** = 0.0588235334, which is one unit in the last place ABOVE 15 / 255 as a single (`0x3D70F0F1`, the value `c141.w` holds). The plan had `0x3D70F0F1`. So the shader's threshold `c141.w` (shaded against colour 0) and the state's reference (drawn against not) are two numbers even in the captures: only an alpha of exactly `0x3D70F0F2` is shaded and not drawn.
- **Bounds** (`t4_bounds.txt`): the smallest and largest of each of the 58 read components; every one inside its kind's sanity bound (`gen_water_table.py`: a place within 1e6, a factor within 65,536, the three zero-product registers in [1 / 65,536, 65,536]; fitted to no capture). `c0.xy` is 0, `c28.xy` 1, `c31.xy` 2/3, `c128.x` 0.3, `c131.x` 0.5, `c138.x` 128, `c140.x` 0.05 in every row; `c116.x` 0.104 to 0.137, `c117.x` -0.427 to -0.421, `c129.xy` = `c130.xy` 0.421 to 0.427, `c136.x` 0.273 to 0.279, `c20.w` 7.85 to 9.16.
- **The table** `src/native/capture/water_table.inc` (`gen_water_table.py`, `--check`): the three hashes, 4 vertex and 24 pixel registers with their read masks (8 and 50 components) and kinds, the two literal registers, the two bools, the four fetch slots (1, 2, 13, 14). No C++ includes it before plan Task 9.
- **The pins** `water_pins.py` (`t4_pins.txt`): vertex, alpha, pixel, view and dead lines; captured rows and pixels with their garbage as captured, and made ones for the near branch, the shadow factor, the glint, the fog's start, land, the kill and the four zero-product points; every output is the **single-precision** reference's (`single=True`). The alpha lines carry `c141.w` and the state's reference apart, with one line skipped-and-drawn and one shaded-and-not-drawn.

### The reflection views: the far terrain, the blur, the export pass (plan Task 15)

Frame map section 20, "The reflection views", has the measurements; this is what the formulas are.

- **The far terrain of a view** (`5003` / `7DDA`) is "Far terrain" above (`terrain_far`, then `fog`, then `c20.w`) with the main scene's registers but two: `c19.xyz` is the main eye mirrored in the view's plane, `(e.x, e.y, 2 h - e.z)` (`water_check.mirrored_eye`), and `c20.w` is 1. The fog's distance is therefore the reflected path's, and a view's picture is the linear colour without the exposure, saturated to 8 bits by the resolve. `c11.x` is the main scene's far rows' (0.4704 in the captures), not 1. The rows cull the back with the winding turned (`0x2205` = `0x00218006`): what is drawn of the ground is its upper side as the mirrored eye sees it (`water_replay.facing`), and nothing below the plane (the views' clip registers are in no capture; the pictures show the cut: frame map). `water_check.py view-terrain`: the microcode on each row's own registers against `terrain_replay.lit_far_colour` and `fog` on the native pass's inputs, 358 of 358 rows, within 3.0e-8.
- **The blur** (`B2D9` / `F186`, `light_formulas.water_blur7`): `oC0.xyz = c255.x * sum over k = 116..122 of tf0(o0.xy + c_k.xy)`, `oC0.w` = 0; `o0.xy` is `(0.5 x + 0.5, 0.5 - 0.5 y)` of the rectangle's position (vertex instr 5 to 7); slot 0 is the scratch picture, linear, clamped. Captured: `c255.x` = 0.142857149, the offsets the centre and six points at 2 / 256 from it, 60 degrees apart (`WATER_BLUR_TEXELS`). A pixel's coordinate is its integral position over 256, so a tap lies on a texel corner (`water_replay.view_blur`: within 1 of 255 of the game's own blurred picture at 100.00% of the texels). `post_check.py --water-blur`: 2,000 of 2,000 values, three mutations caught.
- **The export pass** (`6954` / `A4A9`) has no formula here: it writes the terrain patches' tessellation factors to memory and the position (0, 0, 0, 0) (instr 319 to 321); the native pass draws every patch by a fixed grid and reads none of it. Not run through `ucode_eval` (six opcodes and the export are not in the machine).
