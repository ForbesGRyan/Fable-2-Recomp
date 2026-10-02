@echo off
rem Standalone synthetic tests: no game files, GPU, SDL or audio device needed.
setlocal
set "ROOT=%~dp0.."
set "SDK=%~1"
if not defined SDK set "SDK=%ROOT%\thirdparty\rexglue-sdk"
set "OUT=%ROOT%\out\tests\runtime-unit-tests"
if not exist "%OUT%" mkdir "%OUT%"
python "%ROOT%\tools\generate_game_versions.py" --check || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_clocked_audio_sink.cpp" -o "%OUT%\audio.exe" || exit /b 1
"%OUT%\audio.exe" || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_frame_limiter.cpp" "%SDK%\src\graphics\frame_limiter.cpp" -o "%OUT%\limiter.exe" || exit /b 1
"%OUT%\limiter.exe" || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_guest_frame_meter.cpp" -o "%OUT%\meter.exe" || exit /b 1
"%OUT%\meter.exe" || exit /b 1
for %%P in (goty-compatible goty-us-eu goty-german) do (
  clang++ -std=c++23 -DFABLE2_BUILD_PROFILE=\"%%P\" "%~dp0native\test_xex_verify.cpp" -o "%OUT%\xex-%%P.exe" || exit /b 1
  "%OUT%\xex-%%P.exe" || exit /b 1
)
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_native_suppress_policy.cpp" -o "%OUT%\suppress.exe" || exit /b 1
"%OUT%\suppress.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_native_render_state.cpp" -o "%OUT%\native_state.exe" || exit /b 1
"%OUT%\native_state.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_native_shaders.cpp" -ld3dcompiler -o "%OUT%\native_shaders.exe" || exit /b 1
"%OUT%\native_shaders.exe" || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_emulated_frame_stats.cpp" -o "%OUT%\frame_stats.exe" || exit /b 1
"%OUT%\frame_stats.exe" || exit /b 1
clang++ -std=c++23 -I"%SDK%\include" "%~dp0native\test_pacing_stats.cpp" -o "%OUT%\pacing.exe" || exit /b 1
"%OUT%\pacing.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_position_decode.cpp" -o "%OUT%\position_decode.exe" || exit /b 1
"%OUT%\position_decode.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_index_convert.cpp" -o "%OUT%\index_convert.exe" || exit /b 1
"%OUT%\index_convert.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_vfetch_decode.cpp" -o "%OUT%\vfetch_decode.exe" || exit /b 1
"%OUT%\vfetch_decode.exe" || exit /b 1
rem Float-to-int casts of NaN/infinity trap here (UB that otherwise passes silently).
clang++ -std=c++23 -fsanitize=float-cast-overflow -fsanitize-trap=float-cast-overflow "%~dp0native\test_terrain_patch.cpp" -o "%OUT%\terrain_patch.exe" || exit /b 1
"%OUT%\terrain_patch.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_rigid_skin.cpp" -o "%OUT%\rigid_skin.exe" || exit /b 1
"%OUT%\rigid_skin.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_frame_scene.cpp" -o "%OUT%\frame_scene.exe" || exit /b 1
"%OUT%\frame_scene.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_shader_tally.cpp" -o "%OUT%\shader_tally.exe" || exit /b 1
"%OUT%\shader_tally.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_geometry_cache_index.cpp" -o "%OUT%\geometry_cache_index.exe" || exit /b 1
"%OUT%\geometry_cache_index.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_clay_logic.cpp" -o "%OUT%\clay_logic.exe" || exit /b 1
"%OUT%\clay_logic.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_discovery_format.cpp" -o "%OUT%\discovery_format.exe" || exit /b 1
"%OUT%\discovery_format.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_page_cache.cpp" -o "%OUT%\page_cache.exe" || exit /b 1
"%OUT%\page_cache.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_xdk_layout.cpp" -o "%OUT%\xdk_layout.exe" || exit /b 1
"%OUT%\xdk_layout.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_main_scene.cpp" -o "%OUT%\main_scene.exe" || exit /b 1
"%OUT%\main_scene.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_draw_nesting.cpp" -o "%OUT%\draw_nesting.exe" || exit /b 1
"%OUT%\draw_nesting.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_window_stats.cpp" -o "%OUT%\window_stats.exe" || exit /b 1
"%OUT%\window_stats.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_stream_resolve.cpp" -o "%OUT%\stream_resolve.exe" || exit /b 1
"%OUT%\stream_resolve.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_autoplay.cpp" -o "%OUT%\autoplay.exe" || exit /b 1
"%OUT%\autoplay.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_texture_decode.cpp" -o "%OUT%\texture_decode.exe" || exit /b 1
"%OUT%\texture_decode.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_uv_decode.cpp" -o "%OUT%\uv_decode.exe" || exit /b 1
"%OUT%\uv_decode.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_material.cpp" -o "%OUT%\material.exe" || exit /b 1
"%OUT%\material.exe" || exit /b 1
clang++ -std=c++23 "%~dp0native\test_texture_residency.cpp" -o "%OUT%\texture_residency.exe" || exit /b 1
"%OUT%\texture_residency.exe" || exit /b 1
exit /b 0
