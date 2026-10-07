

#ifndef luaconf_h
#define luaconf_h

#ifndef WINVER
#define WINVER 0x0501
#endif
#include <stddef.h>


#if defined(_WIN32)

#define LUA_LDIR	"!\\lua\\"
#define LUA_CDIR	"!\\"
#define LUA_PATH_DEFAULT \
  ".\\?.lua;" LUA_LDIR"?.lua;" LUA_LDIR"?\\init.lua;"
#define LUA_CPATH_DEFAULT \
  ".\\?.dll;" LUA_CDIR"?.dll;" LUA_CDIR"loadall.dll"
#else

#ifndef LUA_MULTILIB
#define LUA_MULTILIB	"lib"
#endif
#ifndef LUA_LMULTILIB
#define LUA_LMULTILIB	"lib"
#endif
#define LUA_LROOT	"/usr/local"
#define LUA_LUADIR	"/lua/5.1/"

#ifdef LUA_ROOT
#define LUA_JROOT	LUA_ROOT
#define LUA_RLDIR	LUA_ROOT "/share" LUA_LUADIR
#define LUA_RCDIR	LUA_ROOT "/" LUA_MULTILIB LUA_LUADIR
#define LUA_RLPATH	";" LUA_RLDIR "?.lua;" LUA_RLDIR "?/init.lua"
#define LUA_RCPATH	";" LUA_RCDIR "?.so"
#else
#define LUA_JROOT	LUA_LROOT
#define LUA_RLPATH
#define LUA_RCPATH
#endif

#ifndef LUA_LJDIR
#define LUA_LJDIR	LUA_JROOT "/share/luajit-2.1"
#endif

#define LUA_JPATH	";" LUA_LJDIR "/?.lua"
#define LUA_LLDIR	LUA_LROOT "/share" LUA_LUADIR
#define LUA_LCDIR	LUA_LROOT "/" LUA_LMULTILIB LUA_LUADIR
#define LUA_LLPATH	";" LUA_LLDIR "?.lua;" LUA_LLDIR "?/init.lua"
#define LUA_LCPATH1	";" LUA_LCDIR "?.so"
#define LUA_LCPATH2	";" LUA_LCDIR "loadall.so"

#define LUA_PATH_DEFAULT	"./?.lua" LUA_JPATH LUA_LLPATH LUA_RLPATH
#define LUA_CPATH_DEFAULT	"./?.so" LUA_LCPATH1 LUA_RCPATH LUA_LCPATH2
#endif


#define LUA_PATH	"LUA_PATH"
#define LUA_CPATH	"LUA_CPATH"
#define LUA_INIT	"LUA_INIT"


#if defined(_WIN32)
#define LUA_DIRSEP	"\\"
#else
#define LUA_DIRSEP	"/"
#endif
#define LUA_PATHSEP	";"
#define LUA_PATH_MARK	"?"
#define LUA_EXECDIR	"!"
#define LUA_IGMARK	"-"
#define LUA_PATH_CONFIG \
  LUA_DIRSEP "\n" LUA_PATHSEP "\n" LUA_PATH_MARK "\n" \
  LUA_EXECDIR "\n" LUA_IGMARK "\n"


#define LUA_QL(x)	"'" x "'"
#define LUA_QS		LUA_QL("%s")


#define LUAI_MAXSTACK	65500	
#define LUAI_MAXCSTACK	8000	
#define LUAI_GCPAUSE	200	
#define LUAI_GCMUL	200	
#define LUA_MAXCAPTURES	32	


#if defined(luajit_c)
#define LUA_PROGNAME	"luajit"  
#define LUA_PROMPT	"> "	
#define LUA_PROMPT2	">> "	
#define LUA_MAXINPUT	512	
#endif


#define LUA_INTEGER	ptrdiff_t
#define LUA_IDSIZE	60	

#define LUAL_BUFFERSIZE	(BUFSIZ > 16384 ? 8192 : BUFSIZ)


#define LUA_NUMBER_DOUBLE
#define LUA_NUMBER		double
#define LUAI_UACNUMBER		double
#define LUA_NUMBER_SCAN		"%lf"
#define LUA_NUMBER_FMT		"%.14g"
#define lua_number2str(s, n)	sprintf((s), LUA_NUMBER_FMT, (n))
#define LUAI_MAXNUMBER2STR	32
#define LUA_INTFRMLEN		"l"
#define LUA_INTFRM_T		long


#if defined(LUA_BUILD_AS_DLL)
#if defined(LUA_CORE) || defined(LUA_LIB)
#define LUA_API		__declspec(dllexport)
#else
#define LUA_API		__declspec(dllimport)
#endif
#elif (defined(__ELF__) || defined(__MACH__) || defined(__psp2__)) && !((defined(__sun__) && defined(__svr4__)) || defined(__CELLOS_LV2__))

#define LUA_API		extern __attribute__((visibility("hidden")))
#else
#define LUA_API		extern
#endif

#define LUALIB_API	LUA_API


#if defined(LUA_USE_ASSERT) || defined(LUA_USE_APICHECK)
#include <assert.h>
#endif
#ifdef LUA_USE_ASSERT
#define lua_assert(x)		assert(x)
#endif
#ifdef LUA_USE_APICHECK
#define luai_apicheck(L, o)	{ (void)L; assert(o); }
#else
#define luai_apicheck(L, o)	{ (void)L; }
#endif

#endif
