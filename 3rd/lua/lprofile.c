/*
** Sampling-profiler bridge
*/

#define lprofile_c
#define LUA_CORE

#include "lprefix.h"

#include <limits.h>
#include <string.h>

#include "lua.h"

#include "lapi.h"
#include "ldebug.h"
#include "lobject.h"
#include "lprofile.h"
#include "lstate.h"


lua_CFunction luaP_cfunctionfromci (CallInfo *ci) {
  TValue *func;
  if (ci == NULL)
    return NULL;
  func = s2v(ci->func.p);
  if (ttislcf(func))
    return fvalue(func);
  if (ttisCclosure(func))
    return clCvalue(func)->f;
  return NULL;
}


static int statefromci (lua_State *L, CallInfo *ci,
                        lua_CFunction *cfunction) {
  if (ci == NULL || ci == &L->base_ci) {
    *cfunction = NULL;
    return LUA_PROFILE_HOST;
  }
  if (isLua(ci)) {
    *cfunction = NULL;
    return LUA_PROFILE_LUA;
  }
  *cfunction = luaP_cfunctionfromci(ci);
  return LUA_PROFILE_C;
}


static void publishstate (lua_State *L, int state,
                          lua_CFunction cfunction) {
  global_State *g = G(L);
  lua_ProfileStateChange callback = g->profilehooks.state_change;
  if (g->profilethread == L && L->profilestate == state &&
      L->profilecfunction == cfunction)
    return;
  L->profilestate = cast_byte(state);
  L->profilecfunction = cfunction;
  g->profilethread = L;
  if (callback != NULL && !g->profileincallback) {
    g->profileincallback = 1;
    callback(g->ud_profile, L, state, cfunction);
    g->profileincallback = 0;
  }
}


void luaP_enterstate_ (lua_State *L, luaP_StateGuard *guard,
                       int state, lua_CFunction cfunction) {
  guard->active = 1;
  guard->state = L->profilestate;
  guard->cfunction = L->profilecfunction;
  publishstate(L, state, cfunction);
}


void luaP_leavestate_ (lua_State *L, const luaP_StateGuard *guard) {
  if (guard->active)
    publishstate(L, guard->state, guard->cfunction);
  else
    luaP_syncstate(L);
}


void luaP_leaveframe_ (lua_State *L, const luaP_StateGuard *guard,
                       CallInfo *previous) {
  if (guard->active)
    publishstate(L, guard->state, guard->cfunction);
  else {
    lua_CFunction cfunction;
    int state = statefromci(L, previous, &cfunction);
    publishstate(L, state, cfunction);
  }
}


void luaP_syncstate (lua_State *L) {
  if (luaP_stateenabled(L)) {
    lua_CFunction cfunction;
    int state = statefromci(L, L->ci, &cfunction);
    publishstate(L, state, cfunction);
  }
}


void luaP_sethoststate (lua_State *L) {
  if (luaP_stateenabled(L))
    publishstate(L, LUA_PROFILE_HOST, NULL);
}


#if defined(__GNUC__) || defined(__clang__)

static unsigned int takepending (global_State *g) {
  return __atomic_exchange_n(&g->profilepending, 0, __ATOMIC_RELAXED);
}

static void addpending (global_State *g, unsigned int count) {
  unsigned int old = __atomic_load_n(&g->profilepending, __ATOMIC_RELAXED);
  for (;;) {
    unsigned int next = (UINT_MAX - old < count) ? UINT_MAX : old + count;
    if (__atomic_compare_exchange_n(&g->profilepending, &old, next, 1,
                                    __ATOMIC_RELAXED, __ATOMIC_RELAXED))
      return;
  }
}

#else

static unsigned int takepending (global_State *g) {
  unsigned int pending = g->profilepending;
  g->profilepending = 0;
  return pending;
}

static void addpending (global_State *g, unsigned int count) {
  if (UINT_MAX - g->profilepending < count)
    g->profilepending = UINT_MAX;
  else
    g->profilepending += count;
}

#endif


void luaP_safepoint_ (lua_State *L, const Instruction *pc) {
  global_State *g = G(L);
  lua_ProfileSafePoint callback = g->profilehooks.safe_point;
  unsigned int pending;
  if (callback == NULL || g->profileincallback)
    return;
  pending = takepending(g);
  if (pending == 0)
    return;
  if (isLua(L->ci)) {
    Proto *p = ci_func(L->ci)->p;
    L->ci->u.l.savedpc = (pc == p->code) ? pc + 1 : pc;
  }
  g->profileincallback = 1;
  callback(g->ud_profile, L, pending);
  g->profileincallback = 0;
}


void luaP_allocation_ (lua_State *L, void *old_pointer,
                       void *new_pointer, size_t old_size,
                       size_t new_size, int success) {
  global_State *g = G(L);
  lua_ProfileAllocation callback = g->profilehooks.allocation;
  lua_ProfileAllocEvent event;
  if (callback == NULL || g->profileincallback)
    return;
  event.old_pointer = old_pointer;
  event.new_pointer = new_pointer;
  event.old_size = old_size;
  event.new_size = new_size;
  event.success = cast_byte(success != 0);
  g->profileincallback = 1;
  callback(g->ud_profile, L, &event);
  g->profileincallback = 0;
}


LUA_API void lua_setprofilehooks (lua_State *L,
                                  const lua_ProfileHooks *hooks, void *ud) {
  global_State *g;
  int hadsafepoint;
  lua_lock(L);
  g = G(L);
  hadsafepoint = (g->profilehooks.safe_point != NULL);
  if (hooks == NULL)
    memset(&g->profilehooks, 0, sizeof(g->profilehooks));
  else
    g->profilehooks = *hooks;
  g->ud_profile = ud;
  g->profilethread = NULL;
  if (!hadsafepoint || g->profilehooks.safe_point == NULL)
    (void)takepending(g);
  luaP_syncstate(L);
  lua_unlock(L);
}


LUA_API void lua_profile_request (lua_State *L, unsigned int count) {
  if (count != 0)
    addpending(G(L), count);
}


LUA_API int lua_getprofilestate (lua_State *L,
                                 lua_CFunction *cfunction) {
  lua_CFunction current;
  int state;
  lua_lock(L);
  state = statefromci(L, L->ci, &current);
  if (cfunction != NULL)
    *cfunction = current;
  lua_unlock(L);
  return state;
}


LUA_API size_t lua_profile_capturestack (lua_State *L,
                                         lua_ProfileFrame *frames,
                                         size_t capacity,
                                         int *truncated) {
  CallInfo *ci;
  size_t count = 0;
  if (truncated != NULL)
    *truncated = 0;
  if (L->profilecaptureblocked) {
    if (truncated != NULL)
      *truncated = 1;
    return 0;
  }
  for (ci = L->ci; ci != &L->base_ci; ci = ci->previous) {
    lua_ProfileFrame *frame;
    if (count == capacity) {
      if (truncated != NULL)
        *truncated = 1;
      break;
    }
    frame = &frames[count++];
    memset(frame, 0, sizeof(*frame));
    {
      const char *name = NULL;
      (void)luaG_getfuncname(L, ci, &name);
      if (name != NULL) {
        frame->name = name;
        frame->name_length = strlen(name);
      }
    }
    if (isLua(ci)) {
      Proto *p = ci_func(ci)->p;
      int currentpc = pcRel(ci->u.l.savedpc, p);
      frame->kind = LUA_PROFILE_FRAME_LUA;
      frame->function = p;
      frame->linedefined = p->linedefined;
      frame->currentline = (currentpc < 0)
                         ? -1 : luaG_getfuncline(p, currentpc);
      if (p->source != NULL) {
        frame->source = getstr(p->source);
        frame->source_length = tsslen(p->source);
      }
    }
    else {
      frame->kind = LUA_PROFILE_FRAME_C;
      frame->cfunction = luaP_cfunctionfromci(ci);
      frame->linedefined = -1;
      frame->currentline = -1;
    }
  }
  return count;
}
