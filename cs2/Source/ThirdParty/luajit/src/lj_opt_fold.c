

#define lj_opt_fold_c
#define LUA_CORE

#include <math.h>

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_carith.h"
#endif
#include "lj_vm.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"




#define IR(ref)		(&J->cur.ir[(ref)])
#define fins		(&J->fold.ins)
#define fleft		(J->fold.left)
#define fright		(J->fold.right)
#define knumleft	(ir_knum(fleft)->n)
#define knumright	(ir_knum(fright)->n)


#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))


typedef IRRef (LJ_FASTCALL *FoldFunc)(jit_State *J);


#define LJFOLD(x)
#define LJFOLDX(x)
#define LJFOLDF(name)	static TRef LJ_FASTCALL fold_##name(jit_State *J)



#define PHIBARRIER(ir)	if (irt_isphi((ir)->t)) return NEXTFOLD


#define gcstep_barrier(J, ref) \
  ((ref) < J->chain[IR_LOOP] && \
   (J->chain[IR_SNEW] || J->chain[IR_XSNEW] || \
    J->chain[IR_TNEW] || J->chain[IR_TDUP] || \
    J->chain[IR_CNEW] || J->chain[IR_CNEWI] || \
    J->chain[IR_BUFSTR] || J->chain[IR_TOSTR] || J->chain[IR_CALLA]))



LJFOLD(ADD KNUM KNUM)
LJFOLD(SUB KNUM KNUM)
LJFOLD(MUL KNUM KNUM)
LJFOLD(DIV KNUM KNUM)
LJFOLD(LDEXP KNUM KNUM)
LJFOLD(MIN KNUM KNUM)
LJFOLD(MAX KNUM KNUM)
LJFOLDF(kfold_numarith)
{
  lua_Number a = knumleft;
  lua_Number b = knumright;
  lua_Number y = lj_vm_foldarith(a, b, fins->o - IR_ADD);
  return lj_ir_knum(J, y);
}

LJFOLD(NEG KNUM FLOAD)
LJFOLD(ABS KNUM FLOAD)
LJFOLDF(kfold_numabsneg)
{
  lua_Number a = knumleft;
  lua_Number y = lj_vm_foldarith(a, a, fins->o - IR_ADD);
  return lj_ir_knum(J, y);
}

LJFOLD(LDEXP KNUM KINT)
LJFOLDF(kfold_ldexp)
{
#if LJ_TARGET_X86ORX64
  UNUSED(J);
  return NEXTFOLD;
#else
  return lj_ir_knum(J, ldexp(knumleft, fright->i));
#endif
}

LJFOLD(FPMATH KNUM any)
LJFOLDF(kfold_fpmath)
{
  lua_Number a = knumleft;
  lua_Number y = lj_vm_foldfpm(a, fins->op2);
  return lj_ir_knum(J, y);
}

LJFOLD(CALLN KNUM any)
LJFOLDF(kfold_fpcall1)
{
  const CCallInfo *ci = &lj_ir_callinfo[fins->op2];
  if (CCI_TYPE(ci) == IRT_NUM) {
    double y = ((double (*)(double))ci->func)(knumleft);
    return lj_ir_knum(J, y);
  }
  return NEXTFOLD;
}

LJFOLD(CALLN CARG IRCALL_atan2)
LJFOLDF(kfold_fpcall2)
{
  if (irref_isk(fleft->op1) && irref_isk(fleft->op2)) {
    const CCallInfo *ci = &lj_ir_callinfo[fins->op2];
    double a = ir_knum(IR(fleft->op1))->n;
    double b = ir_knum(IR(fleft->op2))->n;
    double y = ((double (*)(double, double))ci->func)(a, b);
    return lj_ir_knum(J, y);
  }
  return NEXTFOLD;
}

LJFOLD(POW KNUM KNUM)
LJFOLDF(kfold_numpow)
{
  return lj_ir_knum(J, lj_vm_foldarith(knumleft, knumright, IR_POW - IR_ADD));
}


LJFOLD(EQ KNUM KNUM)
LJFOLD(NE KNUM KNUM)
LJFOLD(LT KNUM KNUM)
LJFOLD(GE KNUM KNUM)
LJFOLD(LE KNUM KNUM)
LJFOLD(GT KNUM KNUM)
LJFOLD(ULT KNUM KNUM)
LJFOLD(UGE KNUM KNUM)
LJFOLD(ULE KNUM KNUM)
LJFOLD(UGT KNUM KNUM)
LJFOLDF(kfold_numcomp)
{
  return CONDFOLD(lj_ir_numcmp(knumleft, knumright, (IROp)fins->o));
}



static int32_t kfold_intop(int32_t k1, int32_t k2, IROp op)
{
  switch (op) {
  case IR_ADD: k1 += k2; break;
  case IR_SUB: k1 -= k2; break;
  case IR_MUL: k1 *= k2; break;
  case IR_MOD: k1 = lj_vm_modi(k1, k2); break;
  case IR_NEG: k1 = (int32_t)(~(uint32_t)k1+1u); break;
  case IR_BAND: k1 &= k2; break;
  case IR_BOR: k1 |= k2; break;
  case IR_BXOR: k1 ^= k2; break;
  case IR_BSHL: k1 <<= (k2 & 31); break;
  case IR_BSHR: k1 = (int32_t)((uint32_t)k1 >> (k2 & 31)); break;
  case IR_BSAR: k1 >>= (k2 & 31); break;
  case IR_BROL: k1 = (int32_t)lj_rol((uint32_t)k1, (k2 & 31)); break;
  case IR_BROR: k1 = (int32_t)lj_ror((uint32_t)k1, (k2 & 31)); break;
  case IR_MIN: k1 = k1 < k2 ? k1 : k2; break;
  case IR_MAX: k1 = k1 > k2 ? k1 : k2; break;
  default: lj_assertX(0, "bad IR op %d", op); break;
  }
  return k1;
}

LJFOLD(ADD KINT KINT)
LJFOLD(SUB KINT KINT)
LJFOLD(MUL KINT KINT)
LJFOLD(MOD KINT KINT)
LJFOLD(NEG KINT KINT)
LJFOLD(BAND KINT KINT)
LJFOLD(BOR KINT KINT)
LJFOLD(BXOR KINT KINT)
LJFOLD(BSHL KINT KINT)
LJFOLD(BSHR KINT KINT)
LJFOLD(BSAR KINT KINT)
LJFOLD(BROL KINT KINT)
LJFOLD(BROR KINT KINT)
LJFOLD(MIN KINT KINT)
LJFOLD(MAX KINT KINT)
LJFOLDF(kfold_intarith)
{
  return INTFOLD(kfold_intop(fleft->i, fright->i, (IROp)fins->o));
}


static uint64_t kfold_int64arith(jit_State *J, uint64_t k1, uint64_t k2,
				 IROp op);

LJFOLD(ADDOV KINT KINT)
LJFOLD(SUBOV KINT KINT)
LJFOLD(MULOV KINT KINT)
LJFOLDF(kfold_intovarith)
{
  int64_t k = kfold_int64arith(J, (int64_t)fleft->i, (int64_t)fright->i,
		(IROp)((int)fins->o - (int)IR_ADDOV + (int)IR_ADD));
  return checki32(k) ? INTFOLD(k) : FAILFOLD;
}

LJFOLD(BNOT KINT)
LJFOLDF(kfold_bnot)
{
  return INTFOLD(~fleft->i);
}

LJFOLD(BSWAP KINT)
LJFOLDF(kfold_bswap)
{
  return INTFOLD((int32_t)lj_bswap((uint32_t)fleft->i));
}

LJFOLD(LT KINT KINT)
LJFOLD(GE KINT KINT)
LJFOLD(LE KINT KINT)
LJFOLD(GT KINT KINT)
LJFOLD(ULT KINT KINT)
LJFOLD(UGE KINT KINT)
LJFOLD(ULE KINT KINT)
LJFOLD(UGT KINT KINT)
LJFOLD(ABC KINT KINT)
LJFOLDF(kfold_intcomp)
{
  int32_t a = fleft->i, b = fright->i;
  switch ((IROp)fins->o) {
  case IR_LT: return CONDFOLD(a < b);
  case IR_GE: return CONDFOLD(a >= b);
  case IR_LE: return CONDFOLD(a <= b);
  case IR_GT: return CONDFOLD(a > b);
  case IR_ULT: return CONDFOLD((uint32_t)a < (uint32_t)b);
  case IR_UGE: return CONDFOLD((uint32_t)a >= (uint32_t)b);
  case IR_ULE: return CONDFOLD((uint32_t)a <= (uint32_t)b);
  case IR_ABC:
  case IR_UGT: return CONDFOLD((uint32_t)a > (uint32_t)b);
  default: lj_assertJ(0, "bad IR op %d", fins->o); return FAILFOLD;
  }
}

LJFOLD(UGE any KINT)
LJFOLDF(kfold_intcomp0)
{
  if (fright->i == 0)
    return DROPFOLD;
  return NEXTFOLD;
}



static uint64_t kfold_int64arith(jit_State *J, uint64_t k1, uint64_t k2,
				 IROp op)
{
  UNUSED(J);
  switch (op) {
  case IR_ADD: k1 += k2; break;
  case IR_SUB: k1 -= k2; break;
  case IR_MUL: k1 *= k2; break;
#if LJ_HASFFI
  case IR_BAND: k1 &= k2; break;
  case IR_BOR: k1 |= k2; break;
  case IR_BXOR: k1 ^= k2; break;
  case IR_BSHL: k1 <<= (k2 & 63); break;
  case IR_BSHR: k1 >>= (k2 & 63); break;
  case IR_BSAR: k1 = (uint64_t)((int64_t)k1 >> (k2 & 63)); break;
  case IR_BROL: k1 = lj_rol(k1, (k2 & 63)); break;
  case IR_BROR: k1 = lj_ror(k1, (k2 & 63)); break;
#endif
  default: lj_assertJ(0, "bad IR op %d", op); break;
  }
  return k1;
}

LJFOLD(ADD KINT64 KINT64)
LJFOLD(SUB KINT64 KINT64)
LJFOLD(MUL KINT64 KINT64)
LJFOLD(BAND KINT64 KINT64)
LJFOLD(BOR KINT64 KINT64)
LJFOLD(BXOR KINT64 KINT64)
LJFOLDF(kfold_int64arith)
{
  return INT64FOLD(kfold_int64arith(J, ir_k64(fleft)->u64,
				    ir_k64(fright)->u64, (IROp)fins->o));
}

LJFOLD(DIV KINT64 KINT64)
LJFOLD(MOD KINT64 KINT64)
LJFOLD(POW KINT64 KINT64)
LJFOLDF(kfold_int64arith2)
{
#if LJ_HASFFI
  uint64_t k1 = ir_k64(fleft)->u64, k2 = ir_k64(fright)->u64;
  if (irt_isi64(fins->t)) {
    k1 = fins->o == IR_DIV ? lj_carith_divi64((int64_t)k1, (int64_t)k2) :
	 fins->o == IR_MOD ? lj_carith_modi64((int64_t)k1, (int64_t)k2) :
			     lj_carith_powi64((int64_t)k1, (int64_t)k2);
  } else {
    k1 = fins->o == IR_DIV ? lj_carith_divu64(k1, k2) :
	 fins->o == IR_MOD ? lj_carith_modu64(k1, k2) :
			     lj_carith_powu64(k1, k2);
  }
  return INT64FOLD(k1);
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(BSHL KINT64 KINT)
LJFOLD(BSHR KINT64 KINT)
LJFOLD(BSAR KINT64 KINT)
LJFOLD(BROL KINT64 KINT)
LJFOLD(BROR KINT64 KINT)
LJFOLDF(kfold_int64shift)
{
#if LJ_HASFFI
  uint64_t k = ir_k64(fleft)->u64;
  int32_t sh = (fright->i & 63);
  return INT64FOLD(lj_carith_shift64(k, sh, fins->o - IR_BSHL));
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(BNOT KINT64)
LJFOLDF(kfold_bnot64)
{
#if LJ_HASFFI
  return INT64FOLD(~ir_k64(fleft)->u64);
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(BSWAP KINT64)
LJFOLDF(kfold_bswap64)
{
#if LJ_HASFFI
  return INT64FOLD(lj_bswap64(ir_k64(fleft)->u64));
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(LT KINT64 KINT64)
LJFOLD(GE KINT64 KINT64)
LJFOLD(LE KINT64 KINT64)
LJFOLD(GT KINT64 KINT64)
LJFOLD(ULT KINT64 KINT64)
LJFOLD(UGE KINT64 KINT64)
LJFOLD(ULE KINT64 KINT64)
LJFOLD(UGT KINT64 KINT64)
LJFOLDF(kfold_int64comp)
{
#if LJ_HASFFI
  uint64_t a = ir_k64(fleft)->u64, b = ir_k64(fright)->u64;
  switch ((IROp)fins->o) {
  case IR_LT: return CONDFOLD((int64_t)a < (int64_t)b);
  case IR_GE: return CONDFOLD((int64_t)a >= (int64_t)b);
  case IR_LE: return CONDFOLD((int64_t)a <= (int64_t)b);
  case IR_GT: return CONDFOLD((int64_t)a > (int64_t)b);
  case IR_ULT: return CONDFOLD(a < b);
  case IR_UGE: return CONDFOLD(a >= b);
  case IR_ULE: return CONDFOLD(a <= b);
  case IR_UGT: return CONDFOLD(a > b);
  default: lj_assertJ(0, "bad IR op %d", fins->o); return FAILFOLD;
  }
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(UGE any KINT64)
LJFOLDF(kfold_int64comp0)
{
#if LJ_HASFFI
  if (ir_k64(fright)->u64 == 0)
    return DROPFOLD;
  return NEXTFOLD;
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}



LJFOLD(SNEW KKPTR KINT)
LJFOLDF(kfold_snew_kptr)
{
  GCstr *s = lj_str_new(J->L, (const char *)ir_kptr(fleft), (size_t)fright->i);
  return lj_ir_kstr(J, s);
}

LJFOLD(SNEW any KINT)
LJFOLD(XSNEW any KINT)
LJFOLDF(kfold_snew_empty)
{
  if (fright->i == 0)
    return lj_ir_kstr(J, &J2G(J)->strempty);
  return NEXTFOLD;
}

LJFOLD(STRREF KGC KINT)
LJFOLDF(kfold_strref)
{
  GCstr *str = ir_kstr(fleft);
  lj_assertJ((MSize)fright->i <= str->len, "bad string ref");
  return lj_ir_kkptr(J, (char *)strdata(str) + fright->i);
}

LJFOLD(STRREF SNEW any)
LJFOLDF(kfold_strref_snew)
{
  PHIBARRIER(fleft);
  if (irref_isk(fins->op2) && fright->i == 0) {
    return fleft->op1;  
  } else {
    
    IRIns *ir = IR(fleft->op1);
    if (ir->o == IR_STRREF) {
      IRRef1 str = ir->op1;  
      PHIBARRIER(ir);
      fins->op2 = emitir(IRTI(IR_ADD), ir->op2, fins->op2); 
      fins->op1 = str;
      fins->ot = IRT(IR_STRREF, IRT_PGC);
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(CALLN CARG IRCALL_lj_str_cmp)
LJFOLDF(kfold_strcmp)
{
  if (irref_isk(fleft->op1) && irref_isk(fleft->op2)) {
    GCstr *a = ir_kstr(IR(fleft->op1));
    GCstr *b = ir_kstr(IR(fleft->op2));
    return INTFOLD(lj_str_cmp(a, b));
  }
  return NEXTFOLD;
}





LJFOLD(BUFHDR any any)
LJFOLDF(bufhdr_merge)
{
  return fins->op2 == IRBUFHDR_WRITE ? CSEFOLD : EMITFOLD;
}

LJFOLD(BUFPUT any BUFSTR)
LJFOLDF(bufput_bufstr)
{
  if ((J->flags & JIT_F_OPT_FWD)) {
    IRRef hdr = fright->op2;
    
    if (fleft->o == IR_BUFHDR && fleft->op2 == IRBUFHDR_RESET &&
	fleft->prev == hdr &&
	fleft->op1 == IR(hdr)->op1 &&
	!(irt_isphi(fright->t) && IR(hdr)->prev) &&
	(!LJ_HASBUFFER || J->chain[IR_CALLA] < hdr)) {
      IRRef ref = fins->op1;
      IR(ref)->op2 = IRBUFHDR_APPEND;  
      IR(ref)->op1 = fright->op1;
      return ref;
    }
    
    if (IR(hdr)->op2 == IRBUFHDR_RESET && !irt_isphi(fright->t)) {
      IRIns *ir = IR(fright->op1);
      
      if (ir->o == IR_CALLL &&
	  ir->op2 >= IRCALL_lj_buf_putstr_reverse &&
	  ir->op2 <= IRCALL_lj_buf_putstr_rep) {
	IRIns *carg1 = IR(ir->op1);
	if (ir->op2 == IRCALL_lj_buf_putstr_rep) {
	  IRIns *carg2 = IR(carg1->op1);
	  if (carg2->op1 == hdr) {
	    return lj_ir_call(J, ir->op2, fins->op1, carg2->op2, carg1->op2);
	  }
	} else if (carg1->op1 == hdr) {
	  return lj_ir_call(J, ir->op2, fins->op1, carg1->op2);
	}
      }
    }
  }
  return EMITFOLD;  
}

LJFOLD(BUFPUT any any)
LJFOLDF(bufput_kgc)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD) && fright->o == IR_KGC) {
    GCstr *s2 = ir_kstr(fright);
    if (s2->len == 0) {  
      return LEFTFOLD;
    } else {
      if (fleft->o == IR_BUFPUT && irref_isk(fleft->op2) &&
	  !irt_isphi(fleft->t)) {  
	GCstr *s1 = ir_kstr(IR(fleft->op2));
	IRRef kref = lj_ir_kstr(J, lj_buf_cat2str(J->L, s1, s2));
	
	IR(fins->op1)->op2 = kref;  
	return fins->op1;
      }
    }
  }
  return EMITFOLD;  
}

LJFOLD(BUFSTR any any)
LJFOLDF(bufstr_kfold_cse)
{
  lj_assertJ(fleft->o == IR_BUFHDR || fleft->o == IR_BUFPUT ||
	     fleft->o == IR_CALLL,
	     "bad buffer constructor IR op %d", fleft->o);
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD)) {
    if (fleft->o == IR_BUFHDR) {  
      if (fleft->op2 == IRBUFHDR_RESET)  
	return lj_ir_kstr(J, &J2G(J)->strempty);
      fins->op1 = fleft->op1;
      fins->op2 = fleft->prev;  
      return CSEFOLD;
    } else if (fleft->o == IR_BUFPUT) {
      IRIns *irb = IR(fleft->op1);
      if (irb->o == IR_BUFHDR && irb->op2 == IRBUFHDR_RESET)
	return fleft->op2;  
    }
  }
  
  if (LJ_LIKELY(J->flags & JIT_F_OPT_CSE)) {
    IRRef ref = J->chain[IR_BUFSTR];
    while (ref) {
      IRIns *irs = IR(ref), *ira = fleft, *irb = IR(irs->op1);
      while (ira->o == irb->o && ira->op2 == irb->op2) {
	lj_assertJ(ira->o == IR_BUFHDR || ira->o == IR_BUFPUT ||
		   ira->o == IR_CALLL || ira->o == IR_CARG,
		   "bad buffer constructor IR op %d", ira->o);
	if (ira->o == IR_BUFHDR && ira->op2 == IRBUFHDR_RESET)
	  return ref;  
	if (ira->o == IR_CALLL && ira->op2 == IRCALL_lj_buf_puttab)
	  break;
	ira = IR(ira->op1);
	irb = IR(irb->op1);
      }
      ref = irs->prev;
    }
  }
  return EMITFOLD;  
}

LJFOLD(CALLL CARG IRCALL_lj_buf_putstr_reverse)
LJFOLD(CALLL CARG IRCALL_lj_buf_putstr_upper)
LJFOLD(CALLL CARG IRCALL_lj_buf_putstr_lower)
LJFOLD(CALLL CARG IRCALL_lj_strfmt_putquoted)
LJFOLDF(bufput_kfold_op)
{
  if (irref_isk(fleft->op2)) {
    const CCallInfo *ci = &lj_ir_callinfo[fins->op2];
    SBuf *sb = lj_buf_tmp_(J->L);
    sb = ((SBuf * (LJ_FASTCALL *)(SBuf *, GCstr *))ci->func)(sb,
						       ir_kstr(IR(fleft->op2)));
    fins->o = IR_BUFPUT;
    fins->op1 = fleft->op1;
    fins->op2 = lj_ir_kstr(J, lj_buf_tostr(sb));
    return RETRYFOLD;
  }
  return EMITFOLD;  
}

LJFOLD(CALLL CARG IRCALL_lj_buf_putstr_rep)
LJFOLDF(bufput_kfold_rep)
{
  if (irref_isk(fleft->op2)) {
    IRIns *irc = IR(fleft->op1);
    if (irref_isk(irc->op2)) {
      SBuf *sb = lj_buf_tmp_(J->L);
      sb = lj_buf_putstr_rep(sb, ir_kstr(IR(irc->op2)), IR(fleft->op2)->i);
      fins->o = IR_BUFPUT;
      fins->op1 = irc->op1;
      fins->op2 = lj_ir_kstr(J, lj_buf_tostr(sb));
      return RETRYFOLD;
    }
  }
  return EMITFOLD;  
}

LJFOLD(CALLL CARG IRCALL_lj_strfmt_putfxint)
LJFOLD(CALLL CARG IRCALL_lj_strfmt_putfnum_int)
LJFOLD(CALLL CARG IRCALL_lj_strfmt_putfnum_uint)
LJFOLD(CALLL CARG IRCALL_lj_strfmt_putfnum)
LJFOLD(CALLL CARG IRCALL_lj_strfmt_putfstr)
LJFOLD(CALLL CARG IRCALL_lj_strfmt_putfchar)
LJFOLDF(bufput_kfold_fmt)
{
  IRIns *irc = IR(fleft->op1);
  lj_assertJ(irref_isk(irc->op2), "SFormat must be const");
  if (irref_isk(fleft->op2)) {
    SFormat sf = (SFormat)IR(irc->op2)->i;
    IRIns *ira = IR(fleft->op2);
    SBuf *sb = lj_buf_tmp_(J->L);
    switch (fins->op2) {
    case IRCALL_lj_strfmt_putfxint:
      sb = lj_strfmt_putfxint(sb, sf, ir_k64(ira)->u64);
      break;
    case IRCALL_lj_strfmt_putfstr:
      sb = lj_strfmt_putfstr(sb, sf, ir_kstr(ira));
      break;
    case IRCALL_lj_strfmt_putfchar:
      sb = lj_strfmt_putfchar(sb, sf, ira->i);
      break;
    case IRCALL_lj_strfmt_putfnum_int:
    case IRCALL_lj_strfmt_putfnum_uint:
    case IRCALL_lj_strfmt_putfnum:
    default: {
      const CCallInfo *ci = &lj_ir_callinfo[fins->op2];
      sb = ((SBuf * (*)(SBuf *, SFormat, lua_Number))ci->func)(sb, sf,
							 ir_knum(ira)->n);
      break;
      }
    }
    fins->o = IR_BUFPUT;
    fins->op1 = irc->op1;
    fins->op2 = lj_ir_kstr(J, lj_buf_tostr(sb));
    return RETRYFOLD;
  }
  return EMITFOLD;  
}



LJFOLD(ADD KGC KINT)
LJFOLD(ADD KGC KINT64)
LJFOLDF(kfold_add_kgc)
{
  GCobj *o = ir_kgc(fleft);
#if LJ_64
  ptrdiff_t ofs = (ptrdiff_t)ir_kint64(fright)->u64;
#else
  ptrdiff_t ofs = fright->i;
#endif
#if LJ_HASFFI
  if (irt_iscdata(fleft->t)) {
    CType *ct = ctype_raw(ctype_ctsG(J2G(J)), gco2cd(o)->ctypeid);
    if (ctype_isnum(ct->info) || ctype_isenum(ct->info) ||
	ctype_isptr(ct->info) || ctype_isfunc(ct->info) ||
	ctype_iscomplex(ct->info) || ctype_isvector(ct->info))
      return lj_ir_kkptr(J, (char *)o + ofs);
  }
#endif
  return lj_ir_kptr(J, (char *)o + ofs);
}

LJFOLD(ADD KPTR KINT)
LJFOLD(ADD KPTR KINT64)
LJFOLD(ADD KKPTR KINT)
LJFOLD(ADD KKPTR KINT64)
LJFOLDF(kfold_add_kptr)
{
  void *p = ir_kptr(fleft);
#if LJ_64
  ptrdiff_t ofs = (ptrdiff_t)ir_kint64(fright)->u64;
#else
  ptrdiff_t ofs = fright->i;
#endif
  return lj_ir_kptr_(J, fleft->o, (char *)p + ofs);
}

LJFOLD(ADD any KGC)
LJFOLD(ADD any KPTR)
LJFOLD(ADD any KKPTR)
LJFOLDF(kfold_add_kright)
{
  if (fleft->o == IR_KINT || fleft->o == IR_KINT64) {
    IRRef1 tmp = fins->op1; fins->op1 = fins->op2; fins->op2 = tmp;
    return RETRYFOLD;
  }
  return NEXTFOLD;
}



LJFOLD(TOBIT KNUM KNUM)
LJFOLDF(kfold_tobit)
{
  return INTFOLD(lj_num2bit(knumleft));
}

LJFOLD(CONV KINT IRCONV_NUM_INT)
LJFOLDF(kfold_conv_kint_num)
{
  return lj_ir_knum(J, (lua_Number)fleft->i);
}

LJFOLD(CONV KINT IRCONV_NUM_U32)
LJFOLDF(kfold_conv_kintu32_num)
{
  return lj_ir_knum(J, (lua_Number)(uint32_t)fleft->i);
}

LJFOLD(CONV KINT IRCONV_INT_I8)
LJFOLD(CONV KINT IRCONV_INT_U8)
LJFOLD(CONV KINT IRCONV_INT_I16)
LJFOLD(CONV KINT IRCONV_INT_U16)
LJFOLDF(kfold_conv_kint_ext)
{
  int32_t k = fleft->i;
  if ((fins->op2 & IRCONV_SRCMASK) == IRT_I8) k = (int8_t)k;
  else if ((fins->op2 & IRCONV_SRCMASK) == IRT_U8) k = (uint8_t)k;
  else if ((fins->op2 & IRCONV_SRCMASK) == IRT_I16) k = (int16_t)k;
  else k = (uint16_t)k;
  return INTFOLD(k);
}

LJFOLD(CONV KINT IRCONV_I64_INT)
LJFOLD(CONV KINT IRCONV_U64_INT)
LJFOLD(CONV KINT IRCONV_I64_U32)
LJFOLD(CONV KINT IRCONV_U64_U32)
LJFOLDF(kfold_conv_kint_i64)
{
  if ((fins->op2 & IRCONV_SEXT))
    return INT64FOLD((uint64_t)(int64_t)fleft->i);
  else
    return INT64FOLD((uint64_t)(int64_t)(uint32_t)fleft->i);
}

LJFOLD(CONV KINT64 IRCONV_NUM_I64)
LJFOLDF(kfold_conv_kint64_num_i64)
{
  return lj_ir_knum(J, (lua_Number)(int64_t)ir_kint64(fleft)->u64);
}

LJFOLD(CONV KINT64 IRCONV_NUM_U64)
LJFOLDF(kfold_conv_kint64_num_u64)
{
  return lj_ir_knum(J, (lua_Number)ir_kint64(fleft)->u64);
}

LJFOLD(CONV KINT64 IRCONV_INT_I64)
LJFOLD(CONV KINT64 IRCONV_U32_I64)
LJFOLDF(kfold_conv_kint64_int_i64)
{
  return INTFOLD((int32_t)ir_kint64(fleft)->u64);
}

LJFOLD(CONV KNUM IRCONV_INT_NUM)
LJFOLDF(kfold_conv_knum_int_num)
{
  lua_Number n = knumleft;
  if (irt_isguard(fins->t)) {
    int64_t i64;
    int32_t k;
    if (lj_num2int_check(n, i64, k))
      return INTFOLD(k);
    
    return FAILFOLD;
  } else {
    return INTFOLD(lj_num2int(n));
  }
}

LJFOLD(CONV KNUM IRCONV_I64_NUM)
LJFOLDF(kfold_conv_knum_i64_num)
{
  return INT64FOLD((uint64_t)lj_num2i64(knumleft));
}

LJFOLD(CONV KNUM IRCONV_U64_NUM)
LJFOLDF(kfold_conv_knum_u64_num)
{
  return INT64FOLD(lj_num2u64(knumleft));
}

LJFOLD(TOSTR KNUM any)
LJFOLDF(kfold_tostr_knum)
{
  return lj_ir_kstr(J, lj_strfmt_num(J->L, ir_knum(fleft)));
}

LJFOLD(TOSTR KINT any)
LJFOLDF(kfold_tostr_kint)
{
  return lj_ir_kstr(J, fins->op2 == IRTOSTR_INT ?
		       lj_strfmt_int(J->L, fleft->i) :
		       lj_strfmt_char(J->L, fleft->i));
}

LJFOLD(STRTO KGC)
LJFOLDF(kfold_strto)
{
  TValue n;
  if (lj_strscan_num(ir_kstr(fleft), &n))
    return lj_ir_knum(J, numV(&n));
  return FAILFOLD;
}




LJFOLD(EQ FLOAD KNULL)
LJFOLD(NE FLOAD KNULL)
LJFOLDX(lj_opt_cse)


LJFOLD(EQ any KNULL)
LJFOLD(NE any KNULL)
LJFOLD(EQ KNULL any)
LJFOLD(NE KNULL any)
LJFOLD(EQ KINT KINT)  
LJFOLD(NE KINT KINT)
LJFOLD(EQ KINT64 KINT64)
LJFOLD(NE KINT64 KINT64)
LJFOLD(EQ KGC KGC)
LJFOLD(NE KGC KGC)
LJFOLDF(kfold_kref)
{
  return CONDFOLD((fins->op1 == fins->op2) ^ (fins->o == IR_NE));
}



LJFOLD(FPMATH FPMATH IRFPM_FLOOR)
LJFOLD(FPMATH FPMATH IRFPM_CEIL)
LJFOLD(FPMATH FPMATH IRFPM_TRUNC)
LJFOLDF(shortcut_round)
{
  IRFPMathOp op = (IRFPMathOp)fleft->op2;
  if (op == IRFPM_FLOOR || op == IRFPM_CEIL || op == IRFPM_TRUNC)
    return LEFTFOLD;  
  return NEXTFOLD;
}

LJFOLD(ABS ABS FLOAD)
LJFOLDF(shortcut_left)
{
  return LEFTFOLD;  
}

LJFOLD(ABS NEG FLOAD)
LJFOLDF(shortcut_dropleft)
{
  PHIBARRIER(fleft);
  fins->op1 = fleft->op1;  
  return RETRYFOLD;
}


LJFOLD(NEG NEG any)
LJFOLD(BNOT BNOT)
LJFOLD(BSWAP BSWAP)
LJFOLDF(shortcut_leftleft)
{
  PHIBARRIER(fleft);  
  return fleft->op1;  
}





LJFOLD(ADD NEG any)
LJFOLDF(simplify_numadd_negx)
{
  PHIBARRIER(fleft);
  fins->o = IR_SUB;  
  fins->op1 = fins->op2;
  fins->op2 = fleft->op1;
  return RETRYFOLD;
}

LJFOLD(ADD any NEG)
LJFOLDF(simplify_numadd_xneg)
{
  PHIBARRIER(fright);
  fins->o = IR_SUB;  
  fins->op2 = fright->op1;
  return RETRYFOLD;
}

LJFOLD(SUB any KNUM)
LJFOLDF(simplify_numsub_k)
{
  if (ir_knum(fright)->u64 == 0)  
    return LEFTFOLD;
  return NEXTFOLD;
}

LJFOLD(SUB NEG KNUM)
LJFOLDF(simplify_numsub_negk)
{
  PHIBARRIER(fleft);
  fins->op2 = fleft->op1;  
  fins->op1 = (IRRef1)lj_ir_knum(J, -knumright);
  return RETRYFOLD;
}

LJFOLD(SUB any NEG)
LJFOLDF(simplify_numsub_xneg)
{
  PHIBARRIER(fright);
  fins->o = IR_ADD;  
  fins->op2 = fright->op1;
  return RETRYFOLD;
}

LJFOLD(MUL any KNUM)
LJFOLD(DIV any KNUM)
LJFOLDF(simplify_nummuldiv_k)
{
  lua_Number n = knumright;
  if (n == 1.0) {  
    return LEFTFOLD;
  } else if (n == -1.0) {  
    IRRef op1 = fins->op1;
    fins->op2 = (IRRef1)lj_ir_ksimd(J, LJ_KSIMD_NEG);  
    fins->op1 = op1;
    fins->o = IR_NEG;
    return RETRYFOLD;
  } else if (fins->o == IR_MUL && n == 2.0) {  
    fins->o = IR_ADD;
    fins->op2 = fins->op1;
    return RETRYFOLD;
  } else if (fins->o == IR_DIV) {  
    uint64_t u = ir_knum(fright)->u64;
    uint32_t ex = ((uint32_t)(u >> 52) & 0x7ff);
    if ((u & U64x(000fffff,ffffffff)) == 0 && ex - 1 < 0x7fd) {
      u = (u & ((uint64_t)1 << 63)) | ((uint64_t)(0x7fe - ex) << 52);
      fins->o = IR_MUL;  
      fins->op2 = lj_ir_knum_u64(J, u);
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(MUL NEG KNUM)
LJFOLD(DIV NEG KNUM)
LJFOLDF(simplify_nummuldiv_negk)
{
  PHIBARRIER(fleft);
  fins->op1 = fleft->op1;  
  fins->op2 = (IRRef1)lj_ir_knum(J, -knumright);
  return RETRYFOLD;
}

LJFOLD(MUL NEG NEG)
LJFOLD(DIV NEG NEG)
LJFOLDF(simplify_nummuldiv_negneg)
{
  if (fins->o == IR_DIV && (irt_isu32(fins->t) || irt_isu64(fins->t)))
    return NEXTFOLD;
  PHIBARRIER(fleft);
  PHIBARRIER(fright);
  fins->op1 = fleft->op1;  
  fins->op2 = fright->op1;
  return RETRYFOLD;
}

LJFOLD(POW any KNUM)
LJFOLDF(simplify_numpow_k)
{
  if (knumright == 0.0)  
    return lj_ir_knum_one(J);  
  else if (knumright == 1.0)  
    return LEFTFOLD;
  else if (knumright == 2.0)  
    return emitir(IRTN(IR_MUL), fins->op1, fins->op1);
  else
    return NEXTFOLD;
}



LJFOLD(CONV CONV IRCONV_NUM_INT)  
LJFOLDF(shortcut_conv_num_int)
{
  PHIBARRIER(fleft);
  
  if ((fleft->op2 & IRCONV_SRCMASK) == IRT_NUM && irt_isguard(fleft->t))
    return fleft->op1;  
  return NEXTFOLD;
}

LJFOLD(CONV CONV IRCONV_INT_NUM)  
LJFOLDF(simplify_conv_int_num)
{
  
  if ((fleft->op2 & IRCONV_SRCMASK) ==
      ((fins->op2 & IRCONV_DSTMASK) >> IRCONV_DSH))
    return fleft->op1;
  return NEXTFOLD;
}

LJFOLD(CONV CONV IRCONV_I64_NUM)  
LJFOLD(CONV CONV IRCONV_U64_NUM)  
LJFOLDF(simplify_conv_i64_num)
{
  PHIBARRIER(fleft);
  if ((fleft->op2 & IRCONV_SRCMASK) == IRT_INT) {
    
    fins->op1 = fleft->op1;
    fins->op2 = ((IRT_I64<<5)|IRT_INT|IRCONV_SEXT);
    return RETRYFOLD;
  } else if ((fleft->op2 & IRCONV_SRCMASK) == IRT_U32) {
    
    fins->op1 = fleft->op1;
    fins->op2 = (IRT_I64<<5)|IRT_U32;
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(CONV CONV IRCONV_INT_I64)  
LJFOLD(CONV CONV IRCONV_INT_U64)  
LJFOLD(CONV CONV IRCONV_INT_U32)  
LJFOLD(CONV CONV IRCONV_U32_I64)  
LJFOLD(CONV CONV IRCONV_U32_U64)  
LJFOLD(CONV CONV IRCONV_U32_INT)  
LJFOLDF(simplify_conv_int_i64)
{
  int src;
  PHIBARRIER(fleft);
  src = (fleft->op2 & IRCONV_SRCMASK);
  if (src == IRT_INT || src == IRT_U32) {
    if (src == ((fins->op2 & IRCONV_DSTMASK) >> IRCONV_DSH)) {
      return fleft->op1;
    } else {
      fins->op2 = ((fins->op2 & IRCONV_DSTMASK) | src);
      fins->op1 = fleft->op1;
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(CONV CONV IRCONV_FLOAT_NUM)  
LJFOLDF(simplify_conv_flt_num)
{
  PHIBARRIER(fleft);
  if ((fleft->op2 & IRCONV_SRCMASK) == IRT_FLOAT)
    return fleft->op1;
  return NEXTFOLD;
}


LJFOLD(TOBIT CONV KNUM)
LJFOLDF(simplify_tobit_conv)
{
  
  if ((fleft->op2 & IRCONV_SRCMASK) == IRT_INT) {
    lj_assertJ(irt_isnum(fleft->t), "expected TOBIT number arg");
    return fleft->op1;
  } else if ((fleft->op2 & IRCONV_SRCMASK) == IRT_U32) {
    lj_assertJ(irt_isnum(fleft->t), "expected TOBIT number arg");
    fins->o = IR_CONV;
    fins->op1 = fleft->op1;
    fins->op2 = (IRT_INT<<5)|IRT_U32;
    return RETRYFOLD;
  }
  return NEXTFOLD;
}


LJFOLD(FPMATH CONV IRFPM_FLOOR)
LJFOLD(FPMATH CONV IRFPM_CEIL)
LJFOLD(FPMATH CONV IRFPM_TRUNC)
LJFOLDF(simplify_floor_conv)
{
  if ((uint32_t)(fleft->op2 & IRCONV_SRCMASK) - (uint32_t)IRT_I8 <= (uint32_t)(IRT_U64 - IRT_U8))
    return LEFTFOLD;
  return NEXTFOLD;
}


LJFOLD(CONV any IRCONV_I64_INT)
LJFOLD(CONV any IRCONV_U64_INT)
LJFOLDF(simplify_conv_sext)
{
  IRRef ref = fins->op1;
  int64_t ofs = 0;
  if (!(fins->op2 & IRCONV_SEXT))
    return NEXTFOLD;
  PHIBARRIER(fleft);
  if (fleft->o == IR_XLOAD && (irt_isu8(fleft->t) || irt_isu16(fleft->t)))
    goto ok_reduce;
  if (fleft->o == IR_ADD && irref_isk(fleft->op2)) {
    ofs = (int64_t)IR(fleft->op2)->i;
    ref = fleft->op1;
  }
  
  if (ref == J->scev.idx) {
    IRRef lo = J->scev.dir ? J->scev.start : J->scev.stop;
    lj_assertJ(irt_isint(J->scev.t), "only int SCEV supported");
    if (lo && IR(lo)->o == IR_KINT && IR(lo)->i + ofs >= 0) {
    ok_reduce:
#if LJ_TARGET_X64
      
      return LEFTFOLD;
#else
      
      fins->op2 &= ~IRCONV_SEXT;
      return RETRYFOLD;
#endif
    }
  }
  return NEXTFOLD;
}


LJFOLD(CONV ADD IRCONV_INT_I64)
LJFOLD(CONV SUB IRCONV_INT_I64)
LJFOLD(CONV MUL IRCONV_INT_I64)
LJFOLD(CONV ADD IRCONV_INT_U64)
LJFOLD(CONV SUB IRCONV_INT_U64)
LJFOLD(CONV MUL IRCONV_INT_U64)
LJFOLD(CONV ADD IRCONV_U32_I64)
LJFOLD(CONV SUB IRCONV_U32_I64)
LJFOLD(CONV MUL IRCONV_U32_I64)
LJFOLD(CONV ADD IRCONV_U32_U64)
LJFOLD(CONV SUB IRCONV_U32_U64)
LJFOLD(CONV MUL IRCONV_U32_U64)
LJFOLDF(simplify_conv_narrow)
{
#if LJ_64
  UNUSED(J);
  return NEXTFOLD;
#else
  IROp op = (IROp)fleft->o;
  IRType t = irt_type(fins->t);
  IRRef op1 = fleft->op1, op2 = fleft->op2, mode = fins->op2;
  PHIBARRIER(fleft);
  op1 = emitir(IRT(IR_CONV, t), op1, mode);
  op2 = emitir(IRT(IR_CONV, t), op2, mode);
  fins->ot = IRT(op, t);
  fins->op1 = op1;
  fins->op2 = op2;
  return RETRYFOLD;
#endif
}


LJFOLD(CONV any any)
LJFOLDF(cse_conv)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_CSE)) {
    IRRef op1 = fins->op1, op2 = (fins->op2 & IRCONV_MODEMASK);
    uint8_t guard = irt_isguard(fins->t);
    IRRef ref = J->chain[IR_CONV];
    while (ref > op1) {
      IRIns *ir = IR(ref);
      
      if (ir->op1 == op1 && (ir->op2 & IRCONV_MODEMASK) == op2 &&
	  irt_isguard(ir->t) >= guard)
	return ref;
      ref = ir->prev;
    }
  }
  return EMITFOLD;  
}


LJFOLD(TOBIT ADD KNUM)
LJFOLD(TOBIT SUB KNUM)
LJFOLD(CONV ADD IRCONV_INT_NUM)
LJFOLD(CONV SUB IRCONV_INT_NUM)
LJFOLD(CONV ADD IRCONV_I64_NUM)
LJFOLD(CONV SUB IRCONV_I64_NUM)
LJFOLDF(narrow_convert)
{
  PHIBARRIER(fleft);
  
  if (J->chain[IR_LOOP])
    return NEXTFOLD;
  lj_assertJ(fins->o != IR_CONV || (fins->op2&IRCONV_CONVMASK) != IRCONV_TOBIT,
	     "unexpected CONV TOBIT");
  return lj_opt_narrow_convert(J);
}

LJFOLD(XSTORE any CONV)
LJFOLDF(xstore_conv)
{
#if LJ_64
  PHIBARRIER(fright);
  if (!irt_is64(fins->t) &&
      irt_type(fins->t) == (IRType)((fright->op2&IRCONV_DSTMASK)>>IRCONV_DSH) &&
      ((fright->op2&IRCONV_SRCMASK) == IRT_I64 ||
       (fright->op2&IRCONV_SRCMASK) == IRT_U64)) {
    fins->op2 = fright->op1;
    return RETRYFOLD;
  }
#else
  UNUSED(J);
#endif
  return NEXTFOLD;
}



LJFOLD(ADD any KINT)
LJFOLD(ADDOV any KINT)
LJFOLD(SUBOV any KINT)
LJFOLDF(simplify_intadd_k)
{
  if (fright->i == 0)  
    return LEFTFOLD;
  return NEXTFOLD;
}

LJFOLD(MULOV any KINT)
LJFOLDF(simplify_intmul_k)
{
  if (fright->i == 0)  
    return RIGHTFOLD;
  if (fright->i == 1)  
    return LEFTFOLD;
  if (fright->i == 2) {  
    fins->o = IR_ADDOV;
    fins->op2 = fins->op1;
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(SUB any KINT)
LJFOLDF(simplify_intsub_k)
{
  if (fright->i == 0)  
    return LEFTFOLD;
  fins->o = IR_ADD;  
  fins->op2 = (IRRef1)lj_ir_kint(J, (int32_t)(~(uint32_t)fright->i+1u));  
  return RETRYFOLD;
}

LJFOLD(SUB KINT any)
LJFOLD(SUB KINT64 any)
LJFOLDF(simplify_intsub_kleft)
{
  if (fleft->o == IR_KINT ? (fleft->i == 0) : (ir_kint64(fleft)->u64 == 0)) {
    fins->o = IR_NEG;  
    fins->op1 = fins->op2;
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(ADD any KINT64)
LJFOLDF(simplify_intadd_k64)
{
  if (ir_kint64(fright)->u64 == 0)  
    return LEFTFOLD;
  return NEXTFOLD;
}

LJFOLD(SUB any KINT64)
LJFOLDF(simplify_intsub_k64)
{
  uint64_t k = ir_kint64(fright)->u64;
  if (k == 0)  
    return LEFTFOLD;
  fins->o = IR_ADD;  
  fins->op2 = (IRRef1)lj_ir_kint64(J, ~k+1u);
  return RETRYFOLD;
}

static TRef simplify_intmul_k(jit_State *J, int32_t k)
{
  
  if (k == 0) {  
    return RIGHTFOLD;
  } else if (k == 1) {  
    return LEFTFOLD;
  } else if ((k & (k-1)) == 0) {  
    fins->o = IR_BSHL;
    fins->op2 = lj_ir_kint(J, lj_fls((uint32_t)k));
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(MUL any KINT)
LJFOLDF(simplify_intmul_k32)
{
  if (fright->i >= 0)
    return simplify_intmul_k(J, fright->i);
  return NEXTFOLD;
}

LJFOLD(MUL any KINT64)
LJFOLDF(simplify_intmul_k64)
{
#if LJ_HASFFI
  if (ir_kint64(fright)->u64 < 0x80000000u)
    return simplify_intmul_k(J, (int32_t)ir_kint64(fright)->u64);
  return NEXTFOLD;
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(MOD any KINT)
LJFOLDF(simplify_intmod_k)
{
  int32_t k = fright->i;
  lj_assertJ(k != 0, "integer mod 0");
  if (k > 0 && (k & (k-1)) == 0) {  
    fins->o = IR_BAND;
    fins->op2 = lj_ir_kint(J, k-1);
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(MOD KINT any)
LJFOLDF(simplify_intmod_kleft)
{
  if (fleft->i == 0)
    return INTFOLD(0);
  return NEXTFOLD;
}

LJFOLD(SUB any any)
LJFOLD(SUBOV any any)
LJFOLDF(simplify_intsub)
{
  if (fins->op1 == fins->op2 && !irt_isnum(fins->t))  
    return irt_is64(fins->t) ? INT64FOLD(0) : INTFOLD(0);
  return NEXTFOLD;
}

LJFOLD(SUB ADD any)
LJFOLDF(simplify_intsubadd_leftcancel)
{
  if (!irt_isnum(fins->t)) {
    PHIBARRIER(fleft);
    if (fins->op2 == fleft->op1)  
      return fleft->op2;
    if (fins->op2 == fleft->op2)  
      return fleft->op1;
  }
  return NEXTFOLD;
}

LJFOLD(SUB SUB any)
LJFOLDF(simplify_intsubsub_leftcancel)
{
  if (!irt_isnum(fins->t)) {
    PHIBARRIER(fleft);
    if (fins->op2 == fleft->op1) {  
      fins->op1 = (IRRef1)lj_ir_kint(J, 0);
      fins->op2 = fleft->op2;
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(SUB any SUB)
LJFOLDF(simplify_intsubsub_rightcancel)
{
  if (!irt_isnum(fins->t)) {
    PHIBARRIER(fright);
    if (fins->op1 == fright->op1)  
      return fright->op2;
  }
  return NEXTFOLD;
}

LJFOLD(SUB any ADD)
LJFOLDF(simplify_intsubadd_rightcancel)
{
  if (!irt_isnum(fins->t)) {
    PHIBARRIER(fright);
    if (fins->op1 == fright->op1) {  
      fins->op2 = fright->op2;
      fins->op1 = (IRRef1)lj_ir_kint(J, 0);
      return RETRYFOLD;
    }
    if (fins->op1 == fright->op2) {  
      fins->op2 = fright->op1;
      fins->op1 = (IRRef1)lj_ir_kint(J, 0);
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(SUB ADD ADD)
LJFOLDF(simplify_intsubaddadd_cancel)
{
  if (!irt_isnum(fins->t)) {
    PHIBARRIER(fleft);
    PHIBARRIER(fright);
    if (fleft->op1 == fright->op1) {  
      fins->op1 = fleft->op2;
      fins->op2 = fright->op2;
      return RETRYFOLD;
    }
    if (fleft->op1 == fright->op2) {  
      fins->op1 = fleft->op2;
      fins->op2 = fright->op1;
      return RETRYFOLD;
    }
    if (fleft->op2 == fright->op1) {  
      fins->op1 = fleft->op1;
      fins->op2 = fright->op2;
      return RETRYFOLD;
    }
    if (fleft->op2 == fright->op2) {  
      fins->op1 = fleft->op1;
      fins->op2 = fright->op1;
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(BAND any KINT)
LJFOLD(BAND any KINT64)
LJFOLDF(simplify_band_k)
{
  int64_t k = fright->o == IR_KINT ? (int64_t)fright->i :
				     (int64_t)ir_k64(fright)->u64;
  if (k == 0)  
    return RIGHTFOLD;
  if (k == -1)  
    return LEFTFOLD;
  return NEXTFOLD;
}

LJFOLD(BOR any KINT)
LJFOLD(BOR any KINT64)
LJFOLDF(simplify_bor_k)
{
  int64_t k = fright->o == IR_KINT ? (int64_t)fright->i :
				     (int64_t)ir_k64(fright)->u64;
  if (k == 0)  
    return LEFTFOLD;
  if (k == -1)  
    return RIGHTFOLD;
  return NEXTFOLD;
}

LJFOLD(BXOR any KINT)
LJFOLD(BXOR any KINT64)
LJFOLDF(simplify_bxor_k)
{
  int64_t k = fright->o == IR_KINT ? (int64_t)fright->i :
				     (int64_t)ir_k64(fright)->u64;
  if (k == 0)  
    return LEFTFOLD;
  if (k == -1) {  
    fins->o = IR_BNOT;
    fins->op2 = 0;
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(BSHL any KINT)
LJFOLD(BSHR any KINT)
LJFOLD(BSAR any KINT)
LJFOLD(BROL any KINT)
LJFOLD(BROR any KINT)
LJFOLDF(simplify_shift_ik)
{
  int32_t mask = irt_is64(fins->t) ? 63 : 31;
  int32_t k = (fright->i & mask);
  if (k == 0)  
    return LEFTFOLD;
  if (k == 1 && fins->o == IR_BSHL) {  
    fins->o = IR_ADD;
    fins->op2 = fins->op1;
    return RETRYFOLD;
  }
  if (k != fright->i) {  
    fins->op2 = (IRRef1)lj_ir_kint(J, k);
    return RETRYFOLD;
  }
#ifndef LJ_TARGET_UNIFYROT
  if (fins->o == IR_BROR) {  
    fins->o = IR_BROL;
    fins->op2 = (IRRef1)lj_ir_kint(J, (-k)&mask);
    return RETRYFOLD;
  }
#endif
  return NEXTFOLD;
}

LJFOLD(BSHL any BAND)
LJFOLD(BSHR any BAND)
LJFOLD(BSAR any BAND)
LJFOLD(BROL any BAND)
LJFOLD(BROR any BAND)
LJFOLDF(simplify_shift_andk)
{
  IRIns *irk = IR(fright->op2);
  PHIBARRIER(fright);
  if ((fins->o < IR_BROL ? LJ_TARGET_MASKSHIFT : LJ_TARGET_MASKROT) &&
      irk->o == IR_KINT) {  
    int32_t mask = irt_is64(fins->t) ? 63 : 31;
    int32_t k = irk->i & mask;
    if (k == mask) {
      fins->op2 = fright->op1;
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(BSHL KINT any)
LJFOLD(BSHR KINT any)
LJFOLD(BSHL KINT64 any)
LJFOLD(BSHR KINT64 any)
LJFOLDF(simplify_shift1_ki)
{
  int64_t k = fleft->o == IR_KINT ? (int64_t)fleft->i :
				    (int64_t)ir_k64(fleft)->u64;
  if (k == 0)  
    return LEFTFOLD;
  return NEXTFOLD;
}

LJFOLD(BSAR KINT any)
LJFOLD(BROL KINT any)
LJFOLD(BROR KINT any)
LJFOLD(BSAR KINT64 any)
LJFOLD(BROL KINT64 any)
LJFOLD(BROR KINT64 any)
LJFOLDF(simplify_shift2_ki)
{
  int64_t k = fleft->o == IR_KINT ? (int64_t)fleft->i :
				    (int64_t)ir_k64(fleft)->u64;
  if (k == 0 || k == -1)  
    return LEFTFOLD;
  return NEXTFOLD;
}

LJFOLD(BSHL BAND KINT)
LJFOLD(BSHR BAND KINT)
LJFOLD(BROL BAND KINT)
LJFOLD(BROR BAND KINT)
LJFOLDF(simplify_shiftk_andk)
{
  IRIns *irk = IR(fleft->op2);
  PHIBARRIER(fleft);
  if (irk->o == IR_KINT) {  
    int32_t k = kfold_intop(irk->i, fright->i, (IROp)fins->o);
    fins->op1 = fleft->op1;
    fins->op1 = (IRRef1)lj_opt_fold(J);
    fins->op2 = (IRRef1)lj_ir_kint(J, k);
    fins->ot = IRTI(IR_BAND);
    return RETRYFOLD;
  } else if (irk->o == IR_KINT64) {
    uint64_t k = kfold_int64arith(J, ir_k64(irk)->u64, fright->i,
				  (IROp)fins->o);
    IROpT ot = fleft->ot;
    fins->op1 = fleft->op1;
    fins->op1 = (IRRef1)lj_opt_fold(J);
    fins->op2 = (IRRef1)lj_ir_kint64(J, k);
    fins->ot = ot;
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(BAND BSHL KINT)
LJFOLD(BAND BSHR KINT)
LJFOLDF(simplify_andk_shiftk)
{
  IRIns *irk = IR(fleft->op2);
  if (irk->o == IR_KINT &&
      kfold_intop(-1, irk->i, (IROp)fleft->o) == fright->i)
    return LEFTFOLD;  
  return NEXTFOLD;
}

LJFOLD(BAND BOR KINT)
LJFOLD(BOR BAND KINT)
LJFOLDF(simplify_andor_k)
{
  IRIns *irk = IR(fleft->op2);
  PHIBARRIER(fleft);
  if (irk->o == IR_KINT) {
    int32_t k = kfold_intop(irk->i, fright->i, (IROp)fins->o);
    
    
    if (k == (fins->o == IR_BAND ? 0 : -1)) {
      fins->op1 = fleft->op1;
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
}

LJFOLD(BAND BOR KINT64)
LJFOLD(BOR BAND KINT64)
LJFOLDF(simplify_andor_k64)
{
#if LJ_HASFFI
  IRIns *irk = IR(fleft->op2);
  PHIBARRIER(fleft);
  if (irk->o == IR_KINT64) {
    uint64_t k = kfold_int64arith(J, ir_k64(irk)->u64, ir_k64(fright)->u64,
				  (IROp)fins->o);
    
    
    if (k == (fins->o == IR_BAND ? (uint64_t)0 : ~(uint64_t)0)) {
      fins->op1 = fleft->op1;
      return RETRYFOLD;
    }
  }
  return NEXTFOLD;
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}



LJFOLD(ADD ADD KINT)
LJFOLD(MUL MUL KINT)
LJFOLD(BAND BAND KINT)
LJFOLD(BOR BOR KINT)
LJFOLD(BXOR BXOR KINT)
LJFOLDF(reassoc_intarith_k)
{
  IRIns *irk = IR(fleft->op2);
  if (irk->o == IR_KINT) {
    int32_t k = kfold_intop(irk->i, fright->i, (IROp)fins->o);
    if (k == irk->i)  
      return LEFTFOLD;
    PHIBARRIER(fleft);
    fins->op1 = fleft->op1;
    fins->op2 = (IRRef1)lj_ir_kint(J, k);
    return RETRYFOLD;  
  }
  return NEXTFOLD;
}

LJFOLD(ADD ADD KINT64)
LJFOLD(MUL MUL KINT64)
LJFOLD(BAND BAND KINT64)
LJFOLD(BOR BOR KINT64)
LJFOLD(BXOR BXOR KINT64)
LJFOLDF(reassoc_intarith_k64)
{
#if LJ_HASFFI
  IRIns *irk = IR(fleft->op2);
  if (irk->o == IR_KINT64) {
    uint64_t k = kfold_int64arith(J, ir_k64(irk)->u64, ir_k64(fright)->u64,
				  (IROp)fins->o);
    PHIBARRIER(fleft);
    fins->op1 = fleft->op1;
    fins->op2 = (IRRef1)lj_ir_kint64(J, k);
    return RETRYFOLD;  
  }
  return NEXTFOLD;
#else
  UNUSED(J); lj_assertJ(0, "FFI IR op without FFI"); return FAILFOLD;
#endif
}

LJFOLD(BAND BAND any)
LJFOLD(BOR BOR any)
LJFOLDF(reassoc_dup)
{
  if (fins->op2 == fleft->op1 || fins->op2 == fleft->op2)
    return LEFTFOLD;  
  return NEXTFOLD;
}

LJFOLD(MIN MIN any)
LJFOLD(MAX MAX any)
LJFOLDF(reassoc_dup_minmax)
{
  if (fins->op2 == fleft->op2)
    return LEFTFOLD;  
  return NEXTFOLD;
}

LJFOLD(BXOR BXOR any)
LJFOLDF(reassoc_bxor)
{
  PHIBARRIER(fleft);
  if (fins->op2 == fleft->op1)  
    return fleft->op2;
  if (fins->op2 == fleft->op2)  
    return fleft->op1;
  return NEXTFOLD;
}

LJFOLD(BSHL BSHL KINT)
LJFOLD(BSHR BSHR KINT)
LJFOLD(BSAR BSAR KINT)
LJFOLD(BROL BROL KINT)
LJFOLD(BROR BROR KINT)
LJFOLDF(reassoc_shift)
{
  IRIns *irk = IR(fleft->op2);
  PHIBARRIER(fleft);  
  if (irk->o == IR_KINT) {  
    int32_t mask = irt_is64(fins->t) ? 63 : 31;
    int32_t k = (irk->i & mask) + (fright->i & mask);
    if (k > mask) {  
      if (fins->o == IR_BSHL || fins->o == IR_BSHR)
	return mask == 31 ? INTFOLD(0) : INT64FOLD(0);
      else if (fins->o == IR_BSAR)
	k = mask;
      else
	k &= mask;
    }
    fins->op1 = fleft->op1;
    fins->op2 = (IRRef1)lj_ir_kint(J, k);
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(MIN MIN KINT)
LJFOLD(MAX MAX KINT)
LJFOLDF(reassoc_minmax_k)
{
  IRIns *irk = IR(fleft->op2);
  if (irk->o == IR_KINT) {
    int32_t a = irk->i;
    int32_t y = kfold_intop(a, fright->i, fins->o);
    if (a == y)  
      return LEFTFOLD;
    PHIBARRIER(fleft);
    fins->op1 = fleft->op1;
    fins->op2 = (IRRef1)lj_ir_kint(J, y);
    return RETRYFOLD;  
  }
  return NEXTFOLD;
}




LJFOLD(ABC any ADD)
LJFOLDF(abc_fwd)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_ABC)) {
    if (irref_isk(fright->op2)) {
      IRIns *add2 = IR(fright->op1);
      if (add2->o == IR_ADD && irref_isk(add2->op2) &&
	  IR(fright->op2)->i == -IR(add2->op2)->i) {
	IRRef ref = J->chain[IR_ABC];
	IRRef lim = add2->op1;
	if (fins->op1 > lim) lim = fins->op1;
	while (ref > lim) {
	  IRIns *ir = IR(ref);
	  if (ir->op1 == fins->op1 && ir->op2 == add2->op1)
	    return DROPFOLD;
	  ref = ir->prev;
	}
      }
    }
  }
  return NEXTFOLD;
}


LJFOLD(ABC any KINT)
LJFOLDF(abc_k)
{
  PHIBARRIER(fleft);
  if (LJ_LIKELY(J->flags & JIT_F_OPT_ABC)) {
    IRRef ref = J->chain[IR_ABC];
    IRRef asize = fins->op1;
    while (ref > asize) {
      IRIns *ir = IR(ref);
      if (ir->op1 == asize && irref_isk(ir->op2)) {
	uint32_t k = (uint32_t)IR(ir->op2)->i;
	if ((uint32_t)fright->i > k)
	  ir->op2 = fins->op2;
	return DROPFOLD;
      }
      ref = ir->prev;
    }
    return EMITFOLD;  
  }
  return NEXTFOLD;
}


LJFOLD(ABC any any)
LJFOLDF(abc_invar)
{
  
  if (!irt_isint(fins->t) && fins->op1 < J->chain[IR_LOOP] &&
      (irt_isu32(fins->t) ||
       (!irref_isk(fins->op1) && !irt_isphi(IR(fins->op1)->t))))
    return DROPFOLD;
  return NEXTFOLD;
}





LJFOLD(ADD any any)
LJFOLD(MUL any any)
LJFOLD(ADDOV any any)
LJFOLD(MULOV any any)
LJFOLDF(comm_swap)
{
  if (fins->op1 < fins->op2) {  
    IRRef1 tmp = fins->op1;
    fins->op1 = fins->op2;
    fins->op2 = tmp;
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(EQ any any)
LJFOLD(NE any any)
LJFOLDF(comm_equal)
{
  
  if (fins->op1 == fins->op2 &&
      (!irt_isnum(fins->t) ||
       (fleft->o == IR_CONV &&  
	(uint32_t)(fleft->op2 & IRCONV_SRCMASK) - (uint32_t)IRT_I8 <= (uint32_t)(IRT_U64 - IRT_U8))))
    return CONDFOLD(fins->o == IR_EQ);
  return fold_comm_swap(J);
}

LJFOLD(LT any any)
LJFOLD(GE any any)
LJFOLD(LE any any)
LJFOLD(GT any any)
LJFOLD(ULT any any)
LJFOLD(UGE any any)
LJFOLD(ULE any any)
LJFOLD(UGT any any)
LJFOLDF(comm_comp)
{
  
  if (fins->op1 == fins->op2 && !irt_isnum(fins->t))
    return CONDFOLD((fins->o ^ (fins->o >> 1)) & 1);
  if (fins->op1 < fins->op2) {  
    IRRef1 tmp = fins->op1;
    fins->op1 = fins->op2;
    fins->op2 = tmp;
    fins->o ^= 3; 
    return RETRYFOLD;
  }
  return NEXTFOLD;
}

LJFOLD(BAND any any)
LJFOLD(BOR any any)
LJFOLDF(comm_dup)
{
  if (fins->op1 == fins->op2)  
    return LEFTFOLD;
  return fold_comm_swap(J);
}

LJFOLD(MIN any any)
LJFOLD(MAX any any)
LJFOLDF(comm_dup_minmax)
{
  if (fins->op1 == fins->op2)  
    return LEFTFOLD;
  return NEXTFOLD;
}

LJFOLD(BXOR any any)
LJFOLDF(comm_bxor)
{
  if (fins->op1 == fins->op2)  
    return irt_is64(fins->t) ? INT64FOLD(0) : INTFOLD(0);
  return fold_comm_swap(J);
}



static TRef kfold_xload(jit_State *J, IRIns *ir, const void *p)
{
  int32_t k;
  switch (irt_type(ir->t)) {
  case IRT_NUM: return lj_ir_knum_u64(J, *(uint64_t *)p);
  case IRT_I8: k = (int32_t)*(int8_t *)p; break;
  case IRT_U8: k = (int32_t)*(uint8_t *)p; break;
  case IRT_I16: k = (int32_t)(int16_t)lj_getu16(p); break;
  case IRT_U16: k = (int32_t)(uint16_t)lj_getu16(p); break;
  case IRT_INT: case IRT_U32: k = (int32_t)lj_getu32(p); break;
  case IRT_I64: case IRT_U64: return lj_ir_kint64(J, *(uint64_t *)p);
  default: return 0;
  }
  return lj_ir_kint(J, k);
}


LJFOLD(EQ SNEW KGC)
LJFOLD(NE SNEW KGC)
LJFOLDF(merge_eqne_snew_kgc)
{
  GCstr *kstr = ir_kstr(fright);
  int32_t len = (int32_t)kstr->len;
  lj_assertJ(irt_isstr(fins->t), "bad equality IR type");

#if LJ_TARGET_UNALIGNED
#define FOLD_SNEW_MAX_LEN	4  
#define FOLD_SNEW_TYPE8		IRT_I8	
#else
#define FOLD_SNEW_MAX_LEN	1  
#define FOLD_SNEW_TYPE8		IRT_U8  
#endif

  PHIBARRIER(fleft);
  if (len <= FOLD_SNEW_MAX_LEN) {
    IROp op = (IROp)fins->o;
    IRRef strref = fleft->op1;
    if (IR(strref)->o != IR_STRREF)
      return NEXTFOLD;
    if (op == IR_EQ) {
      emitir(IRTGI(IR_EQ), fleft->op2, lj_ir_kint(J, len));
      
    } else {
      
      if (!irref_isk(fleft->op2))  
	return NEXTFOLD;
      if (IR(fleft->op2)->i != len)
	return DROPFOLD;
    }
    if (len > 0) {
      
      uint16_t ot = (uint16_t)(len == 1 ? IRT(IR_XLOAD, FOLD_SNEW_TYPE8) :
			       len == 2 ? IRT(IR_XLOAD, IRT_U16) :
			       IRTI(IR_XLOAD));
      TRef tmp = emitir(ot, strref,
			IRXLOAD_READONLY | (len > 1 ? IRXLOAD_UNALIGNED : 0));
      TRef val = kfold_xload(J, IR(tref_ref(tmp)), strdata(kstr));
      if (len == 3)
	tmp = emitir(IRTI(IR_BAND), tmp,
		     lj_ir_kint(J, LJ_ENDIAN_SELECT(0x00ffffff, 0xffffff00)));
      fins->op1 = (IRRef1)tmp;
      fins->op2 = (IRRef1)val;
      fins->ot = (IROpT)IRTGI(op);
      return RETRYFOLD;
    } else {
      return DROPFOLD;
    }
  }
  return NEXTFOLD;
}





LJFOLD(ALOAD any)
LJFOLDX(lj_opt_fwd_aload)


LJFOLD(HLOAD KKPTR)
LJFOLDF(kfold_hload_kkptr)
{
  UNUSED(J);
  lj_assertJ(ir_kptr(fleft) == niltvg(J2G(J)), "expected niltv");
  return TREF_NIL;
}

LJFOLD(HLOAD any)
LJFOLDX(lj_opt_fwd_hload)

LJFOLD(ULOAD any)
LJFOLDX(lj_opt_fwd_uload)

LJFOLD(ALEN any any)
LJFOLDX(lj_opt_fwd_alen)


static TRef merge_uref(jit_State *J, IRRef ref, IRIns* ir)
{
  if (ir->o == IR_UREFO && irt_isguard(ir->t)) {
    
    if (gcstep_barrier(J, ref))
      return EMITFOLD;  
    
    if ((irt_t(fins->t) & (IRT_GUARD|IRT_TYPE)) == (IRT_GUARD|IRT_PGC) &&
	irt_type(ir->t) == IRT_IGC)
      ir->t.irt += IRT_PGC-IRT_IGC;  
  }
  return ref;  
}


LJFOLD(UREFO KGC any)
LJFOLD(UREFC KGC any)
LJFOLDF(cse_uref)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_CSE)) {
    IRRef ref = J->chain[fins->o];
    GCfunc *fn = ir_kfunc(fleft);
    GCupval *uv = gco2uv(gcref(fn->l.uvptr[(fins->op2 >> 8)]));
    while (ref > 0) {
      IRIns *ir = IR(ref);
      if (irref_isk(ir->op1)) {
	GCfunc *fn2 = ir_kfunc(IR(ir->op1));
	if (gco2uv(gcref(fn2->l.uvptr[(ir->op2 >> 8)])) == uv) {
	  return merge_uref(J, ref, ir);
	}
      }
      ref = ir->prev;
    }
  }
  return EMITFOLD;
}


LJFOLD(UREFO any any)
LJFOLDF(cse_urefo)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_CSE)) {
    IRRef ref = J->chain[IR_UREFO];
    IRRef lim = fins->op1;
    IRRef2 op12 = (IRRef2)fins->op1 + ((IRRef2)fins->op2 << 16);
    while (ref > lim) {
      IRIns *ir = IR(ref);
      if (ir->op12 == op12)
	return merge_uref(J, ref, ir);
      ref = ir->prev;
    }
  }
  return EMITFOLD;
}

LJFOLD(HREFK any any)
LJFOLDX(lj_opt_fwd_hrefk)

LJFOLD(HREF TNEW any)
LJFOLDF(fwd_href_tnew)
{
  if (lj_opt_fwd_href_nokey(J))
    return lj_ir_kkptr(J, niltvg(J2G(J)));
  return NEXTFOLD;
}

LJFOLD(HREF TDUP KPRI)
LJFOLD(HREF TDUP KGC)
LJFOLD(HREF TDUP KNUM)
LJFOLDF(fwd_href_tdup)
{
  TValue keyv;
  cTValue *val;
  lj_ir_kvalue(J->L, &keyv, fright);
  val = lj_tab_get(J->L, ir_ktab(IR(fleft->op1)), &keyv);
  
  if ((tvisnil(val) || tvistab(val)) && lj_opt_fwd_href_nokey(J))
    return lj_ir_kkptr(J, niltvg(J2G(J)));
  return NEXTFOLD;
}


LJFOLD(FLOAD TNEW IRFL_TAB_ASIZE)
LJFOLDF(fload_tab_tnew_asize)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD) && lj_opt_fwd_tptr(J, fins->op1))
    return INTFOLD(fleft->op1);
  return NEXTFOLD;
}

LJFOLD(FLOAD TNEW IRFL_TAB_HMASK)
LJFOLDF(fload_tab_tnew_hmask)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD) && lj_opt_fwd_tptr(J, fins->op1))
    return INTFOLD((1 << fleft->op2)-1);
  return NEXTFOLD;
}

LJFOLD(FLOAD TDUP IRFL_TAB_ASIZE)
LJFOLDF(fload_tab_tdup_asize)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD) && lj_opt_fwd_tptr(J, fins->op1))
    return INTFOLD((int32_t)ir_ktab(IR(fleft->op1))->asize);
  return NEXTFOLD;
}

LJFOLD(FLOAD TDUP IRFL_TAB_HMASK)
LJFOLDF(fload_tab_tdup_hmask)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD) && lj_opt_fwd_tptr(J, fins->op1))
    return INTFOLD((int32_t)ir_ktab(IR(fleft->op1))->hmask);
  return NEXTFOLD;
}

LJFOLD(HREF any any)
LJFOLD(FLOAD any IRFL_TAB_ARRAY)
LJFOLD(FLOAD any IRFL_TAB_NODE)
LJFOLD(FLOAD any IRFL_TAB_ASIZE)
LJFOLD(FLOAD any IRFL_TAB_HMASK)
LJFOLDF(fload_tab_ah)
{
  TRef tr = lj_opt_cse(J);
  return lj_opt_fwd_tptr(J, tref_ref(tr)) ? tr : EMITFOLD;
}


LJFOLD(FLOAD KGC IRFL_STR_LEN)
LJFOLDF(fload_str_len_kgc)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD))
    return INTFOLD((int32_t)ir_kstr(fleft)->len);
  return NEXTFOLD;
}

LJFOLD(FLOAD SNEW IRFL_STR_LEN)
LJFOLDF(fload_str_len_snew)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD)) {
    PHIBARRIER(fleft);
    return fleft->op2;
  }
  return NEXTFOLD;
}

LJFOLD(FLOAD TOSTR IRFL_STR_LEN)
LJFOLDF(fload_str_len_tostr)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD) && fleft->op2 == IRTOSTR_CHAR)
    return INTFOLD(1);
  return NEXTFOLD;
}

LJFOLD(FLOAD any IRFL_SBUF_W)
LJFOLD(FLOAD any IRFL_SBUF_E)
LJFOLD(FLOAD any IRFL_SBUF_B)
LJFOLD(FLOAD any IRFL_SBUF_L)
LJFOLD(FLOAD any IRFL_SBUF_REF)
LJFOLD(FLOAD any IRFL_SBUF_R)
LJFOLDF(fload_sbuf)
{
  TRef tr = lj_opt_fwd_fload(J);
  return lj_opt_fwd_sbuf(J, tref_ref(tr)) ? tr : EMITFOLD;
}


LJFOLD(FLOAD KGC IRFL_FUNC_FFID)
LJFOLDF(fload_func_ffid_kgc)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD))
    return INTFOLD((int32_t)ir_kfunc(fleft)->c.ffid);
  return NEXTFOLD;
}


LJFOLD(FLOAD KGC IRFL_CDATA_CTYPEID)
LJFOLDF(fload_cdata_typeid_kgc)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD))
    return INTFOLD((int32_t)ir_kcdata(fleft)->ctypeid);
  return NEXTFOLD;
}


LJFOLD(FLOAD KGC IRFL_CDATA_PTR)
LJFOLD(FLOAD KGC IRFL_CDATA_INT)
LJFOLD(FLOAD KGC IRFL_CDATA_INT64)
LJFOLDF(fload_cdata_int64_kgc)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD)) {
    void *p = cdataptr(ir_kcdata(fleft));
    if (irt_is64(fins->t))
      return INT64FOLD(*(uint64_t *)p);
    else
      return INTFOLD(*(int32_t *)p);
  }
  return NEXTFOLD;
}

LJFOLD(FLOAD CNEW IRFL_CDATA_CTYPEID)
LJFOLD(FLOAD CNEWI IRFL_CDATA_CTYPEID)
LJFOLDF(fload_cdata_typeid_cnew)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD))
    return fleft->op1;  
  return NEXTFOLD;
}


LJFOLD(FLOAD CNEWI IRFL_CDATA_PTR)
LJFOLD(FLOAD CNEWI IRFL_CDATA_INT)
LJFOLD(FLOAD CNEWI IRFL_CDATA_INT64)
LJFOLDF(fload_cdata_ptr_int64_cnew)
{
  if (LJ_LIKELY(J->flags & JIT_F_OPT_FOLD))
    return fleft->op2;  
  return NEXTFOLD;
}

LJFOLD(FLOAD any IRFL_STR_LEN)
LJFOLD(FLOAD any IRFL_FUNC_ENV)
LJFOLD(FLOAD any IRFL_THREAD_ENV)
LJFOLD(FLOAD any IRFL_CDATA_CTYPEID)
LJFOLD(FLOAD any IRFL_CDATA_PTR)
LJFOLD(FLOAD any IRFL_CDATA_INT)
LJFOLD(FLOAD any IRFL_CDATA_INT64)
LJFOLD(VLOAD any any)  
LJFOLDX(lj_opt_cse)


LJFOLD(FLOAD any any)
LJFOLDX(lj_opt_fwd_fload)


LJFOLD(SLOAD any any)
LJFOLDF(fwd_sload)
{
  if ((fins->op2 & IRSLOAD_FRAME)) {
    TRef tr = lj_opt_cse(J);
    return tref_ref(tr) < J->chain[IR_RETF] ? EMITFOLD : tr;
  } else {
    lj_assertJ(J->slot[fins->op1] != 0, "uninitialized slot accessed");
    return J->slot[fins->op1];
  }
}


LJFOLD(XLOAD KKPTR any)
LJFOLDF(xload_kptr)
{
  TRef tr = kfold_xload(J, fins, ir_kptr(fleft));
  return tr ? tr : NEXTFOLD;
}

LJFOLD(XLOAD any any)
LJFOLDX(lj_opt_fwd_xload)




LJFOLD(SUB any BASE)
LJFOLD(SUB BASE any)
LJFOLD(EQ any BASE)
LJFOLDF(fold_base)
{
  return lj_opt_cselim(J, J->chain[IR_RETF]);
}




LJFOLD(TBAR any)
LJFOLD(OBAR any any)
LJFOLDF(barrier_tab)
{
  TRef tr = lj_opt_cse(J);
  if (gcstep_barrier(J, tref_ref(tr)))  
    return EMITFOLD;  
  return tr;
}

LJFOLD(TBAR TNEW)
LJFOLD(TBAR TDUP)
LJFOLDF(barrier_tnew_tdup)
{
  
  if (fins->op1 < J->chain[IR_LOOP])  
    return NEXTFOLD;
  return DROPFOLD;
}



LJFOLD(PROF any any)
LJFOLDF(prof)
{
  IRRef ref = J->chain[IR_PROF];
  if (ref+1 == J->cur.nins)  
    return ref;
  return EMITFOLD;
}





LJFOLD(ASTORE any any)
LJFOLD(HSTORE any any)
LJFOLDX(lj_opt_dse_ahstore)

LJFOLD(USTORE any any)
LJFOLDX(lj_opt_dse_ustore)

LJFOLD(FSTORE any any)
LJFOLDX(lj_opt_dse_fstore)

LJFOLD(XSTORE any any)
LJFOLDX(lj_opt_dse_xstore)

LJFOLD(NEWREF any any)  
LJFOLD(TMPREF any any)
LJFOLD(CALLA any any)
LJFOLD(CALLL any any)  
LJFOLD(CALLS any any)
LJFOLD(CALLXS any any)
LJFOLD(XBAR)
LJFOLD(RETF any any)  
LJFOLD(TNEW any any)
LJFOLD(TDUP any)
LJFOLD(CNEW any any)
LJFOLD(XSNEW any any)
LJFOLDX(lj_ir_emit)



LJFOLD(CARG any any)
LJFOLDF(cse_carg)
{
  TRef tr = lj_opt_cse(J);
  if (tref_ref(tr) < J->chain[IR_LOOP])  
    return EMITFOLD;  
  return tr;
}





#include "lj_folddef.h"




TRef LJ_FASTCALL lj_opt_fold(jit_State *J)
{
  uint32_t key, any;
  IRRef ref;

  if (LJ_UNLIKELY((J->flags & JIT_F_OPT_MASK) != JIT_F_OPT_DEFAULT)) {
    lj_assertJ(((JIT_F_OPT_FOLD|JIT_F_OPT_FWD|JIT_F_OPT_CSE|JIT_F_OPT_DSE) |
		JIT_F_OPT_DEFAULT) == JIT_F_OPT_DEFAULT,
	       "bad JIT_F_OPT_DEFAULT");
    
    if (!(J->flags & JIT_F_OPT_FOLD) && irm_kind(lj_ir_mode[fins->o]) == IRM_N)
      return lj_opt_cse(J);

    
    if ((J->flags & (JIT_F_OPT_FOLD|JIT_F_OPT_FWD|JIT_F_OPT_CSE)) !=
		    (JIT_F_OPT_FOLD|JIT_F_OPT_FWD|JIT_F_OPT_CSE) &&
	irm_kind(lj_ir_mode[fins->o]) == IRM_L && fins->o != IR_SLOAD)
      return lj_ir_emit(J);

    
    if ((J->flags & (JIT_F_OPT_FOLD|JIT_F_OPT_DSE)) !=
		    (JIT_F_OPT_FOLD|JIT_F_OPT_DSE) &&
	irm_kind(lj_ir_mode[fins->o]) == IRM_S)
      return lj_ir_emit(J);
  }

  
retry:
  
  key = ((uint32_t)fins->o << 17);
  if (fins->op1 >= J->cur.nk) {
    key += (uint32_t)IR(fins->op1)->o << 10;
    *fleft = *IR(fins->op1);
    if (fins->op1 < REF_TRUE)
      fleft[1] = IR(fins->op1)[1];
  }
  if (fins->op2 >= J->cur.nk) {
    key += (uint32_t)IR(fins->op2)->o;
    *fright = *IR(fins->op2);
    if (fins->op2 < REF_TRUE)
      fright[1] = IR(fins->op2)[1];
  } else {
    key += (fins->op2 & 0x3ffu);  
  }

  
  any = 0;
  for (;;) {
    uint32_t k = key | (any & 0x1ffff);
    uint32_t h = fold_hashkey(k);
    uint32_t fh = fold_hash[h];  
    if ((fh & 0xffffff) == k || (fh = fold_hash[h+1], (fh & 0xffffff) == k)) {
      ref = (IRRef)tref_ref(fold_func[fh >> 24](J));
      if (ref != NEXTFOLD)
	break;
    }
    if (any == 0xfffff)  
      return lj_opt_cse(J);
    any = (any | (any >> 10)) ^ 0xffc00;
  }

  
  if (LJ_LIKELY(ref >= MAX_FOLD))
    return TREF(ref, irt_t(IR(ref)->t));
  if (ref == RETRYFOLD)
    goto retry;
  if (ref == KINTFOLD)
    return lj_ir_kint(J, fins->i);
  if (ref == FAILFOLD)
    lj_trace_err(J, LJ_TRERR_GFAIL);
  lj_assertJ(ref == DROPFOLD, "bad fold result");
  return REF_DROP;
}




TRef LJ_FASTCALL lj_opt_cse(jit_State *J)
{
  
  IRRef2 op12 = (IRRef2)fins->op1 + ((IRRef2)fins->op2 << 16);
  IROp op = fins->o;
  if (LJ_LIKELY(J->flags & JIT_F_OPT_CSE)) {
    
    IRRef ref = J->chain[op];
    IRRef lim = fins->op1;
    if (fins->op2 > lim) lim = fins->op2;  
    while (ref > lim) {
      if (IR(ref)->op12 == op12)
	return TREF(ref, irt_t(IR(ref)->t));  
      ref = IR(ref)->prev;
    }
  }
  
  {
    IRRef ref = lj_ir_nextins(J);
    IRIns *ir = IR(ref);
    ir->prev = J->chain[op];
    ir->op12 = op12;
    J->chain[op] = (IRRef1)ref;
    ir->o = fins->o;
    J->guardemit.irt |= fins->t.irt;
    return TREF(ref, irt_t((ir->t = fins->t)));
  }
}


TRef LJ_FASTCALL lj_opt_cselim(jit_State *J, IRRef lim)
{
  IRRef ref = J->chain[fins->o];
  IRRef2 op12 = (IRRef2)fins->op1 + ((IRRef2)fins->op2 << 16);
  while (ref > lim) {
    if (IR(ref)->op12 == op12)
      return ref;
    ref = IR(ref)->prev;
  }
  return lj_ir_emit(J);
}



#undef IR
#undef fins
#undef fleft
#undef fright
#undef knumleft
#undef knumright
#undef emitir

#endif
