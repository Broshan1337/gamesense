

#ifndef _LJ_CLIB_H
#define _LJ_CLIB_H

#include "lj_obj.h"

#if LJ_HASFFI


#define CLNS_INDEX	((1u<<CT_FUNC)|(1u<<CT_EXTERN)|(1u<<CT_CONSTVAL))


typedef struct CLibrary {
  void *handle;		
  GCtab *cache;		
} CLibrary;

LJ_FUNC TValue *lj_clib_index(lua_State *L, CLibrary *cl, GCstr *name);
LJ_FUNC void lj_clib_load(lua_State *L, GCtab *mt, GCstr *name, int global);
LJ_FUNC void lj_clib_unload(CLibrary *cl);
LJ_FUNC void lj_clib_default(lua_State *L, GCtab *mt);

#endif

#endif
