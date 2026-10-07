

#ifndef _LUAJIT_H
#define _LUAJIT_H

#include "lua.h"

#define LUAJIT_VERSION		"LuaJIT 2.1.ROLLING"
#define LUAJIT_VERSION_NUM	20199  
#define LUAJIT_VERSION_SYM	luaJIT_version_2_1_ROLLING
#define LUAJIT_COPYRIGHT	"Copyright (C) 2005-2026 Mike Pall"
#define LUAJIT_URL		"https://luajit.org/"


#define LUAJIT_MODE_MASK	0x00ff

enum {
  LUAJIT_MODE_ENGINE,		
  LUAJIT_MODE_DEBUG,		

  LUAJIT_MODE_FUNC,		
  LUAJIT_MODE_ALLFUNC,		
  LUAJIT_MODE_ALLSUBFUNC,	

  LUAJIT_MODE_TRACE,		

  LUAJIT_MODE_WRAPCFUNC = 0x10,	

  LUAJIT_MODE_MAX
};


#define LUAJIT_MODE_OFF		0x0000	
#define LUAJIT_MODE_ON		0x0100	
#define LUAJIT_MODE_FLUSH	0x0200	




LUA_API int luaJIT_setmode(lua_State *L, int idx, int mode);


typedef void (*luaJIT_profile_callback)(void *data, lua_State *L,
					int samples, int vmstate);
LUA_API void luaJIT_profile_start(lua_State *L, const char *mode,
				  luaJIT_profile_callback cb, void *data);
LUA_API void luaJIT_profile_stop(lua_State *L);
LUA_API const char *luaJIT_profile_dumpstack(lua_State *L, const char *fmt,
					     int depth, size_t *len);


LUA_API void LUAJIT_VERSION_SYM(void);

#error "DO NOT USE luajit_rolling.h -- only include build-generated luajit.h"
#endif
