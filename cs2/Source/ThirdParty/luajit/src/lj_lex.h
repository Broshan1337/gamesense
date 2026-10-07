

#ifndef _LJ_LEX_H
#define _LJ_LEX_H

#include <stdarg.h>

#include "lj_obj.h"
#include "lj_err.h"


#define TKDEF(_, __) \
  _(and) _(break) _(const) _(continue) _(do) _(else) _(elseif) _(end) _(false) \
  _(for) _(function) _(goto) _(if) _(in) _(local) _(nil) _(not) _(or) \
  _(repeat) _(return) _(then) _(true) _(until) _(while) \
  __(concat, ..) __(dots, ...) __(eq, ==) __(ge, >=) __(le, <=) __(ne, ~=) \
  __(nav, ?.) __(coal, \?\?) __(shl, <<) __(shr, >>)  __(sar, ~>>) \
  __(and_, &&) __(or_, ||) __(ne_, !=) __(arrow, ->) \
  __(label, ::) __(number, <number>) __(name, <name>) __(string, <string>) \
  __(eof, <eof>)

enum {
  TK_OFS = 256,
#define TKENUM1(name)		TK_##name,
#define TKENUM2(name, sym)	TK_##name,
TKDEF(TKENUM1, TKENUM2)
#undef TKENUM1
#undef TKENUM2
  TK_RESERVED = TK_while - TK_OFS
};

typedef int LexChar;	
typedef int LexToken;	


typedef struct BCInsLine {
  BCIns ins;		
  BCLine line;		
} BCInsLine;


typedef uint16_t VarIndex;

#define LJ_VINDEX_HSIZE	32	
#define LJ_VINDEX_MASK	(LJ_VINDEX_HSIZE-1)


typedef struct VarInfo {
  GCRef name;		
  BCPos startpc;	
  BCPos endpc;		
  uint8_t slot;		
  uint8_t info;		
  VarIndex prev;	
} VarInfo;


typedef struct LexState {
  struct FuncState *fs;	
  struct lua_State *L;	
  TValue tokval;	
  TValue lookaheadval;	
  const char *p;	
  const char *pe;	
  LexChar c;		
  LexToken tok;		
  LexToken lookahead;	
  SBuf sb;		
  lua_Reader rfunc;	
  void *rdata;		
  BCLine linenumber;	
  BCLine lastline;	
  GCstr *chunkname;	
  const char *chunkarg;	
  const char *mode;	
  VarInfo *vstack;	
  MSize sizevstack;	
  MSize vtop;		
  BCInsLine *bcstack;	
  MSize sizebcstack;	
  uint32_t level;	
  int endmark;		
  int fr2;		
  VarIndex vhash[LJ_VINDEX_HSIZE];	
} LexState;

LJ_FUNC int lj_lex_setup(lua_State *L, LexState *ls);
LJ_FUNC void lj_lex_cleanup(lua_State *L, LexState *ls);
LJ_FUNC void lj_lex_next(LexState *ls);
LJ_FUNC LexToken lj_lex_lookahead(LexState *ls);
LJ_FUNC const char *lj_lex_token2str(LexState *ls, LexToken tok);
LJ_FUNC_NORET void lj_lex_error(LexState *ls, LexToken tok, ErrMsg em, ...);
LJ_FUNC void lj_lex_init(lua_State *L);

#ifdef LUA_USE_ASSERT
#define lj_assertLS(c, ...)	(lj_assertG_(G(ls->L), (c), __VA_ARGS__))
#else
#define lj_assertLS(c, ...)	((void)ls)
#endif

#endif
