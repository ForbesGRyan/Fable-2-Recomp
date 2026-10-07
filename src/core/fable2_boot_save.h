// fable_2 - Boot save: runtime side (see src/core/boot_save.h for the options).
//
// The front-end menu is UI over one global, the initial save name. Its Load Game
// handler (sub_82695590) checks that the save exists, assigns the name, clears a
// byte and calls sub_826962D0(front end, user index). OnFrontEndUpdate runs the
// same sequence from the per-frame front-end update (sub_82692230, r3 = front
// end) once the front end accepts input, so no pad input is needed.
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

extern "C" void __imp__sub_82692230(PPCContext& ctx, uint8_t* base);
extern "C" void __imp__sub_822F4958(PPCContext& ctx, uint8_t* base);

namespace fable2::bootsave {

namespace ta = fable2::textappend;

inline uint32_t GuestZeroed(PPCContext& ctx, uint8_t* base, size_t size) {
  const uint32_t p = ta::guest_alloc(ctx, base, size);
  if (!p || !ta::wr2(base, p, size)) return 0;
  std::memset(reinterpret_cast<void*>(ta::host_addr(base, p)), 0, size);
  return p;
}

// *(*(*kGameRoot + 12) + 40), or 0 when any link is not readable.
inline uint32_t SaveManager(const uint8_t* base) {
  if (!ta::rdable2(base, kGameRoot, 4)) return 0;
  const uint32_t root = ta::load_be(base, kGameRoot);
  if (!root || !ta::rdable2(base, root + 12, 4)) return 0;
  const uint32_t mid = ta::load_be(base, root + 12);
  if (!mid || !ta::rdable2(base, mid + 40, 4)) return 0;
  return ta::load_be(base, mid + 40);
}

// The Load Game handler's sequence. Returns false (and starts nothing) when the game
// says the save does not exist. Must run on the guest thread that updates the front end.
inline bool StartLoad(PPCContext& ctx, uint8_t* base, uint32_t front_end, const std::string& hero) {
  const uint32_t cstr = GuestZeroed(ctx, base, hero.size() + 1);
  const uint32_t name = GuestZeroed(ctx, base, 16);
  const uint32_t query = GuestZeroed(ctx, base, 96);
  const uint32_t manager = SaveManager(base);
  if (!cstr || !name || !query || !manager) {
    REXSYS_WARN("[boot-save] not started: cstr={:08X} name={:08X} query={:08X} manager={:08X}",
                cstr, name, query, manager);
    return false;
  }
  std::memcpy(reinterpret_cast<void*>(ta::host_addr(base, cstr)), hero.data(), hero.size());

  ctx.r3.u32 = name;
  ctx.r4.u32 = cstr;
  ctx.r5.u32 = 0xFFFFFFFF;
  REX_CALL_INDIRECT_FUNC(kStringCtorFn);

  ctx.r3.u32 = query;
  REX_CALL_INDIRECT_FUNC(kSaveQueryCtorFn);
  ctx.r3.u32 = manager;
  ctx.r4.u32 = query;
  ctx.r5.u32 = name;
  ctx.r6.u32 = kUserIndex;
  REX_CALL_INDIRECT_FUNC(kSaveExistsFn);
  const bool exists = (ctx.r3.u32 & 0xFF) != 0;
  ctx.r3.u32 = query + 16;
  REX_CALL_INDIRECT_FUNC(kStringDtorFn);
  ctx.r3.u32 = query + 12;
  REX_CALL_INDIRECT_FUNC(kStringDtorFn);

  if (exists) {
    ctx.r3.u32 = kInitialSaveName;
    ctx.r4.u32 = name;
    REX_CALL_INDIRECT_FUNC(kStringAssignFn);
    *reinterpret_cast<uint8_t*>(ta::host_addr(base, kLoadClearedByte)) = 0;
    ctx.r3.u32 = front_end;
    ctx.r4.u32 = kUserIndex;
    REX_CALL_INDIRECT_FUNC(kStartGameFn);
  }
  ctx.r3.u32 = name;
  REX_CALL_INDIRECT_FUNC(kStringDtorFn);
  // The C string and the two small blocks are left allocated (once per run).
  return exists;
}

// Called after every front-end update with its object.
inline void OnFrontEndUpdate(PPCContext& ctx, uint8_t* base, uint32_t front_end) {
  const Config& cfg = config();
  if (!cfg.trace && cfg.hero.empty()) return;
  if (!front_end || !ta::rdable2(base, front_end + 4, 4) || !ta::rdable2(base, kFrontEndState, 4)) return;
  const uint32_t state = ta::load_be(base, kFrontEndState);
  const uint32_t screen = ta::load_be(base, front_end + 4);

  static Trigger trigger;
  static uint32_t last_state = 0xFFFFFFFF, last_screen = 0xFFFFFFFF;
  if (state != last_state || screen != last_screen) {
    REXSYS_INFO("[boot-save] front end state {} screen {} (update {})", state, screen, trigger.updates);
    last_state = state;
    last_screen = screen;
  }
  if (cfg.hero.empty()) {
    ++trigger.updates;
    return;
  }

  const Action action = trigger.Step(state);
  if (action == Action::kWait && trigger.updates == 1800)
    REXSYS_WARN("[boot-save] still waiting after 1800 front-end updates (state {}, needs {}..{})",
                state, kFireStateMin, kFireStateMax);
  if (action != Action::kFire) return;

  const FindResult save = FindHeroDir(cfg.saves_root, cfg.hero);
  if (save.dir.empty()) {
    REXSYS_WARN("[boot-save] not started: {}", save.error);
    return;
  }
  // StartLoad calls guest functions that clobber the registers; the update we hooked
  // has already returned, so restore the context it left.
  const PPCContext saved = ctx;
  const bool started = StartLoad(ctx, base, front_end, cfg.hero);
  ctx = saved;
  if (started)
    REXSYS_INFO("[boot-save] loading {} (front end state {}, update {})", cfg.hero, state, trigger.updates);
  else
    REXSYS_WARN("[boot-save] the game reports no save named {}; menu left as is", cfg.hero);
}

}  // namespace fable2::bootsave

// Front-end per-frame update: run the original, then let boot save act.
extern "C" void sub_82692230(PPCContext& ctx, uint8_t* base) {
  const uint32_t front_end = ctx.r3.u32;
  __imp__sub_82692230(ctx, base);
  fable2::bootsave::OnFrontEndUpdate(ctx, base, front_end);
}

// Queues the Microsoft and Lionhead logo videos. FABLE2_SKIP_LOGOS=1: queue nothing.
extern "C" void sub_822F4958(PPCContext& ctx, uint8_t* base) {
  if (fable2::bootsave::config().skip_logos) {
    REXSYS_INFO("[boot-save] boot logo videos skipped");
    return;
  }
  __imp__sub_822F4958(ctx, base);
}
