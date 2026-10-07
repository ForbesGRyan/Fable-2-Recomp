// fable_2 - Boot save: runtime side (see src/core/boot_save.h for the options).
//
// The retail front end loads a save by reading its info (sub_824EA7A0: the save's
// level name) and calling sub_8237A2D8(gui, save name, level name); see its Load
// Game and Continue handlers (sub_826C9280, sub_826C72A8, sub_826C81A0).
// OnFrame runs the same two calls from the per-frame main loop hook while the
// front end is up, so no pad input is needed. (The game update sub_82355460 does
// not run in the front end, and the 0x8269xxxx menu handlers are a second,
// non-retail front end that the retail game never enters.)
//
// Recompiled guest functions are weak aliases of __imp__<name>; the strong
// definitions below replace them for direct and indirect calls.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include <rex/logging/macros.h>
#include "rex/ppc/context.h"

#include "../../constants/game_versions.h"
#include "boot_save.h"

static_assert(fable2::versions::kCodeGroup == "goty-identical-payload-2026-09-28",
              "boot save guest addresses were taken from this code group; re-derive them");

extern "C" void __imp__sub_822F4958(PPCContext& ctx, uint8_t* base);

namespace fable2::bootsave {

namespace ta = fable2::textappend;

inline uint32_t GuestZeroed(PPCContext& ctx, uint8_t* base, size_t size) {
  const uint32_t p = ta::guest_alloc(ctx, base, size);
  if (!p || !ta::wr2(base, p, size)) return 0;
  std::memset(reinterpret_cast<void*>(ta::host_addr(base, p)), 0, size);
  return p;
}

// *(addr) as a guest pointer, or 0 when addr is 0 or not readable.
inline uint32_t Deref(const uint8_t* base, uint32_t addr) {
  if (!addr || !ta::rdable2(base, addr, 4)) return 0;
  return ta::load_be(base, addr);
}

struct Objects {
  uint32_t game = 0, manager = 0, gui = 0;
};
inline Objects FindObjects(const uint8_t* base) {
  Objects o;
  const uint32_t root = Deref(base, kGameRoot);
  o.game = Deref(base, root ? root + 12 : 0);
  o.manager = Deref(base, o.game ? o.game + 40 : 0);
  const uint32_t gui_owner = Deref(base, root ? root + 8 : 0);
  o.gui = Deref(base, gui_owner ? gui_owner + 80 : 0);
  return o;
}

enum class LoadResult { kStarted, kNoSaveInfo, kNoDevice, kNoMemory };

// The front end's own sequence: pick the storage device, read the save's info, then start
// from it. Must run on the guest thread that runs the main loop. `valid` receives the
// info's valid byte (logged only: the Load Game list handler starts without checking it).
inline LoadResult TryLoad(PPCContext& ctx, uint8_t* base, const Objects& o, uint32_t user,
                          const std::string& hero, uint8_t* valid) {
  static uint32_t name = 0;  // game string with the hero name, built once
  if (!name) {
    const uint32_t cstr = GuestZeroed(ctx, base, hero.size() + 1);
    const uint32_t str = GuestZeroed(ctx, base, 16);
    if (!cstr || !str) return LoadResult::kNoMemory;
    std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, cstr)), hero.data(), hero.size());
    ctx.r3.u32 = str;
    ctx.r4.u32 = cstr;
    ctx.r5.u32 = 0xFFFFFFFF;
    REX_CALL_INDIRECT_FUNC(kStringCtorFn);
    name = str;
  }
  // Storage device for the user, as the Continue handler does before it reads saves.
  ctx.r3.u32 = o.manager;
  ctx.r4.u32 = user;
  REX_CALL_INDIRECT_FUNC(kHasDeviceFn);
  if ((ctx.r3.u32 & 0xFF) == 0) {
    ctx.r3.u32 = o.manager;
    ctx.r4.u32 = user;
    ctx.r5.u32 = 0;
    REX_CALL_INDIRECT_FUNC(kSelectDeviceFn);
    if ((ctx.r3.u32 & 0xFF) == 0) return LoadResult::kNoDevice;
  }

  const uint32_t info = GuestZeroed(ctx, base, 96);
  if (!info) return LoadResult::kNoMemory;

  ctx.r3.u32 = info;
  REX_CALL_INDIRECT_FUNC(kSaveInfoCtorFn);
  ctx.r3.u32 = o.manager;
  ctx.r4.u32 = info;
  ctx.r5.u32 = name;
  ctx.r6.u32 = user;
  REX_CALL_INDIRECT_FUNC(kSaveInfoFn);
  const bool found = (ctx.r3.u32 & 0xFF) != 0;
  *valid = *reinterpret_cast<const uint8_t*>(ta::host_addr(base, info + kInfoValid));

  if (found) {
    ctx.r3.u32 = o.gui;
    ctx.r4.u32 = name;
    ctx.r5.u32 = info + kInfoLevelName;
    REX_CALL_INDIRECT_FUNC(kStartFromSaveFn);
  }
  ctx.r3.u32 = info + kInfoString2;
  REX_CALL_INDIRECT_FUNC(kStringDtorFn);
  ctx.r3.u32 = info + kInfoLevelName;
  REX_CALL_INDIRECT_FUNC(kStringDtorFn);
  // The info block is left allocated (96 bytes per attempt).
  return found ? LoadResult::kStarted : LoadResult::kNoSaveInfo;
}

// Called once per frame from the main loop hook (MainRenderLoop_82B9CD68 in
// src/diagnostics/fps_meter.h), which also runs while the front end is up.
inline void OnFrame(PPCContext& ctx, uint8_t* base) {
  const Config& cfg = config();
  if (!cfg.trace && cfg.hero.empty()) return;
  static Trigger trigger;
  if (trigger.done && !cfg.trace) return;  // loaded, left by hand, or gave up: nothing more to do
  const Objects o = FindObjects(base);
  const bool ready = o.game && o.manager && o.gui && ta::rdable2(base, o.game + kGameLeftFrontEnd, 1);
  const bool in_front_end =
      ready && *reinterpret_cast<const uint8_t*>(ta::host_addr(base, o.game + kGameLeftFrontEnd)) == 0;
  const uint32_t state = Deref(base, kFrontEndState);
  const uint32_t mask = Deref(base, kSignedInMask);
  const uint32_t active_user = Deref(base, kActiveUser);

  static uint32_t last[5] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
  const uint32_t now[5] = {(o.game ? 1u : 0u) | (o.manager ? 2u : 0u) | (o.gui ? 4u : 0u),
                           in_front_end ? 1u : 0u, state, mask, active_user};
  if (std::memcmp(last, now, sizeof now) != 0) {
    REXSYS_INFO("[boot-save] frame {}: game {:08X} manager {:08X} gui {:08X} front end {} state {} "
                "signed-in mask {:X} user {}",
                trigger.updates, o.game, o.manager, o.gui, in_front_end ? "up" : "not up", state,
                mask, active_user);
    std::memcpy(last, now, sizeof now);
  }
  if (cfg.hero.empty()) {  // trace only
    ++trigger.updates;
    return;
  }
  if (!trigger.Step(in_front_end)) {
    static bool reported = false;
    if (trigger.gave_up && !reported) {
      reported = true;
      REXSYS_WARN("[boot-save] gave up on {} after {} attempts; menu left as is", cfg.hero,
                  trigger.attempts);
    }
    return;
  }
  const uint32_t attempts = trigger.attempts;
  if (attempts == 1) {
    const FindResult save = FindHeroDir(cfg.saves_root, cfg.hero);
    if (save.dir.empty()) {
      REXSYS_WARN("[boot-save] not started: {}", save.error);
      trigger.Finish();
      return;
    }
  }
  // TryLoad calls guest functions that clobber the registers; restore them so the main
  // loop function sees the state it was called with.
  const uint32_t user = active_user < 4 ? active_user : 0;
  uint8_t valid = 0;
  const PPCContext saved = ctx;
  const LoadResult result = TryLoad(ctx, base, o, user, cfg.hero, &valid);
  ctx = saved;
  if (result == LoadResult::kStarted) {
    REXSYS_INFO("[boot-save] loading {} (attempt {}, update {}, state {}, valid {})", cfg.hero,
                attempts, trigger.updates, state, valid);
    trigger.Finish();
  } else if (result == LoadResult::kNoMemory) {
    REXSYS_WARN("[boot-save] not started: guest allocation failed");
    trigger.Finish();
  } else if (attempts == 1 || attempts == 10 || attempts == 60) {
    REXSYS_WARN("[boot-save] {} (attempt {}, save {}, state {}, mask {:X})",
                result == LoadResult::kNoDevice ? "no storage device for the user yet"
                                                : "the game cannot read the save yet",
                attempts, cfg.hero, state, mask);
  }
}

}  // namespace fable2::bootsave

// Queues the Microsoft and Lionhead logo videos. FABLE2_SKIP_LOGOS=1: queue nothing.
extern "C" void sub_822F4958(PPCContext& ctx, uint8_t* base) {
  if (fable2::bootsave::config().skip_logos) {
    REXSYS_INFO("[boot-save] boot logo videos skipped");
    return;
  }
  __imp__sub_822F4958(ctx, base);
}
