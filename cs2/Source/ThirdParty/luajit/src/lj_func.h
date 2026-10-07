

#ifndef _LJ_FUNC_H
#define _LJ_FUNC_H

#include "lj_obj.h"


LJ_FUNC void LJ_FASTCALL lj_func_freeproto(global_State *g, GCproto *pt);


LJ_FUNCA void LJ_FASTCALL lj_func_closeuv(lua_State *L, TValue *level);
LJ_FUNC void LJ_FASTCALL lj_func_freeuv(global_State *g, GCupval *uv);


LJ_FUNC GCfunc *lj_func_newC(lua_State *L, MSize nelems, GCtab *env);
LJ_FUNC GCfunc *lj_func_newL_empty(lua_State *L, GCproto *pt, GCtab *env);
LJ_FUNCA GCfunc *lj_func_newL_gc(lua_State *L, GCproto *pt, GCfuncL *parent);
LJ_FUNC void LJ_FASTCALL lj_func_free(global_State *g, GCfunc *c);

#endif
