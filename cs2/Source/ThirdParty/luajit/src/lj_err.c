

#define lj_err_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_err.h"
#include "lj_debug.h"
#include "lj_str.h"
#include "lj_func.h"
#include "lj_state.h"
#include "lj_frame.h"
#include "lj_ff.h"
#include "lj_trace.h"
#include "lj_vm.h"
#include "lj_strfmt.h"






LJ_DATADEF const char *lj_err_allmsg =
#define ERRDEF(name, msg)	msg "\0"
#include "lj_errmsg.h"
;




LJ_NOINLINE static void unwindstack(lua_State *L, TValue *top)
{
  lj_func_closeuv(L, top);
  if (top < L->top-1) {
    copyTV(L, top, L->top-1);
    L->top = top+1;
  }
  lj_state_relimitstack(L);
}


static void *err_unwind(lua_State *L, void *stopcf, int errcode)
{
  TValue *frame = L->base-1;
  void *cf = L->cframe;
  while (cf) {
    int32_t nres = cframe_nres(cframe_raw(cf));
    if (nres < 0) {  
      TValue *top = restorestack(L, -nres);
      if (frame < top) {  
	if (errcode) {
	  L->base = frame+1;
	  L->cframe = cframe_prev(cf);
	  unwindstack(L, top);
	}
	return cf;
      }
    }
    if (frame <= tvref(L->stack)+LJ_FR2)
      break;
    switch (frame_typep(frame)) {
    case FRAME_LUA:  
    case FRAME_LUAP:
      frame = frame_prevl(frame);
      break;
    case FRAME_C:  
    unwind_c:
#if LJ_UNWIND_EXT
      if (errcode) {
	L->base = frame_prevd(frame) + 1;
	L->cframe = cframe_prev(cf);
	unwindstack(L, frame - LJ_FR2);
      } else if (cf != stopcf) {
	cf = cframe_prev(cf);
	frame = frame_prevd(frame);
	break;
      }
      return NULL;  
#else
      UNUSED(stopcf);
      cf = cframe_prev(cf);
      frame = frame_prevd(frame);
      break;
#endif
    case FRAME_CP:  
      if (cframe_canyield(cf)) {  
	if (errcode) {
	  hook_leave(G(L));  
	  L->cframe = NULL;
	  L->status = (uint8_t)errcode;
	}
	return cf;
      }
      if (errcode) {
	L->base = frame_prevd(frame) + 1;
	L->cframe = cframe_prev(cf);
	unwindstack(L, frame - LJ_FR2);
      }
      return cf;
    case FRAME_CONT:  
      if (frame_iscont_fficb(frame))
	goto unwind_c;
      
    case FRAME_VARG:  
      frame = frame_prevd(frame);
      break;
    case FRAME_PCALL:  
    case FRAME_PCALLH:  
      if (errcode) {
	global_State *g;
	if (errcode == LUA_YIELD) {
	  frame = frame_prevd(frame);
	  break;
	}
	g = G(L);
	setgcref(g->cur_L, obj2gco(L));
	if (frame_typep(frame) == FRAME_PCALL)
	  hook_leave(g);
	L->base = frame_prevd(frame) + 1;
	L->cframe = cf;
	unwindstack(L, L->base);
      }
      return (void *)((intptr_t)cf | CFRAME_UNWIND_FF);
    }
  }
  
  if (errcode) {
    L->base = tvref(L->stack)+1+LJ_FR2;
    L->cframe = NULL;
    unwindstack(L, L->base);
    if (G(L)->panic)
      G(L)->panic(L);
    exit(EXIT_FAILURE);
  }
  return L;  
}



#if LJ_ABI_WIN



#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if LJ_TARGET_X86
typedef void *UndocumentedDispatcherContext;  
#else

typedef struct UndocumentedDispatcherContext {
  ULONG64 ControlPc;
  ULONG64 ImageBase;
  PRUNTIME_FUNCTION FunctionEntry;
  ULONG64 EstablisherFrame;
  ULONG64 TargetIp;
  PCONTEXT ContextRecord;
  void (*LanguageHandler)(void);
  PVOID HandlerData;
  PUNWIND_HISTORY_TABLE HistoryTable;
  ULONG ScopeIndex;
  ULONG Fill0;
} UndocumentedDispatcherContext;
#endif


extern void __DestructExceptionObject(EXCEPTION_RECORD *rec, int nothrow);

#define LJ_MSVC_EXCODE		((DWORD)0xe06d7363)
#define LJ_GCC_EXCODE		((DWORD)0x20474343)

#define LJ_EXCODE		((DWORD)0xe24c4a00)
#define LJ_EXCODE_MAKE(c)	(LJ_EXCODE | (DWORD)(c))
#define LJ_EXCODE_CHECK(cl)	(((cl) ^ LJ_EXCODE) <= 0xff)
#define LJ_EXCODE_ERRCODE(cl)	((int)((cl) & 0xff))


LJ_FUNCA int lj_err_unwind_win(EXCEPTION_RECORD *rec,
  void *f, CONTEXT *ctx, UndocumentedDispatcherContext *dispatch)
{
#if LJ_TARGET_X86
  void *cf = (char *)f - CFRAME_OFS_SEH;
#elif LJ_TARGET_ARM64
  void *cf = (char *)f - CFRAME_SIZE;
#else
  void *cf = f;
#endif
  lua_State *L = cframe_L(cf);
  int errcode = LJ_EXCODE_CHECK(rec->ExceptionCode) ?
		LJ_EXCODE_ERRCODE(rec->ExceptionCode) : LUA_ERRRUN;
  if ((rec->ExceptionFlags & 6)) {  
    if (rec->ExceptionCode == STATUS_LONGJUMP &&
	rec->ExceptionRecord &&
	LJ_EXCODE_CHECK(rec->ExceptionRecord->ExceptionCode)) {
      errcode = LJ_EXCODE_ERRCODE(rec->ExceptionRecord->ExceptionCode);
      if ((rec->ExceptionFlags & 0x20)) {  
	
	rec->ExceptionCode = 0;
      }
    }
    
    err_unwind(L, cf, errcode);
  } else {
    void *cf2 = err_unwind(L, cf, 0);
    if (cf2) {  
#if !LJ_TARGET_X86
      EXCEPTION_RECORD rec2;
#endif
      if (rec->ExceptionCode == LJ_MSVC_EXCODE ||
	  rec->ExceptionCode == LJ_GCC_EXCODE) {
#if !LJ_TARGET_CYGWIN
	__DestructExceptionObject(rec, 1);
#endif
	setstrV(L, L->top++, lj_err_str(L, LJ_ERR_ERRCPP));
      } else if (!LJ_EXCODE_CHECK(rec->ExceptionCode)) {
	
	return 1;  
      }
#if LJ_TARGET_X86
      UNUSED(ctx);
      UNUSED(dispatch);
      
      lj_vm_rtlunwind(cf, (void *)rec,
	(cframe_unwind_ff(cf2) && errcode != LUA_YIELD) ?
	(void *)lj_vm_unwind_ff : (void *)lj_vm_unwind_c, errcode);
      
#else
      if (LJ_EXCODE_CHECK(rec->ExceptionCode)) {
	
	rec2.ExceptionCode = STATUS_LONGJUMP;
	rec2.ExceptionRecord = rec;
	rec2.ExceptionAddress = 0;
	rec2.NumberParameters = 1;
	rec2.ExceptionInformation[0] = (ULONG_PTR)ctx;
	rec = &rec2;
      }
      
      RtlUnwindEx(f, (void *)((cframe_unwind_ff(cf2) && errcode != LUA_YIELD) ?
			      lj_vm_unwind_ff_eh :
			      lj_vm_unwind_c_eh),
		  rec, (void *)(uintptr_t)errcode, dispatch->ContextRecord,
		  dispatch->HistoryTable);
      
#endif
    }
  }
  return 1;  
}

#if LJ_UNWIND_JIT

#if LJ_TARGET_X64
#define CONTEXT_REG_PC	Rip
#elif LJ_TARGET_ARM64
#define CONTEXT_REG_PC	Pc
#else
#error "NYI: Windows arch-specific unwinder for JIT-compiled code"
#endif


static void err_unwind_win_jit(global_State *g, int errcode)
{
  CONTEXT ctx;
  UNWIND_HISTORY_TABLE hist;

  memset(&hist, 0, sizeof(hist));
  RtlCaptureContext(&ctx);
  while (1) {
    DWORD64 frame, base, addr = ctx.CONTEXT_REG_PC;
    void *hdata;
    PRUNTIME_FUNCTION func = RtlLookupFunctionEntry(addr, &base, &hist);
    if (!func) {  
      ExitNo exitno;
      uintptr_t stub = lj_trace_unwind(G2J(g), (uintptr_t)(addr - sizeof(MCode)), &exitno);
      if (stub) {  
	ctx.CONTEXT_REG_PC = stub;
	G2J(g)->exitcode = errcode;
	RtlRestoreContext(&ctx, NULL);  
      }
      break;
    }
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, addr, func,
		     &ctx, &hdata, &frame, NULL);
    if (!addr) break;
  }
  
}
#endif


static void err_raise_ext(global_State *g, int errcode)
{
#if LJ_UNWIND_JIT
  if (tvref(g->jit_base)) {
    err_unwind_win_jit(g, errcode);
    return;  
  }
#elif LJ_HASJIT
  
  setmref(g->jit_base, NULL);
#endif
  UNUSED(g);
  RaiseException(LJ_EXCODE_MAKE(errcode), 1 , 0, NULL);
}

#elif !LJ_NO_UNWIND && (defined(__GNUC__) || defined(__clang__))



typedef struct _Unwind_Context _Unwind_Context;

#define _URC_OK			0
#define _URC_FATAL_PHASE2_ERROR	2
#define _URC_FATAL_PHASE1_ERROR	3
#define _URC_HANDLER_FOUND	6
#define _URC_INSTALL_CONTEXT	7
#define _URC_CONTINUE_UNWIND	8
#define _URC_FAILURE		9

#define LJ_UEXCLASS		0x4c55414a49543200ULL	
#define LJ_UEXCLASS_MAKE(c)	(LJ_UEXCLASS | (uint64_t)(c))
#define LJ_UEXCLASS_CHECK(cl)	(((cl) ^ LJ_UEXCLASS) <= 0xff)
#define LJ_UEXCLASS_ERRCODE(cl)	((int)((cl) & 0xff))

#if !LJ_TARGET_ARM

typedef struct _Unwind_Exception
{
  uint64_t exclass;
  void (*excleanup)(int, struct _Unwind_Exception *);
  uintptr_t p1, p2;
} __attribute__((__aligned__)) _Unwind_Exception;
#define UNWIND_EXCEPTION_TYPE	_Unwind_Exception

extern uintptr_t _Unwind_GetCFA(_Unwind_Context *);
extern void _Unwind_SetGR(_Unwind_Context *, int, uintptr_t);
extern uintptr_t _Unwind_GetIP(_Unwind_Context *);
extern void _Unwind_SetIP(_Unwind_Context *, uintptr_t);
extern void _Unwind_DeleteException(_Unwind_Exception *);
extern int _Unwind_RaiseException(_Unwind_Exception *);

#define _UA_SEARCH_PHASE	1
#define _UA_CLEANUP_PHASE	2
#define _UA_HANDLER_FRAME	4
#define _UA_FORCE_UNWIND	8


LJ_FUNCA int lj_err_unwind_dwarf(int version, int actions,
  uint64_t uexclass, _Unwind_Exception *uex, _Unwind_Context *ctx)
{
  void *cf;
  lua_State *L;
  if (version != 1)
    return _URC_FATAL_PHASE1_ERROR;
  cf = (void *)_Unwind_GetCFA(ctx);
  L = cframe_L(cf);
  if ((actions & _UA_SEARCH_PHASE)) {
#if LJ_UNWIND_EXT
    if (err_unwind(L, cf, 0) == NULL)
      return _URC_CONTINUE_UNWIND;
#endif
    if (!LJ_UEXCLASS_CHECK(uexclass)) {
      setstrV(L, L->top++, lj_err_str(L, LJ_ERR_ERRCPP));
    }
    return _URC_HANDLER_FOUND;
  }
  if ((actions & _UA_CLEANUP_PHASE)) {
    int errcode;
    if (LJ_UEXCLASS_CHECK(uexclass)) {
      errcode = LJ_UEXCLASS_ERRCODE(uexclass);
    } else {
      if ((actions & _UA_HANDLER_FRAME))
	_Unwind_DeleteException(uex);
      errcode = LUA_ERRRUN;
    }
#if LJ_UNWIND_EXT
    cf = err_unwind(L, cf, errcode);
    if ((actions & _UA_FORCE_UNWIND)) {
      return _URC_CONTINUE_UNWIND;
    } else if (cf) {
      ASMFunction ip;
      _Unwind_SetGR(ctx, LJ_TARGET_EHRETREG, errcode);
      ip = cframe_unwind_ff(cf) ? lj_vm_unwind_ff_eh : lj_vm_unwind_c_eh;
      _Unwind_SetIP(ctx, (uintptr_t)lj_ptr_strip(ip));
      return _URC_INSTALL_CONTEXT;
    }
#if LJ_TARGET_X86ORX64
    else if ((actions & _UA_HANDLER_FRAME)) {
      
      _Unwind_SetGR(ctx, LJ_TARGET_EHRETREG, errcode);
      _Unwind_SetIP(ctx, (uintptr_t)lj_vm_unwind_rethrow);
      return _URC_INSTALL_CONTEXT;
    }
#endif
#else
    
    lj_err_throw(L, errcode);
#if LJ_TARGET_X64
#error "Broken build system -- only use the provided Makefiles!"
#endif
#endif
  }
  return _URC_CONTINUE_UNWIND;
}

#if LJ_UNWIND_EXT && defined(LUA_USE_ASSERT)
struct dwarf_eh_bases { void *tbase, *dbase, *func; };
extern const void *_Unwind_Find_FDE(void *pc, struct dwarf_eh_bases *bases);


void lj_err_verify(void)
{
#if !LJ_TARGET_OSX
  
  struct dwarf_eh_bases ehb;
  lj_assertX(_Unwind_Find_FDE((void *)lj_err_throw, &ehb), "broken build: external frame unwinding enabled, but missing -funwind-tables");
#endif
  
}
#endif

#if LJ_UNWIND_JIT

static int err_unwind_jit(int version, int actions,
  uint64_t uexclass, _Unwind_Exception *uex, _Unwind_Context *ctx)
{
  
  if (version != 1 || !LJ_UEXCLASS_CHECK(uexclass))
    return _URC_FATAL_PHASE1_ERROR;
  if ((actions & _UA_SEARCH_PHASE)) {
    return _URC_HANDLER_FOUND;
  }
  if ((actions & _UA_CLEANUP_PHASE)) {
    global_State *g = *(global_State **)(uex+1);
    ExitNo exitno;
    uintptr_t addr = _Unwind_GetIP(ctx);  
    uintptr_t stub = lj_trace_unwind(G2J(g), addr - sizeof(MCode), &exitno);
    lj_assertG(tvref(g->jit_base), "unexpected throw across mcode frame");
    if (stub) {  
      G2J(g)->exitcode = LJ_UEXCLASS_ERRCODE(uexclass);
#ifdef LJ_TARGET_MIPS
      _Unwind_SetGR(ctx, 4, stub);
      _Unwind_SetGR(ctx, 5, exitno);
      _Unwind_SetIP(ctx, (uintptr_t)(void *)lj_vm_unwind_stub);
#else
      _Unwind_SetIP(ctx, stub);
#endif
      return _URC_INSTALL_CONTEXT;
    }
    return _URC_FATAL_PHASE2_ERROR;
  }
  return _URC_FATAL_PHASE1_ERROR;
}


static const uint8_t err_frame_jit_template[] = {
#if LJ_BE
  0,0,0,
#endif
  LJ_64 ? 0x1c : 0x14,  
#if LJ_LE
  0,0,0,
#endif
  0,0,0,0, 1, 'z','P','R',0,  
  1, LJ_64 ? 0x78 : 0x7c, LJ_TARGET_EHRAREG,  
#if LJ_64
  10, 0, 0,0,0,0,0,0,0,0, 0x1b,  
  0,0,0,0,0,  
#else
  6, 0, 0,0,0,0, 0x1b,  
  0,  
#endif
#if LJ_BE
  0,0,0,
#endif
  LJ_64 ? 0x14 : 0x10,  
  0,0,0,
  LJ_64 ? 0x24 : 0x1c,  
  0,0,0,
  LJ_64 ? 0x14 : 0x10,  
#if LJ_LE
  0,0,0,
#endif
  0,0,0,0, 0, 0,0,0, 
#if LJ_64
  0,0,0,0,  
#endif
  0,0,0,0  
};

#define ERR_FRAME_JIT_OFS_HANDLER	0x12
#define ERR_FRAME_JIT_OFS_FDE		(LJ_64 ? 0x20 : 0x18)
#define ERR_FRAME_JIT_OFS_CODE_SIZE	(LJ_64 ? 0x2c : 0x24)
#if LJ_TARGET_OSX
#define ERR_FRAME_JIT_OFS_REGISTER	ERR_FRAME_JIT_OFS_FDE
#else
#define ERR_FRAME_JIT_OFS_REGISTER	0
#endif

extern void __register_frame(const void *);
extern void __deregister_frame(const void *);

uint8_t *lj_err_register_mcode(void *base, size_t sz, uint8_t *info)
{
  ASMFunction handler = (ASMFunction)err_unwind_jit;
  memcpy(info, err_frame_jit_template, sizeof(err_frame_jit_template));
#if LJ_ABI_PAUTH
#if LJ_TARGET_ARM64
  handler = ptrauth_auth_and_resign(handler,
    ptrauth_key_function_pointer, 0,
    ptrauth_key_process_independent_code, info + ERR_FRAME_JIT_OFS_HANDLER);
#else
#error "missing pointer authentication support for this architecture"
#endif
#endif
  memcpy(info + ERR_FRAME_JIT_OFS_HANDLER, &handler, sizeof(handler));
  *(uint32_t *)(info + ERR_FRAME_JIT_OFS_CODE_SIZE) =
    (uint32_t)(sz - sizeof(err_frame_jit_template) - (info - (uint8_t *)base));
  __register_frame(info + ERR_FRAME_JIT_OFS_REGISTER);
#ifdef LUA_USE_ASSERT
  {
    struct dwarf_eh_bases ehb;
    lj_assertX(_Unwind_Find_FDE(info + sizeof(err_frame_jit_template)+1, &ehb),
	       "bad JIT unwind table registration");
  }
#endif
  return info + sizeof(err_frame_jit_template);
}

void lj_err_deregister_mcode(void *base, size_t sz, uint8_t *info)
{
  UNUSED(base); UNUSED(sz);
  __deregister_frame(info + ERR_FRAME_JIT_OFS_REGISTER);
}
#endif

#else 

#define _US_VIRTUAL_UNWIND_FRAME	0
#define _US_UNWIND_FRAME_STARTING	1
#define _US_ACTION_MASK			3
#define _US_FORCE_UNWIND		8

typedef struct _Unwind_Control_Block _Unwind_Control_Block;
#define UNWIND_EXCEPTION_TYPE	_Unwind_Control_Block

struct _Unwind_Control_Block {
  uint64_t exclass;
  uint32_t misc[20];
};

extern int _Unwind_RaiseException(_Unwind_Control_Block *);
extern int __gnu_unwind_frame(_Unwind_Control_Block *, _Unwind_Context *);
extern int _Unwind_VRS_Set(_Unwind_Context *, int, uint32_t, int, void *);
extern int _Unwind_VRS_Get(_Unwind_Context *, int, uint32_t, int, void *);

static inline uint32_t _Unwind_GetGR(_Unwind_Context *ctx, int r)
{
  uint32_t v;
  _Unwind_VRS_Get(ctx, 0, r, 0, &v);
  return v;
}

static inline void _Unwind_SetGR(_Unwind_Context *ctx, int r, uint32_t v)
{
  _Unwind_VRS_Set(ctx, 0, r, 0, &v);
}

extern void lj_vm_unwind_ext(void);


LJ_FUNCA int lj_err_unwind_arm(int state, _Unwind_Control_Block *ucb,
			       _Unwind_Context *ctx)
{
  void *cf = (void *)_Unwind_GetGR(ctx, 13);
  lua_State *L = cframe_L(cf);
  int errcode;

  switch ((state & _US_ACTION_MASK)) {
  case _US_VIRTUAL_UNWIND_FRAME:
    if ((state & _US_FORCE_UNWIND)) break;
    return _URC_HANDLER_FOUND;
  case _US_UNWIND_FRAME_STARTING:
    if (LJ_UEXCLASS_CHECK(ucb->exclass)) {
      errcode = LJ_UEXCLASS_ERRCODE(ucb->exclass);
    } else {
      errcode = LUA_ERRRUN;
      setstrV(L, L->top++, lj_err_str(L, LJ_ERR_ERRCPP));
    }
    cf = err_unwind(L, cf, errcode);
    if ((state & _US_FORCE_UNWIND) || cf == NULL) break;
    _Unwind_SetGR(ctx, 15, (uint32_t)lj_vm_unwind_ext);
    _Unwind_SetGR(ctx, 0, (uint32_t)ucb);
    _Unwind_SetGR(ctx, 1, (uint32_t)errcode);
    _Unwind_SetGR(ctx, 2, cframe_unwind_ff(cf) ?
			    (uint32_t)lj_vm_unwind_ff_eh :
			    (uint32_t)lj_vm_unwind_c_eh);
    return _URC_INSTALL_CONTEXT;
  default:
    return _URC_FAILURE;
  }
  if (__gnu_unwind_frame(ucb, ctx) != _URC_OK)
    return _URC_FAILURE;
#ifdef LUA_USE_ASSERT
  
  if (_Unwind_GetGR(ctx, 0) == 0xff33aa77) {
    _Unwind_SetGR(ctx, 0, 0xff33aa88);
  }
#endif
  return _URC_CONTINUE_UNWIND;
}

#if LJ_UNWIND_EXT && defined(LUA_USE_ASSERT)
typedef int (*_Unwind_Trace_Fn)(_Unwind_Context *, void *);
extern int _Unwind_Backtrace(_Unwind_Trace_Fn, void *);

static int err_verify_bt(_Unwind_Context *ctx, int *got)
{
  if (_Unwind_GetGR(ctx, 0) == 0xff33aa88) { *got = 2; }
  else if (*got == 0) { *got = 1; _Unwind_SetGR(ctx, 0, 0xff33aa77); }
  return _URC_OK;
}


void lj_err_verify(void)
{
  int got = 0;
  _Unwind_Backtrace((_Unwind_Trace_Fn)err_verify_bt, &got);
  lj_assertX(got == 2, "broken build: external frame unwinding enabled, but missing -funwind-tables");
}
#endif



#endif 


#if LJ_UNWIND_EXT
static __thread struct {
  UNWIND_EXCEPTION_TYPE ex;
  global_State *g;
} static_uex;


static void err_raise_ext(global_State *g, int errcode)
{
  memset(&static_uex, 0, sizeof(static_uex));
  static_uex.ex.exclass = LJ_UEXCLASS_MAKE(errcode);
  static_uex.g = g;
  _Unwind_RaiseException(&static_uex.ex);
}

#endif

#endif




LJ_NOINLINE void LJ_FASTCALL lj_err_throw(lua_State *L, int errcode)
{
  global_State *g = G(L);
  lj_trace_abort(g);
  L->status = LUA_OK;
#if LJ_UNWIND_EXT
  err_raise_ext(g, errcode);
  
  if (G(L)->panic)
    G(L)->panic(L);
#else
#if LJ_HASJIT
  setmref(g->jit_base, NULL);
#endif
  {
    void *cf = err_unwind(L, NULL, errcode);
    if (cframe_unwind_ff(cf))
      lj_vm_unwind_ff(cframe_raw(cf));
    else
      lj_vm_unwind_c(cframe_raw(cf), errcode);
  }
#endif
  exit(EXIT_FAILURE);
}


LJ_NOINLINE GCstr *lj_err_str(lua_State *L, ErrMsg em)
{
  return lj_str_newz(L, err2msg(em));
}

LJ_NORET LJ_NOINLINE static void lj_err_err(lua_State *L)
{
  setstrV(L, L->top++, lj_err_str(L, LJ_ERR_ERRERR));
  lj_err_throw(L, LUA_ERRERR);
}


LJ_NOINLINE void lj_err_mem(lua_State *L)
{
  if (L->status == LUA_ERRERR)
    lj_err_err(L);
  if (L->status == LUA_ERRERR+1)  
    lj_vm_unwind_c(L->cframe, LUA_ERRMEM);
  if (LJ_HASJIT) {
    TValue *base = tvref(G(L)->jit_base);
    if (base) L->base = base;
  }
  if (curr_funcisL(L)) {
    L->top = curr_topL(L);
    if (LJ_UNLIKELY(L->top > tvref(L->maxstack))) {
      
      L->top = L->base;
      setframe_gc(L->base - 1 - LJ_FR2, obj2gco(L), LJ_TTHREAD);
    }
  }
  setstrV(L, L->top++, lj_err_str(L, LJ_ERR_ERRMEM));
  lj_err_throw(L, LUA_ERRMEM);
}


static ptrdiff_t finderrfunc(lua_State *L)
{
  cTValue *frame = L->base-1, *bot = tvref(L->stack)+LJ_FR2;
  void *cf = L->cframe;
  while (frame > bot && cf) {
    while (cframe_nres(cframe_raw(cf)) < 0) {  
      if (frame >= restorestack(L, -cframe_nres(cf)))
	break;
      if (cframe_errfunc(cf) >= 0)  
	return cframe_errfunc(cf);
      cf = cframe_prev(cf);  
      if (cf == NULL)
	return 0;
    }
    switch (frame_typep(frame)) {
    case FRAME_LUA:
    case FRAME_LUAP:
      frame = frame_prevl(frame);
      break;
    case FRAME_C:
      cf = cframe_prev(cf);
      
    case FRAME_VARG:
      frame = frame_prevd(frame);
      break;
    case FRAME_CONT:
      if (frame_iscont_fficb(frame))
	cf = cframe_prev(cf);
      frame = frame_prevd(frame);
      break;
    case FRAME_CP:
      if (cframe_canyield(cf)) return 0;
      if (cframe_errfunc(cf) >= 0)
	return cframe_errfunc(cf);
      cf = cframe_prev(cf);
      frame = frame_prevd(frame);
      break;
    case FRAME_PCALL:
    case FRAME_PCALLH:
      if (frame_func(frame_prevd(frame))->c.ffid == FF_xpcall)
	return savestack(L, frame_prevd(frame)+1);  
      return 0;
    default:
      lj_assertL(0, "bad frame type");
      return 0;
    }
  }
  return 0;
}


LJ_NOINLINE void LJ_FASTCALL lj_err_run(lua_State *L)
{
  ptrdiff_t ef = (LJ_HASJIT && tvref(G(L)->jit_base)) ? 0 : finderrfunc(L);
  if (ef) {
    TValue *errfunc, *top;
    lj_state_checkstack(L, LUA_MINSTACK * 2);  
    lj_trace_abort(G(L));
    errfunc = restorestack(L, ef);
    top = L->top;
    if (!tvisfunc(errfunc) || L->status == LUA_ERRERR) {
      setstrV(L, top-1, lj_err_str(L, LJ_ERR_ERRERR));
      lj_err_throw(L, LUA_ERRERR);
    }
    L->status = LUA_ERRERR;
    copyTV(L, top+LJ_FR2, top-1);
    copyTV(L, top-1, errfunc);
    if (LJ_FR2) setnilV(top++);
    L->top = top+1;
    lj_vm_call(L, top, 1+1);  
  }
  lj_err_throw(L, LUA_ERRRUN);
}


void LJ_FASTCALL lj_err_stkov(lua_State *L)
{
  if (L->status == LUA_ERRERR)
    lj_err_err(L);
  lj_debug_addloc(L, err2msg(LJ_ERR_STKOV), L->base-1, NULL);
  lj_err_run(L);
}

#if LJ_HASJIT

LJ_NOINLINE void LJ_FASTCALL lj_err_trace(lua_State *L, int errcode)
{
  if (errcode == LUA_ERRRUN)
    lj_err_run(L);
  else
    lj_err_throw(L, errcode);
}
#endif


LJ_NORET LJ_NOINLINE static void err_msgv(lua_State *L, ErrMsg em, ...)
{
  const char *msg;
  va_list argp;
  va_start(argp, em);
  if (LJ_HASJIT) {
    TValue *base = tvref(G(L)->jit_base);
    if (base) L->base = base;
  }
  if (curr_funcisL(L)) L->top = curr_topL(L);
  msg = lj_strfmt_pushvf(L, err2msg(em), argp);
  va_end(argp);
  lj_debug_addloc(L, msg, L->base-1, NULL);
  lj_err_run(L);
}


LJ_NOINLINE void lj_err_msg(lua_State *L, ErrMsg em)
{
  err_msgv(L, em);
}


LJ_NOINLINE void lj_err_lex(lua_State *L, GCstr *src, const char *tok,
			    BCLine line, ErrMsg em, va_list argp)
{
  char buff[LUA_IDSIZE];
  const char *msg;
  lj_debug_shortname(buff, src, line);
  msg = lj_strfmt_pushvf(L, err2msg(em), argp);
  msg = lj_strfmt_pushf(L, "%s:%d: %s", buff, line, msg);
  if (tok)
    lj_strfmt_pushf(L, err2msg(LJ_ERR_XNEAR), msg, tok);
  lj_err_throw(L, LUA_ERRSYNTAX);
}


LJ_NOINLINE void lj_err_optype(lua_State *L, cTValue *o, ErrMsg opm)
{
  const char *tname = lj_typename(o);
  const char *opname = err2msg(opm);
  if (curr_funcisL(L)) {
    GCproto *pt = curr_proto(L);
    const BCIns *pc = cframe_Lpc(L) - 1;
    const char *oname = NULL;
    const char *kind = lj_debug_slotname(pt, pc, (BCReg)(o-L->base), &oname);
    if (kind)
      err_msgv(L, LJ_ERR_BADOPRT, opname, kind, oname, tname);
  }
  err_msgv(L, LJ_ERR_BADOPRV, opname, tname);
}


LJ_NOINLINE void lj_err_comp(lua_State *L, cTValue *o1, cTValue *o2)
{
  const char *t1 = lj_typename(o1);
  const char *t2 = lj_typename(o2);
  err_msgv(L, t1 == t2 ? LJ_ERR_BADCMPV : LJ_ERR_BADCMPT, t1, t2);
  
}


LJ_NOINLINE void lj_err_optype_call(lua_State *L, TValue *o)
{
  
  const BCIns *pc = cframe_Lpc(L);
  if (((ptrdiff_t)pc & FRAME_TYPE) != FRAME_LUA) {
    const char *tname = lj_typename(o);
    setframe_gc(o, obj2gco(L), LJ_TTHREAD);
    if (LJ_FR2) o++;
    setframe_pc(o, pc);
    L->top = L->base = o+1;
    err_msgv(L, LJ_ERR_BADCALL, tname);
  }
  lj_err_optype(L, o, LJ_ERR_OPCALL);
}


LJ_NOINLINE void lj_err_callermsg(lua_State *L, const char *msg)
{
  TValue *frame = NULL, *pframe = NULL;
  if (!(LJ_HASJIT && tvref(G(L)->jit_base))) {
    frame = L->base-1;
    if (frame_islua(frame)) {
      pframe = frame_prevl(frame);
    } else if (frame_iscont(frame)) {
      if (frame_iscont_fficb(frame)) {
	pframe = frame;
	frame = NULL;
      } else {
	pframe = frame_prevd(frame);
#if LJ_HASFFI
	
	if (frame_func(frame)->c.ffid >= FF_ffi_meta___index &&
	    frame_func(frame)->c.ffid <= FF_ffi_meta___tostring) {
	  L->base = pframe+1;
	  L->top = frame;
	  setcframe_pc(cframe_raw(L->cframe), frame_contpc(frame));
	}
#endif
      }
    }
  }
  lj_debug_addloc(L, msg, pframe, frame);
  lj_err_run(L);
}


LJ_NOINLINE void lj_err_callerv(lua_State *L, ErrMsg em, ...)
{
  const char *msg;
  va_list argp;
  va_start(argp, em);
  msg = lj_strfmt_pushvf(L, err2msg(em), argp);
  va_end(argp);
  lj_err_callermsg(L, msg);
}


LJ_NOINLINE void lj_err_caller(lua_State *L, ErrMsg em)
{
  lj_err_callermsg(L, err2msg(em));
}


LJ_NORET LJ_NOINLINE static void err_argmsg(lua_State *L, int narg,
					    const char *msg)
{
  const char *fname = "?";
  const char *ftype = lj_debug_funcname(L, L->base - 1, &fname);
  if (narg < 0 && narg > LUA_REGISTRYINDEX)
    narg = (int)(L->top - L->base) + narg + 1;
  if (ftype && ftype[3] == 'h' && --narg == 0)  
    msg = lj_strfmt_pushf(L, err2msg(LJ_ERR_BADSELF), fname, msg);
  else
    msg = lj_strfmt_pushf(L, err2msg(LJ_ERR_BADARG), narg, fname, msg);
  lj_err_callermsg(L, msg);
}


LJ_NOINLINE void lj_err_argv(lua_State *L, int narg, ErrMsg em, ...)
{
  const char *msg;
  va_list argp;
  va_start(argp, em);
  msg = lj_strfmt_pushvf(L, err2msg(em), argp);
  va_end(argp);
  err_argmsg(L, narg, msg);
}


LJ_NOINLINE void lj_err_arg(lua_State *L, int narg, ErrMsg em)
{
  err_argmsg(L, narg, err2msg(em));
}


LJ_NOINLINE void lj_err_argtype(lua_State *L, int narg, const char *xname)
{
  const char *tname, *msg;
  if (narg <= LUA_REGISTRYINDEX) {
    if (narg >= LUA_GLOBALSINDEX) {
      tname = lj_obj_itypename[~LJ_TTAB];
    } else {
      GCfunc *fn = curr_func(L);
      int idx = LUA_GLOBALSINDEX - narg;
      if (idx <= fn->c.nupvalues)
	tname = lj_typename(&fn->c.upvalue[idx-1]);
      else
	tname = lj_obj_typename[0];
    }
  } else {
    TValue *o = narg < 0 ? L->top + narg : L->base + narg-1;
    tname = o < L->top ? lj_typename(o) : lj_obj_typename[0];
  }
  msg = lj_strfmt_pushf(L, err2msg(LJ_ERR_BADTYPE), xname, tname);
  err_argmsg(L, narg, msg);
}


LJ_NOINLINE void lj_err_argt(lua_State *L, int narg, int tt)
{
  lj_err_argtype(L, narg, lj_obj_typename[tt+1]);
}



LUA_API lua_CFunction lua_atpanic(lua_State *L, lua_CFunction panicf)
{
  lua_CFunction old = G(L)->panic;
  G(L)->panic = panicf;
  return old;
}


LUA_API int lua_error(lua_State *L)
{
  lj_err_run(L);
  return 0;  
}

LUALIB_API int luaL_argerror(lua_State *L, int narg, const char *msg)
{
  err_argmsg(L, narg, msg);
  return 0;  
}

LUALIB_API int luaL_typerror(lua_State *L, int narg, const char *xname)
{
  lj_err_argtype(L, narg, xname);
  return 0;  
}

LUALIB_API void luaL_where(lua_State *L, int level)
{
  int size;
  cTValue *frame = lj_debug_frame(L, level, &size);
  lj_debug_addloc(L, "", frame, size ? frame+size : NULL);
}

LUALIB_API int luaL_error(lua_State *L, const char *fmt, ...)
{
  const char *msg;
  va_list argp;
  va_start(argp, fmt);
  msg = lj_strfmt_pushvf(L, fmt, argp);
  va_end(argp);
  lj_err_callermsg(L, msg);
  return 0;  
}

