/*
** Internal sampling-profiler bridge
*/

#ifndef lprofile_h
#define lprofile_h

#include "lstate.h"

#if defined(LUA_USE_LUAPROF)

typedef struct luaP_StateGuard {
  lu_byte active;
  lu_byte state;
  lua_CFunction cfunction;
} luaP_StateGuard;

#define luaP_stateguard(name)  luaP_StateGuard name

#if defined(__GNUC__) || defined(__clang__)
#define luaP_loadpending(g) \
  __atomic_load_n(&(g)->profilepending, __ATOMIC_RELAXED)
#else
#define luaP_loadpending(g)  ((g)->profilepending)
#endif

LUAI_FUNC void luaP_enterstate_ (lua_State *L, luaP_StateGuard *guard,
                                 int state, lua_CFunction cfunction);
LUAI_FUNC void luaP_leavestate_ (lua_State *L,
                                 const luaP_StateGuard *guard);
LUAI_FUNC void luaP_leaveframe_ (lua_State *L,
                                 const luaP_StateGuard *guard,
                                 CallInfo *previous);
LUAI_FUNC void luaP_syncstate (lua_State *L);
LUAI_FUNC void luaP_sethoststate (lua_State *L);
LUAI_FUNC lua_CFunction luaP_cfunctionfromci (CallInfo *ci);
LUAI_FUNC void luaP_safepoint_ (lua_State *L, const Instruction *pc);
LUAI_FUNC void luaP_allocation_ (lua_State *L, void *old_pointer,
                                 void *new_pointer, size_t old_size,
                                 size_t new_size, int success);

#define luaP_stateenabled(L)  (G(L)->profilehooks.state_change != NULL)

#define luaP_enterstate(L,guard,state,cfunction) \
  { (guard)->active = 0; \
    if (l_unlikely(luaP_stateenabled(L))) \
      luaP_enterstate_(L, guard, state, cfunction); }

#define luaP_leavestate(L,guard) \
  { if (l_unlikely((guard)->active || luaP_stateenabled(L))) \
      luaP_leavestate_(L, guard); }

#define luaP_leaveframe(L,guard,previous) \
  { if (l_unlikely((guard)->active || luaP_stateenabled(L))) \
      luaP_leaveframe_(L, guard, previous); }

#define luaP_safepoint(L) \
  { global_State *g__ = G(L); \
    if (l_unlikely(g__->profilehooks.safe_point != NULL && \
                   luaP_loadpending(g__) != 0)) \
      luaP_safepoint_(L, pc); }

#define luaP_allocation(L,oldp,newp,olds,news,success) \
  { if (((oldp) != NULL || (news) != 0) && \
        l_unlikely(G(L)->profilehooks.allocation != NULL)) \
      luaP_allocation_(L, oldp, newp, olds, news, success); }

#define luaP_allocationpc(L,ci,pc) \
  { if (l_unlikely(G(L)->profilehooks.allocation != NULL)) \
      (ci)->u.l.savedpc = (pc); }

#define luaP_initthread(L) \
  { (L)->profilestate = LUA_PROFILE_HOST; \
    (L)->profilecaptureblocked = 0; \
    (L)->profilecfunction = NULL; }

#define luaP_initglobal(g) \
  { memset(&(g)->profilehooks, 0, sizeof((g)->profilehooks)); \
    (g)->ud_profile = NULL; \
    (g)->profilethread = NULL; \
    (g)->profilepending = 0; \
    (g)->profileincallback = 0; }

#define luaP_blockcapture(L)    ((L)->profilecaptureblocked = 1)
#define luaP_unblockcapture(L)  ((L)->profilecaptureblocked = 0)

#else

#define luaP_stateguard(name)  ((void)0)
#define luaP_enterstate(L,guard,state,cfunction)  ((void)0)
#define luaP_leavestate(L,guard)                  ((void)0)
#define luaP_leaveframe(L,guard,previous)         ((void)0)
#define luaP_syncstate(L)                         ((void)0)
#define luaP_sethoststate(L)                      ((void)0)
#define luaP_safepoint(L)                         ((void)0)
#define luaP_allocation(L,oldp,newp,olds,news,success)  ((void)0)
#define luaP_allocationpc(L,ci,pc)                      ((void)0)
#define luaP_initthread(L)                        ((void)0)
#define luaP_initglobal(g)                        ((void)0)
#define luaP_blockcapture(L)                      ((void)0)
#define luaP_unblockcapture(L)                    ((void)0)

#endif

#endif
