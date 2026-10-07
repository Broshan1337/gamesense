

#ifndef _LJ_OBJ_H
#define _LJ_OBJ_H

#include "lua.h"
#include "lj_def.h"
#include "lj_arch.h"




typedef uint32_t MSize;
#if LJ_GC64
typedef uint64_t GCSize;
#else
typedef uint32_t GCSize;
#endif


typedef struct MRef {
#if LJ_GC64
  uint64_t ptr64;	
#else
  uint32_t ptr32;	
#endif
} MRef;

#if LJ_GC64
#define mref(r, t)	((t *)(void *)(r).ptr64)
#define mrefu(r)	((r).ptr64)

#define setmref(r, p)	((r).ptr64 = (uint64_t)(void *)(p))
#define setmrefu(r, u)	((r).ptr64 = (uint64_t)(u))
#define setmrefr(r, v)	((r).ptr64 = (v).ptr64)
#else
#define mref(r, t)	((t *)(void *)(uintptr_t)(r).ptr32)
#define mrefu(r)	((r).ptr32)

#define setmref(r, p)	((r).ptr32 = (uint32_t)(uintptr_t)(void *)(p))
#define setmrefu(r, u)	((r).ptr32 = (uint32_t)(u))
#define setmrefr(r, v)	((r).ptr32 = (v).ptr32)
#endif




typedef struct GCRef {
#if LJ_GC64
  uint64_t gcptr64;	
#else
  uint32_t gcptr32;	
#endif
} GCRef;


#define GCHeader	GCRef nextgc; uint8_t marked; uint8_t gct


#if LJ_GC64
#define gcref(r)	((GCobj *)(r).gcptr64)
#define gcrefp(r, t)	((t *)(void *)(r).gcptr64)
#define gcrefu(r)	((r).gcptr64)
#define gcrefeq(r1, r2)	((r1).gcptr64 == (r2).gcptr64)

#define setgcref(r, gc)	((r).gcptr64 = (uint64_t)&(gc)->gch)
#define setgcreft(r, gc, it) \
  (r).gcptr64 = (uint64_t)&(gc)->gch | (((uint64_t)(it)) << 47)
#define setgcrefp(r, p)	((r).gcptr64 = (uint64_t)(p))
#define setgcrefnull(r)	((r).gcptr64 = 0)
#define setgcrefr(r, v)	((r).gcptr64 = (v).gcptr64)
#else
#define gcref(r)	((GCobj *)(uintptr_t)(r).gcptr32)
#define gcrefp(r, t)	((t *)(void *)(uintptr_t)(r).gcptr32)
#define gcrefu(r)	((r).gcptr32)
#define gcrefeq(r1, r2)	((r1).gcptr32 == (r2).gcptr32)

#define setgcref(r, gc)	((r).gcptr32 = (uint32_t)(uintptr_t)&(gc)->gch)
#define setgcrefp(r, p)	((r).gcptr32 = (uint32_t)(uintptr_t)(p))
#define setgcrefnull(r)	((r).gcptr32 = 0)
#define setgcrefr(r, v)	((r).gcptr32 = (v).gcptr32)
#endif

#define gcnext(gc)	(gcref((gc)->gch.nextgc))






typedef uint32_t BCIns;  
typedef uint32_t BCPos;  
typedef uint32_t BCReg;  
typedef int32_t BCLine;  


typedef void (*ASMFunction)(void);


#define SBufHeader	char *w, *e, *b; MRef L
typedef struct SBuf {
  SBufHeader;
} SBuf;




typedef union {
  int32_t ftsz;		
  MRef pcr;		
} FrameLink;


typedef LJ_ALIGN(8) union TValue {
  uint64_t u64;		
  lua_Number n;		
#if LJ_GC64
  GCRef gcr;		
  int64_t it64;
  struct {
    LJ_ENDIAN_LOHI(
      int32_t i;	
    , uint32_t it;	
    )
  };
#else
  struct {
    LJ_ENDIAN_LOHI(
      union {
	GCRef gcr;	
	int32_t i;	
      };
    , uint32_t it;	
    )
  };
#endif
#if LJ_FR2
  int64_t ftsz;		
#else
  struct {
    LJ_ENDIAN_LOHI(
      GCRef func;	
    , FrameLink tp;	
    )
  } fr;
#endif
  struct {
    LJ_ENDIAN_LOHI(
      uint32_t lo;	
    , uint32_t hi;	
    )
  } u32;
} TValue;

typedef const TValue cTValue;

#define tvref(r)	(mref(r, TValue))


#define LAST_TT		LUA_TTHREAD
#define LUA_TPROTO	(LAST_TT+1)
#define LUA_TCDATA	(LAST_TT+2)


#define LJ_TNIL			(~0u)
#define LJ_TFALSE		(~1u)
#define LJ_TTRUE		(~2u)
#define LJ_TLIGHTUD		(~3u)
#define LJ_TSTR			(~4u)
#define LJ_TUPVAL		(~5u)
#define LJ_TTHREAD		(~6u)
#define LJ_TPROTO		(~7u)
#define LJ_TFUNC		(~8u)
#define LJ_TTRACE		(~9u)
#define LJ_TCDATA		(~10u)
#define LJ_TTAB			(~11u)
#define LJ_TUDATA		(~12u)

#define LJ_TNUMX		(~13u)


#if LJ_64 && !LJ_GC64
#define LJ_TISNUM		0xfffeffffu
#else
#define LJ_TISNUM		LJ_TNUMX
#endif
#define LJ_TISTRUECOND		LJ_TFALSE
#define LJ_TISPRI		LJ_TTRUE
#define LJ_TISGCV		(LJ_TSTR+1)
#define LJ_TISTABUD		LJ_TTAB


#define LJ_KEYINDEX		0xfffe7fffu

#if LJ_GC64
#define LJ_GCVMASK		(((uint64_t)1 << 47) - 1)
#endif

#if LJ_64

#define LJ_LIGHTUD_BITS_SEG	8
#define LJ_LIGHTUD_BITS_LO	(47 - LJ_LIGHTUD_BITS_SEG)
#endif



typedef uint32_t StrHash;	
typedef uint32_t StrID;		


typedef struct GCstr {
  GCHeader;
  uint8_t reserved;	
  uint8_t hashalg;	
  StrID sid;		
  StrHash hash;		
  MSize len;		
} GCstr;

#define strref(r)	(&gcref((r))->str)
#define strdata(s)	((const char *)((s)+1))
#define strdatawr(s)	((char *)((s)+1))
#define strVdata(o)	strdata(strV(o))




typedef struct GCudata {
  GCHeader;
  uint8_t udtype;	
  uint8_t unused2;
  GCRef env;		
  MSize len;		
  GCRef metatable;	
  uint32_t align1;	
} GCudata;


enum {
  UDTYPE_USERDATA,	
  UDTYPE_IO_FILE,	
  UDTYPE_FFI_CLIB,	
  UDTYPE_BUFFER,	
  UDTYPE__MAX
};

#define uddata(u)	((void *)((u)+1))
#define sizeudata(u)	(sizeof(struct GCudata)+(u)->len)




typedef struct GCcdata {
  GCHeader;
  uint16_t ctypeid;	
} GCcdata;


typedef struct GCcdataVar {
  uint16_t offset;	
  uint16_t extra;	
  MSize len;		
} GCcdataVar;

#define cdataptr(cd)	((void *)((cd)+1))
#define cdataisv(cd)	((cd)->marked & 0x80)
#define cdatav(cd)	((GCcdataVar *)((char *)(cd) - sizeof(GCcdataVar)))
#define cdatavlen(cd)	check_exp(cdataisv(cd), cdatav(cd)->len)
#define sizecdatav(cd)	(cdatavlen(cd) + cdatav(cd)->extra)
#define memcdatav(cd)	((void *)((char *)(cd) - cdatav(cd)->offset))



#define SCALE_NUM_GCO	((int32_t)sizeof(lua_Number)/sizeof(GCRef))
#define round_nkgc(n)	(((n) + SCALE_NUM_GCO-1) & ~(SCALE_NUM_GCO-1))

typedef struct GCproto {
  GCHeader;
  uint8_t numparams;	
  uint8_t framesize;	
  MSize sizebc;		
#if LJ_GC64
  uint32_t unused_gc64;
#endif
  GCRef gclist;
  MRef k;		
  MRef uv;		
  MSize sizekgc;	
  MSize sizekn;		
  MSize sizept;		
  uint8_t sizeuv;	
  uint8_t flags;	
  uint16_t trace;	
  
  GCRef chunkname;	
  BCLine firstline;	
  BCLine numline;	
  MRef lineinfo;	
  MRef uvinfo;		
  MRef varinfo;		
} GCproto;


#define PROTO_CHILD		0x01	
#define PROTO_VARARG		0x02	
#define PROTO_FFI		0x04	
#define PROTO_NOJIT		0x08	
#define PROTO_ILOOP		0x10	
#define PROTO_BITOP		0x80	

#define PROTO_HAS_RETURN	0x20	
#define PROTO_FIXUP_RETURN	0x40	

#define PROTO_CLCOUNT		0x20	
#define PROTO_CLC_BITS		3
#define PROTO_CLC_POLY		(3*PROTO_CLCOUNT)  

#define PROTO_UV_LOCAL		0x8000	
#define PROTO_UV_IMMUTABLE	0x4000	

#define proto_kgc(pt, idx) \
  check_exp((uintptr_t)(intptr_t)(idx) >= ~(uintptr_t)(pt)->sizekgc+1u, \
	    gcref(mref((pt)->k, GCRef)[(idx)]))
#define proto_knumtv(pt, idx) \
  check_exp((uintptr_t)(idx) < (pt)->sizekn, &mref((pt)->k, TValue)[(idx)])
#define proto_bc(pt)		((BCIns *)((char *)(pt) + sizeof(GCproto)))
#define proto_bcpos(pt, pc)	((BCPos)((pc) - proto_bc(pt)))
#define proto_uv(pt)		(mref((pt)->uv, uint16_t))

#define proto_chunkname(pt)	(strref((pt)->chunkname))
#define proto_chunknamestr(pt)	(strdata(proto_chunkname((pt))))
#define proto_lineinfo(pt)	(mref((pt)->lineinfo, const void))
#define proto_uvinfo(pt)	(mref((pt)->uvinfo, const uint8_t))
#define proto_varinfo(pt)	(mref((pt)->varinfo, const uint8_t))



typedef struct GCupval {
  GCHeader;
  uint8_t closed;	
  uint8_t immutable;	
  union {
    TValue tv;		
    struct {		
      GCRef prev;
      GCRef next;
    };
  };
  MRef v;		
  uint32_t dhash;	
} GCupval;

#define uvprev(uv_)	(&gcref((uv_)->prev)->uv)
#define uvnext(uv_)	(&gcref((uv_)->next)->uv)
#define uvval(uv_)	(mref((uv_)->v, TValue))




#define GCfuncHeader \
  GCHeader; uint8_t ffid; uint8_t nupvalues; \
  GCRef env; GCRef gclist; MRef pc

typedef struct GCfuncC {
  GCfuncHeader;
  lua_CFunction f;	
  TValue upvalue[1];	
} GCfuncC;

typedef struct GCfuncL {
  GCfuncHeader;
  GCRef uvptr[1];	
} GCfuncL;

typedef union GCfunc {
  GCfuncC c;
  GCfuncL l;
} GCfunc;

#define FF_LUA		0
#define FF_C		1
#define isluafunc(fn)	((fn)->c.ffid == FF_LUA)
#define iscfunc(fn)	((fn)->c.ffid == FF_C)
#define isffunc(fn)	((fn)->c.ffid > FF_C)
#define funcproto(fn) \
  check_exp(isluafunc(fn), (GCproto *)(mref((fn)->l.pc, char)-sizeof(GCproto)))
#define sizeCfunc(n)	(sizeof(GCfuncC)-sizeof(TValue)+sizeof(TValue)*(n))
#define sizeLfunc(n)	(sizeof(GCfuncL)-sizeof(GCRef)+sizeof(GCRef)*(n))




typedef struct Node {
  TValue val;		
  TValue key;		
  MRef next;		
#if !LJ_GC64
  MRef freetop;		
#endif
} Node;

LJ_STATIC_ASSERT(offsetof(Node, val) == 0);

typedef struct GCtab {
  GCHeader;
  uint8_t nomm;		
  int8_t colo;		
  MRef array;		
  GCRef gclist;
  GCRef metatable;	
  MRef node;		
  uint32_t asize;	
  uint32_t hmask;	
#if LJ_GC64
  MRef freetop;		
#endif
} GCtab;

#define sizetabcolo(n)	((n)*sizeof(TValue) + sizeof(GCtab))
#define tabref(r)	((GCtab *)gcref((r)))
#define noderef(r)	(mref((r), Node))
#define nextnode(n)	(mref((n)->next, Node))
#if LJ_GC64
#define getfreetop(t, n)	(noderef((t)->freetop))
#define setfreetop(t, n, v)	(setmref((t)->freetop, (v)))
#else
#define getfreetop(t, n)	(noderef((n)->freetop))
#define setfreetop(t, n, v)	(setmref((n)->freetop, (v)))
#endif




enum {
  LJ_VMST_INTERP,	
  LJ_VMST_C,		
  LJ_VMST_GC,		
  LJ_VMST_EXIT,		
  LJ_VMST_RECORD,	
  LJ_VMST_OPT,		
  LJ_VMST_ASM,		
  LJ_VMST__MAX
};

#define setvmstate(g, st)	((g)->vmstate = ~LJ_VMST_##st)


#ifdef LJ_HASFFI
#define MMDEF_FFI(_) _(new)
#else
#define MMDEF_FFI(_)
#endif

#if LJ_52 || LJ_HASFFI
#define MMDEF_PAIRS(_) _(pairs) _(ipairs)
#else
#define MMDEF_PAIRS(_)
#define MM_pairs	255
#define MM_ipairs	255
#endif

#define MMDEF(_) \
  _(index) _(newindex) _(gc) _(mode) _(eq) _(len) \
   \
  _(lt) _(le) _(concat) _(call) \
   \
  _(add) _(sub) _(mul) _(div) _(mod) _(pow) _(unm) \
   \
  _(metatable) _(tostring) MMDEF_FFI(_) MMDEF_PAIRS(_)

typedef enum {
#define MMENUM(name)	MM_##name,
MMDEF(MMENUM)
#undef MMENUM
  MM__MAX,
  MM____ = MM__MAX,
  MM_FAST = MM_len
} MMS;


typedef enum {
  GCROOT_MMNAME,	
  GCROOT_MMNAME_LAST = GCROOT_MMNAME + MM__MAX-1,
  GCROOT_BASEMT,	
  GCROOT_BASEMT_NUM = GCROOT_BASEMT + ~LJ_TNUMX,
  GCROOT_IO_INPUT,	
  GCROOT_IO_OUTPUT,	
#if LJ_HASFFI
  GCROOT_FFI_FIN,	
#endif
  GCROOT_MAX
} GCRootID;

#define basemt_it(g, it)	((g)->gcroot[GCROOT_BASEMT+~(it)])
#define basemt_obj(g, o)	((g)->gcroot[GCROOT_BASEMT+itypemap(o)])
#define mmname_str(g, mm)	(strref((g)->gcroot[GCROOT_MMNAME+(mm)]))


typedef struct GCState {
  GCSize total;		
  GCSize threshold;	
  uint8_t currentwhite;	
  uint8_t state;	
  uint8_t unused0;
#if LJ_64
  uint8_t lightudnum;	
#else
  uint8_t unused1;
#endif
  MSize sweepstr;	
  GCRef root;		
  MRef sweep;		
  GCRef gray;		
  GCRef grayagain;	
  GCRef weak;		
  GCRef mmudata;	
  GCSize debt;		
  GCSize estimate;	
  MSize stepmul;	
  MSize pause;		
#if LJ_64
  MRef lightudseg;	
#endif
} GCState;


typedef struct StrInternState {
  GCRef *tab;		
  MSize mask;		
  MSize num;		
  StrID id;		
  uint8_t idreseed;	
  uint8_t second;	
  uint8_t unused1;
  uint8_t unused2;
  LJ_ALIGN(8) uint64_t seed;	
} StrInternState;


typedef struct global_State {
  lua_Alloc allocf;	
  void *allocd;		
  GCState gc;		
  GCstr strempty;	
  uint8_t stremptyz;	
  uint8_t hookmask;	
  uint8_t dispatchmode;	
  uint8_t vmevmask;	
  StrInternState str;	
  volatile int32_t vmstate;  
  GCRef mainthref;	
  SBuf tmpbuf;		
  TValue tmptv, tmptv2;	
  Node nilnode;		
  TValue registrytv;	
  GCRef vmthref;	
  GCupval uvhead;	
  int32_t hookcount;	
  int32_t hookcstart;	
  lua_Hook hookf;	
  lua_CFunction wrapf;	
  lua_CFunction panic;	
  BCIns bc_cfunc_int;	
  BCIns bc_cfunc_ext;	
  GCRef cur_L;		
  MRef jit_base;	
  MRef ctype_state;	
  PRNGState prng;	
  GCRef gcroot[GCROOT_MAX];  
} global_State;

#define mainthread(g)	(&gcref(g->mainthref)->th)
#define vmthread(g)	(&gcref(g->vmthref)->th)
#define niltv(L) \
  check_exp(tvisnil(&G(L)->nilnode.val), &G(L)->nilnode.val)
#define niltvg(g) \
  check_exp(tvisnil(&(g)->nilnode.val), &(g)->nilnode.val)


#define HOOK_EVENTMASK		0x0f
#define HOOK_ACTIVE		0x10
#define HOOK_ACTIVE_SHIFT	4
#define HOOK_VMEVENT		0x20
#define HOOK_GC			0x40
#define HOOK_PROFILE		0x80
#define hook_active(g)		((g)->hookmask & HOOK_ACTIVE)
#define hook_enter(g)		((g)->hookmask |= HOOK_ACTIVE)
#define hook_entergc(g) \
  ((g)->hookmask = ((g)->hookmask | (HOOK_ACTIVE|HOOK_GC)) & ~HOOK_PROFILE)
#define hook_vmevent(g)		((g)->hookmask |= (HOOK_ACTIVE|HOOK_VMEVENT))
#define hook_leave(g)		((g)->hookmask &= ~HOOK_ACTIVE)
#define hook_save(g)		((g)->hookmask & ~HOOK_EVENTMASK)
#define hook_restore(g, h) \
  ((g)->hookmask = ((g)->hookmask & HOOK_EVENTMASK) | (h))


struct lua_State {
  GCHeader;
  uint8_t dummy_ffid;	
  uint8_t status;	
  MRef glref;		
  GCRef gclist;		
  TValue *base;		
  TValue *top;		
  MRef maxstack;	
  MRef stack;		
  GCRef openupval;	
  GCRef env;		
  void *cframe;		
  MSize stacksize;	
};

#define G(L)			(mref(L->glref, global_State))
#define registry(L)		(&G(L)->registrytv)


#if LJ_GC64
#define curr_func(L)		(&gcval(L->base-2)->fn)
#elif LJ_FR2
#define curr_func(L)		(&gcref((L->base-2)->gcr)->fn)
#else
#define curr_func(L)		(&gcref((L->base-1)->fr.func)->fn)
#endif
#define curr_funcisL(L)		(isluafunc(curr_func(L)))
#define curr_proto(L)		(funcproto(curr_func(L)))
#define curr_topL(L)		(L->base + curr_proto(L)->framesize)
#define curr_top(L)		(curr_funcisL(L) ? curr_topL(L) : L->top)

#if defined(LUA_USE_ASSERT) || defined(LUA_USE_APICHECK)
LJ_FUNC_NORET void lj_assert_fail(global_State *g, const char *file, int line,
				  const char *func, const char *fmt, ...);
#endif




typedef struct GChead {
  GCHeader;
  uint8_t unused1;
  uint8_t unused2;
  GCRef env;
  GCRef gclist;
  GCRef metatable;
} GChead;


LJ_STATIC_ASSERT(offsetof(GChead, env) == offsetof(GCfuncL, env));
LJ_STATIC_ASSERT(offsetof(GChead, env) == offsetof(GCudata, env));


LJ_STATIC_ASSERT(offsetof(GChead, metatable) == offsetof(GCtab, metatable));
LJ_STATIC_ASSERT(offsetof(GChead, metatable) == offsetof(GCudata, metatable));


LJ_STATIC_ASSERT(offsetof(GChead, gclist) == offsetof(lua_State, gclist));
LJ_STATIC_ASSERT(offsetof(GChead, gclist) == offsetof(GCproto, gclist));
LJ_STATIC_ASSERT(offsetof(GChead, gclist) == offsetof(GCfuncL, gclist));
LJ_STATIC_ASSERT(offsetof(GChead, gclist) == offsetof(GCtab, gclist));

typedef union GCobj {
  GChead gch;
  GCstr str;
  GCupval uv;
  lua_State th;
  GCproto pt;
  GCfunc fn;
  GCcdata cd;
  GCtab tab;
  GCudata ud;
} GCobj;


#define gco2str(o)	check_exp((o)->gch.gct == ~LJ_TSTR, &(o)->str)
#define gco2uv(o)	check_exp((o)->gch.gct == ~LJ_TUPVAL, &(o)->uv)
#define gco2th(o)	check_exp((o)->gch.gct == ~LJ_TTHREAD, &(o)->th)
#define gco2pt(o)	check_exp((o)->gch.gct == ~LJ_TPROTO, &(o)->pt)
#define gco2func(o)	check_exp((o)->gch.gct == ~LJ_TFUNC, &(o)->fn)
#define gco2cd(o)	check_exp((o)->gch.gct == ~LJ_TCDATA, &(o)->cd)
#define gco2tab(o)	check_exp((o)->gch.gct == ~LJ_TTAB, &(o)->tab)
#define gco2ud(o)	check_exp((o)->gch.gct == ~LJ_TUDATA, &(o)->ud)


#define obj2gco(v)	((GCobj *)(v))




#if LJ_GC64
#define itype(o)	((uint32_t)((o)->it64 >> 47))
#define tvisnil(o)	((o)->it64 == -1)
#else
#define itype(o)	((o)->it)
#define tvisnil(o)	(itype(o) == LJ_TNIL)
#endif
#define tvisfalse(o)	(itype(o) == LJ_TFALSE)
#define tvistrue(o)	(itype(o) == LJ_TTRUE)
#define tvisbool(o)	(tvisfalse(o) || tvistrue(o))
#if LJ_64 && !LJ_GC64
#define tvislightud(o)	(((int32_t)itype(o) >> 15) == -2)
#else
#define tvislightud(o)	(itype(o) == LJ_TLIGHTUD)
#endif
#define tvisstr(o)	(itype(o) == LJ_TSTR)
#define tvisfunc(o)	(itype(o) == LJ_TFUNC)
#define tvisthread(o)	(itype(o) == LJ_TTHREAD)
#define tvisproto(o)	(itype(o) == LJ_TPROTO)
#define tviscdata(o)	(itype(o) == LJ_TCDATA)
#define tvistab(o)	(itype(o) == LJ_TTAB)
#define tvisudata(o)	(itype(o) == LJ_TUDATA)
#define tvisnumber(o)	(itype(o) <= LJ_TISNUM)
#define tvisint(o)	(LJ_DUALNUM && itype(o) == LJ_TISNUM)
#define tvisnum(o)	(itype(o) < LJ_TISNUM)

#define tvistruecond(o)	(itype(o) < LJ_TISTRUECOND)
#define tvispri(o)	(itype(o) >= LJ_TISPRI)
#define tvistabud(o)	(itype(o) <= LJ_TISTABUD)  
#define tvisgcv(o)	((itype(o) - LJ_TISGCV) > (LJ_TNUMX - LJ_TISGCV))


#define tvisnan(o)	((o)->n != (o)->n)
#if LJ_64
#define tviszero(o)	(((o)->u64 << 1) == 0)
#else
#define tviszero(o)	(((o)->u32.lo | ((o)->u32.hi << 1)) == 0)
#endif
#define tvispzero(o)	((o)->u64 == 0)
#define tvismzero(o)	((o)->u64 == U64x(80000000,00000000))
#define tvispone(o)	((o)->u64 == U64x(3ff00000,00000000))
#define rawnumequal(o1, o2)	((o1)->u64 == (o2)->u64)


#if LJ_64 && !LJ_GC64
#define itypemap(o) \
  (tvisnumber(o) ? ~LJ_TNUMX : tvislightud(o) ? ~LJ_TLIGHTUD : ~itype(o))
#else
#define itypemap(o)	(tvisnumber(o) ? ~LJ_TNUMX : ~itype(o))
#endif


#if LJ_GC64
#define gcval(o)	((GCobj *)(gcrefu((o)->gcr) & LJ_GCVMASK))
#else
#define gcval(o)	(gcref((o)->gcr))
#endif
#define boolV(o)	check_exp(tvisbool(o), (LJ_TFALSE - itype(o)))
#if LJ_64
#define lightudseg(u) \
  (((u) >> LJ_LIGHTUD_BITS_LO) & ((1 << LJ_LIGHTUD_BITS_SEG)-1))
#define lightudlo(u) \
  ((u) & (((uint64_t)1 << LJ_LIGHTUD_BITS_LO) - 1))
#define lightudup(p) \
  ((uint32_t)(((p) >> LJ_LIGHTUD_BITS_LO) << (LJ_LIGHTUD_BITS_LO-32)))
static LJ_AINLINE void *lightudV(global_State *g, cTValue *o)
{
  uint64_t u = o->u64;
  uint64_t seg = lightudseg(u);
  uint32_t *segmap = mref(g->gc.lightudseg, uint32_t);
  lj_assertG(tvislightud(o), "lightuserdata expected");
  if (seg == (1 << LJ_LIGHTUD_BITS_SEG)-1) return NULL;
  lj_assertG(seg <= g->gc.lightudnum, "bad lightuserdata segment %d", seg);
  return (void *)(((uint64_t)segmap[seg] << 32) | lightudlo(u));
}
#else
#define lightudV(g, o)	check_exp(tvislightud(o), gcrefp((o)->gcr, void))
#endif
#define gcV(o)		check_exp(tvisgcv(o), gcval(o))
#define strV(o)		check_exp(tvisstr(o), &gcval(o)->str)
#define funcV(o)	check_exp(tvisfunc(o), &gcval(o)->fn)
#define threadV(o)	check_exp(tvisthread(o), &gcval(o)->th)
#define protoV(o)	check_exp(tvisproto(o), &gcval(o)->pt)
#define cdataV(o)	check_exp(tviscdata(o), &gcval(o)->cd)
#define tabV(o)		check_exp(tvistab(o), &gcval(o)->tab)
#define udataV(o)	check_exp(tvisudata(o), &gcval(o)->ud)
#define numV(o)		check_exp(tvisnum(o), (o)->n)
#define intV(o)		check_exp(tvisint(o), (int32_t)(o)->i)


#if LJ_GC64
#define setitype(o, i)		((o)->it = ((i) << 15))
#define setnilV(o)		((o)->it64 = -1)
#define setpriV(o, x)		((o)->it64 = (int64_t)~((uint64_t)~(x)<<47))
#define setboolV(o, x)		((o)->it64 = (int64_t)~((uint64_t)((x)+1)<<47))
#else
#define setitype(o, i)		((o)->it = (i))
#define setnilV(o)		((o)->it = LJ_TNIL)
#define setboolV(o, x)		((o)->it = LJ_TFALSE-(uint32_t)(x))
#define setpriV(o, i)		(setitype((o), (i)))
#endif

static LJ_AINLINE void setrawlightudV(TValue *o, void *p)
{
#if LJ_GC64
  o->u64 = (uint64_t)p | (((uint64_t)LJ_TLIGHTUD) << 47);
#elif LJ_64
  o->u64 = (uint64_t)p | (((uint64_t)0xffff) << 48);
#else
  setgcrefp(o->gcr, p); setitype(o, LJ_TLIGHTUD);
#endif
}

#if LJ_FR2 || LJ_32
#define contptr(f)		((void *)(f))
#define setcont(o, f)		((o)->u64 = (uint64_t)(uintptr_t)contptr(f))
#else
#define contptr(f) \
  ((void *)(uintptr_t)(uint32_t)((intptr_t)(f) - (intptr_t)lj_vm_asm_begin))
#define setcont(o, f) \
  ((o)->u64 = (uint64_t)(void *)(f) - (uint64_t)lj_vm_asm_begin)
#endif

static LJ_AINLINE void checklivetv(lua_State *L, TValue *o, const char *msg)
{
  UNUSED(L); UNUSED(o); UNUSED(msg);
#if LUA_USE_ASSERT
  if (tvisgcv(o)) {
    lj_assertL(~itype(o) == gcval(o)->gch.gct,
	       "mismatch of TValue type %d vs GC type %d",
	       ~itype(o), gcval(o)->gch.gct);
    
    lj_assertL(!(gcval(o)->gch.marked & (G(L)->gc.currentwhite ^ 3) & 3), msg);
  }
#endif
}

static LJ_AINLINE void setgcVraw(TValue *o, GCobj *v, uint32_t itype)
{
#if LJ_GC64
  setgcreft(o->gcr, v, itype);
#else
  setgcref(o->gcr, v); setitype(o, itype);
#endif
}

static LJ_AINLINE void setgcV(lua_State *L, TValue *o, GCobj *v, uint32_t it)
{
  setgcVraw(o, v, it);
  checklivetv(L, o, "store to dead GC object");
}

#define define_setV(name, type, tag) \
static LJ_AINLINE void name(lua_State *L, TValue *o, const type *v) \
{ \
  setgcV(L, o, obj2gco(v), tag); \
}
define_setV(setstrV, GCstr, LJ_TSTR)
define_setV(setthreadV, lua_State, LJ_TTHREAD)
define_setV(setprotoV, GCproto, LJ_TPROTO)
define_setV(setfuncV, GCfunc, LJ_TFUNC)
define_setV(setcdataV, GCcdata, LJ_TCDATA)
define_setV(settabV, GCtab, LJ_TTAB)
define_setV(setudataV, GCudata, LJ_TUDATA)

#define setnumV(o, x)		((o)->n = (x))
#define setnanV(o)		((o)->u64 = U64x(fff80000,00000000))
#define setpinfV(o)		((o)->u64 = U64x(7ff00000,00000000))
#define setminfV(o)		((o)->u64 = U64x(fff00000,00000000))

static LJ_AINLINE void setintV(TValue *o, int32_t i)
{
#if LJ_DUALNUM
  o->i = (uint32_t)i; setitype(o, LJ_TISNUM);
#else
  o->n = (lua_Number)i;
#endif
}

static LJ_AINLINE void setint64V(TValue *o, int64_t i)
{
  if (LJ_DUALNUM && LJ_LIKELY(i == (int64_t)(int32_t)i))
    setintV(o, (int32_t)i);
  else
    setnumV(o, (lua_Number)i);
}

#if LJ_64
#define setintptrV(o, i)	setint64V((o), (i))
#else
#define setintptrV(o, i)	setintV((o), (i))
#endif


static LJ_AINLINE void copyTV(lua_State *L, TValue *o1, const TValue *o2)
{
  *o1 = *o2;
  checklivetv(L, o1, "copy of dead GC object");
}






#define lj_num2int(n)		((int32_t)(n))


#define lj_num2int_type(n, tp)	((tp)(n))


LJ_ASMF LJ_CONSTF int64_t lj_vm_num2int_check(double x);


#define lj_num2int_ok(x)	(lj_vm_num2int_check((x)) >= 0)


#define lj_num2int_cond(x, i64, i, cond) \
  (i64 = lj_vm_num2int_check((x)), cond ? (i = (int32_t)i64, 1) : 0)


#define lj_num2int_check(x, i64, i) \
  lj_num2int_cond((x), i64, i, i64 >= 0)


LJ_ASMF LJ_CONSTF int64_t lj_vm_num2i64(double x);
LJ_ASMF LJ_CONSTF uint64_t lj_vm_num2u64(double x);

#if LJ_TARGET_X86

LJ_ASMF LJ_CONSTF int64_t lj_vm_num2i64_sse3(double x);
LJ_ASMF LJ_CONSTF uint64_t lj_vm_num2u64_sse3(double x);
LJ_ASMF int64_t (*lj_vm_num2i64_ptr)(double x);
LJ_ASMF uint64_t (*lj_vm_num2u64_ptr)(double x);
static LJ_AINLINE int64_t lj_num2i64(double x)
{
  return (*lj_vm_num2i64_ptr)(x);
}
static LJ_AINLINE uint64_t lj_num2u64(double x)
{
  return (*lj_vm_num2u64_ptr)(x);
}

#else

#define lj_num2i64(x)		(lj_vm_num2i64((x)))
#define lj_num2u64(x)		(lj_vm_num2u64((x)))

#endif


LJ_ASMF LJ_CONSTF int32_t lj_vm_tobit(double x);

#define lj_num2bit(x)	lj_vm_tobit((x))

static LJ_AINLINE int32_t numberVint(cTValue *o)
{
  if (LJ_LIKELY(tvisint(o)))
    return intV(o);
  else
    return lj_num2int(numV(o));
}

static LJ_AINLINE lua_Number numberVnum(cTValue *o)
{
  if (LJ_UNLIKELY(tvisint(o)))
    return (lua_Number)intV(o);
  else
    return numV(o);
}




LJ_DATA const char *const lj_obj_typename[1+LUA_TCDATA+1];
LJ_DATA const char *const lj_obj_itypename[~LJ_TNUMX+1];

#define lj_typename(o)	(lj_obj_itypename[itypemap(o)])


LJ_FUNC int LJ_FASTCALL lj_obj_equal(cTValue *o1, cTValue *o2);
LJ_FUNC const void * LJ_FASTCALL lj_obj_ptr(global_State *g, cTValue *o);

#if LJ_ABI_PAUTH
#if LJ_TARGET_ARM64
#include <ptrauth.h>
#define lj_ptr_sign(ptr, ctx) \
  ptrauth_sign_unauthenticated((ptr), ptrauth_key_function_pointer, (ctx))
#define lj_ptr_strip(ptr) ptrauth_strip((ptr), ptrauth_key_function_pointer)
#else
#error "No support for pointer authentication for this architecture"
#endif
#else
#define lj_ptr_sign(ptr, ctx) (ptr)
#define lj_ptr_strip(ptr) (ptr)
#endif

#endif
