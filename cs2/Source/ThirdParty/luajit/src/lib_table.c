

#define lib_table_c
#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_tab.h"
#include "lj_ff.h"
#include "lj_lib.h"



#define LJLIB_MODULE_table

LJLIB_LUA(table_foreachi) 

LJLIB_LUA(table_foreach) 

LJLIB_LUA(table_getn) 

LJLIB_CF(table_maxn)
{
  GCtab *t = lj_lib_checktab(L, 1);
  TValue *array = tvref(t->array);
  Node *node;
  lua_Number m = 0;
  ptrdiff_t i;
  for (i = (ptrdiff_t)t->asize - 1; i >= 0; i--)
    if (!tvisnil(&array[i])) {
      m = (lua_Number)(int32_t)i;
      break;
    }
  node = noderef(t->node);
  for (i = (ptrdiff_t)t->hmask; i >= 0; i--)
    if (!tvisnil(&node[i].val) && tvisnumber(&node[i].key)) {
      lua_Number n = numberVnum(&node[i].key);
      if (n > m) m = n;
    }
  setnumV(L->top-1, m);
  return 1;
}

LJLIB_CF(table_insert)		LJLIB_REC(.)
{
  GCtab *t = lj_lib_checktab(L, 1);
  int32_t n, i = (int32_t)lj_tab_len(t) + 1;
  int nargs = (int)((char *)L->top - (char *)L->base);
  if (nargs != 2*sizeof(TValue)) {
    if (nargs != 3*sizeof(TValue))
      lj_err_caller(L, LJ_ERR_TABINS);
    
    for (n = lj_lib_checkint(L, 2); i > n; i--) {
      
      TValue *dst = lj_tab_setint(L, t, i);
      cTValue *src = lj_tab_getint(t, i-1);
      if (src) {
	copyTV(L, dst, src);
      } else {
	setnilV(dst);
      }
    }
    i = n;
  }
  {
    TValue *dst = lj_tab_setint(L, t, i);
    copyTV(L, dst, L->top-1);  
    lj_gc_barriert(L, t, dst);
  }
  return 0;
}

LJLIB_LUA(table_remove) 

LJLIB_LUA(table_move) 

LJLIB_CF(table_concat)		LJLIB_REC(.)
{
  GCtab *t = lj_lib_checktab(L, 1);
  GCstr *sep = lj_lib_optstr(L, 2);
  int32_t i = lj_lib_optint(L, 3, 1);
  int32_t e = (L->base+3 < L->top && !tvisnil(L->base+3)) ?
	      lj_lib_checkint(L, 4) : (int32_t)lj_tab_len(t);
  SBuf *sb = lj_buf_tmp_(L);
  SBuf *sbx = lj_buf_puttab(sb, t, sep, i, e);
  if (LJ_UNLIKELY(!sbx)) {  
    int32_t idx = (int32_t)(intptr_t)sb->w;
    cTValue *o = lj_tab_getint(t, idx);
    lj_err_callerv(L, LJ_ERR_TABCAT,
		   lj_obj_itypename[o ? itypemap(o) : ~LJ_TNIL], idx);
  }
  setstrV(L, L->top-1, lj_buf_str(L, sbx));
  lj_gc_check(L);
  return 1;
}



static void set2(lua_State *L, int i, int j)
{
  lua_rawseti(L, 1, i);
  lua_rawseti(L, 1, j);
}

static int sort_comp(lua_State *L, int a, int b)
{
  if (!lua_isnil(L, 2)) {  
    int res;
    lua_pushvalue(L, 2);
    lua_pushvalue(L, a-1);  
    lua_pushvalue(L, b-2);  
    lua_call(L, 2, 1);
    res = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return res;
  } else {  
    return lua_lessthan(L, a, b);
  }
}

static void auxsort(lua_State *L, int l, int u)
{
  while (l < u) {  
    int i, j;
    
    lua_rawgeti(L, 1, l);
    lua_rawgeti(L, 1, u);
    if (sort_comp(L, -1, -2))  
      set2(L, l, u);  
    else
      lua_pop(L, 2);
    if (u-l == 1) break;  
    i = (l+u)/2;
    lua_rawgeti(L, 1, i);
    lua_rawgeti(L, 1, l);
    if (sort_comp(L, -2, -1)) {  
      set2(L, i, l);
    } else {
      lua_pop(L, 1);  
      lua_rawgeti(L, 1, u);
      if (sort_comp(L, -1, -2))  
	set2(L, i, u);
      else
	lua_pop(L, 2);
    }
    if (u-l == 2) break;  
    lua_rawgeti(L, 1, i);  
    lua_pushvalue(L, -1);
    lua_rawgeti(L, 1, u-1);
    set2(L, i, u-1);
    
    i = l; j = u-1;
    for (;;) {  
      
      while (lua_rawgeti(L, 1, ++i), sort_comp(L, -1, -2)) {
	if (i>=u) lj_err_caller(L, LJ_ERR_TABSORT);
	lua_pop(L, 1);  
      }
      
      while (lua_rawgeti(L, 1, --j), sort_comp(L, -3, -1)) {
	if (j<=l) lj_err_caller(L, LJ_ERR_TABSORT);
	lua_pop(L, 1);  
      }
      if (j<i) {
	lua_pop(L, 3);  
	break;
      }
      set2(L, i, j);
    }
    lua_rawgeti(L, 1, u-1);
    lua_rawgeti(L, 1, i);
    set2(L, u-1, i);  
    
    
    if (i-l < u-i) {
      j=l; i=i-1; l=i+2;
    } else {
      j=i+1; i=u; u=j-2;
    }
    auxsort(L, j, i);  
  }  
}

LJLIB_CF(table_sort)
{
  GCtab *t = lj_lib_checktab(L, 1);
  int32_t n = (int32_t)lj_tab_len(t);
  lua_settop(L, 2);
  if (!tvisnil(L->base+1))
    lj_lib_checkfunc(L, 2);
  auxsort(L, 1, n);
  return 0;
}

#if LJ_52
LJLIB_PUSH("n")
LJLIB_CF(table_pack)
{
  TValue *array, *base = L->base;
  MSize i, n = (uint32_t)(L->top - base);
  GCtab *t = lj_tab_new(L, n ? n+1 : 0, 1);
  
  setintV(lj_tab_setstr(L, t, strV(lj_lib_upvalue(L, 1))), (int32_t)n);
  for (array = tvref(t->array) + 1, i = 0; i < n; i++)
    copyTV(L, &array[i], &base[i]);
  settabV(L, base, t);
  L->top = base+1;
  lj_gc_check(L);
  return 1;
}
#endif

LJLIB_NOREG LJLIB_CF(table_new)		LJLIB_REC(.)
{
  int32_t a = lj_lib_checkint(L, 1);
  int32_t h = lj_lib_checkint(L, 2);
  lua_createtable(L, a, h);
  return 1;
}

LJLIB_NOREG LJLIB_CF(table_clear)	LJLIB_REC(.)
{
  lj_tab_clear(lj_lib_checktab(L, 1));
  return 0;
}

static int luaopen_table_new(lua_State *L)
{
  return lj_lib_postreg(L, lj_cf_table_new, FF_table_new, "new");
}

static int luaopen_table_clear(lua_State *L)
{
  return lj_lib_postreg(L, lj_cf_table_clear, FF_table_clear, "clear");
}



#include "lj_libdef.h"

LUALIB_API int luaopen_table(lua_State *L)
{
  LJ_LIB_REG(L, LUA_TABLIBNAME, table);
#if LJ_52
  lua_getglobal(L, "unpack");
  lua_setfield(L, -2, "unpack");
#endif
  lj_lib_prereg(L, LUA_TABLIBNAME ".new", luaopen_table_new, tabV(L->top-1));
  lj_lib_prereg(L, LUA_TABLIBNAME ".clear", luaopen_table_clear, tabV(L->top-1));
  return 1;
}

