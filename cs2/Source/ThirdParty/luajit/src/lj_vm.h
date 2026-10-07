

#ifndef _LJ_VM_H
#define _LJ_VM_H

#include "lj_obj.h"


LJ_ASMF void lj_vm_call(lua_State *L, TValue *base, int nres1);
LJ_ASMF int lj_vm_pcall(lua_State *L, TValue *base, int nres1, ptrdiff_t ef);
typedef TValue *(*lua_CPFunction)(lua_State *L, lua_CFunction func, void *ud);
LJ_ASMF int lj_vm_cpcall(lua_State *L, lua_CFunction func, void *ud,
			 lua_CPFunction cp);
LJ_ASMF int lj_vm_resume(lua_State *L, TValue *base, int nres1, ptrdiff_t ef);
LJ_ASMF_NORET void LJ_FASTCALL lj_vm_unwind_c(void *cframe, int errcode);
LJ_ASMF_NORET void LJ_FASTCALL lj_vm_unwind_ff(void *cframe);
#if LJ_ABI_WIN && LJ_TARGET_X86
LJ_ASMF_NORET void LJ_FASTCALL lj_vm_rtlunwind(void *cframe, void *excptrec,
					       void *unwinder, int errcode);
#endif
LJ_ASMF void lj_vm_unwind_c_eh(void);
LJ_ASMF void lj_vm_unwind_ff_eh(void);
#if LJ_TARGET_X86ORX64
LJ_ASMF void lj_vm_unwind_rethrow(void);
#endif
#if LJ_TARGET_MIPS
LJ_ASMF void lj_vm_unwind_stub(void);
#endif


#if LJ_TARGET_X86ORX64
LJ_ASMF int lj_vm_cpuid(uint32_t f, uint32_t res[4]);
#endif
#if LJ_TARGET_PPC
void lj_vm_cachesync(void *start, void *end);
#endif
LJ_ASMF LJ_CONSTF double lj_vm_foldarith(double x, double y, int op);
#if LJ_HASJIT
LJ_ASMF LJ_CONSTF double lj_vm_foldfpm(double x, int op);
#endif
#if LJ_SOFTFP && LJ_TARGET_MIPS64
LJ_ASMF int32_t lj_vm_tointg(double x);
#endif



LJ_ASMF void lj_vm_record(void);
LJ_ASMF void lj_vm_inshook(void);
LJ_ASMF void lj_vm_rethook(void);
LJ_ASMF void lj_vm_callhook(void);
LJ_ASMF void lj_vm_profhook(void);
LJ_ASMF void lj_vm_IITERN(void);


LJ_ASMF char lj_vm_exit_handler[];
LJ_ASMF char lj_vm_exit_interp[];


#if LJ_TARGET_PPC || LJ_TARGET_ARM64 || (LJ_TARGET_MIPS && LJ_ABI_SOFTFP)
#define lj_vm_floor	floor
#define lj_vm_ceil	ceil
#else
LJ_ASMF LJ_CONSTF double lj_vm_floor(double);
LJ_ASMF LJ_CONSTF double lj_vm_ceil(double);
#if LJ_TARGET_ARM
LJ_ASMF double lj_vm_floor_sf(double);
LJ_ASMF double lj_vm_ceil_sf(double);
#endif
#endif
#ifdef LUAJIT_NO_LOG2
LJ_ASMF LJ_CONSTF double lj_vm_log2(double);
#else
#define lj_vm_log2	log2
#endif
#if !(defined(_LJ_DISPATCH_H) && LJ_TARGET_MIPS)
LJ_ASMF int32_t LJ_FASTCALL lj_vm_modi(int32_t, int32_t);
#endif

#if LJ_HASJIT
#if LJ_TARGET_X86ORX64
LJ_ASMF void lj_vm_floor_sse(void);
LJ_ASMF void lj_vm_ceil_sse(void);
LJ_ASMF void lj_vm_trunc_sse(void);
#endif
#if LJ_TARGET_PPC || LJ_TARGET_ARM64
#define lj_vm_trunc	trunc
#else
LJ_ASMF LJ_CONSTF double lj_vm_trunc(double);
#if LJ_TARGET_ARM
LJ_ASMF double lj_vm_trunc_sf(double);
#endif
#endif
#if LJ_HASFFI
LJ_ASMF int lj_vm_errno(void);
#endif
LJ_ASMF TValue *lj_vm_next(GCtab *t, uint32_t idx);
#endif


LJ_ASMF void lj_cont_cat(void);  
LJ_ASMF void lj_cont_ra(void);  
LJ_ASMF void lj_cont_nop(void);  
LJ_ASMF void lj_cont_condt(void);  
LJ_ASMF void lj_cont_condf(void);  
LJ_ASMF void lj_cont_hook(void);  
LJ_ASMF void lj_cont_stitch(void);  


LJ_ASMF char lj_vm_asm_begin[];


#define makeasmfunc(ofs) lj_ptr_sign((ASMFunction)(lj_vm_asm_begin + (ofs)), 0)

#endif
