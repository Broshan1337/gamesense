

#define lj_opt_narrow_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_bc.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#include "lj_vm.h"
#include "lj_strscan.h"




#define IR(ref)			(&J->cur.ir[(ref)])
#define fins			(&J->fold.ins)


#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))

#define emitir_raw(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_ir_emit(J))






#define NARROW_MAX_BACKPROP	100
#define NARROW_MAX_STACK	256


enum {
  NARROW_REF,		
  NARROW_CONV,		
  NARROW_SEXT,		
  NARROW_INT		
};

typedef uint32_t NarrowIns;

#define NARROWINS(op, ref)	(((op) << 16) + (ref))
#define narrow_op(ins)		((IROpT)((ins) >> 16))
#define narrow_ref(ins)		((IRRef1)(ins))


typedef struct NarrowConv {
  jit_State *J;		
  NarrowIns *sp;	
  NarrowIns *maxsp;	
  IRRef mode;		
  IRType t;		
  NarrowIns stack[NARROW_MAX_STACK];  
} NarrowConv;


static BPropEntry *narrow_bpc_get(jit_State *J, IRRef1 key, IRRef mode)
{
  ptrdiff_t i;
  for (i = 0; i < BPROP_SLOTS; i++) {
    BPropEntry *bp = &J->bpropcache[i];
    
    if (bp->key == key && bp->mode >= mode &&
	((bp->mode ^ mode) & IRCONV_MODEMASK) == 0)
      return bp;
  }
  return NULL;
}


static void narrow_bpc_set(jit_State *J, IRRef1 key, IRRef1 val, IRRef mode)
{
  uint32_t slot = J->bpropslot;
  BPropEntry *bp = &J->bpropcache[slot];
  J->bpropslot = (slot + 1) & (BPROP_SLOTS-1);
  bp->key = key;
  bp->val = val;
  bp->mode = mode;
}


static void narrow_stripov_backprop(NarrowConv *nc, IRRef ref, int depth)
{
  jit_State *J = nc->J;
  IRIns *ir = IR(ref);
  if (ir->o == IR_ADDOV || ir->o == IR_SUBOV ||
      (ir->o == IR_MULOV && (nc->mode & IRCONV_CONVMASK) == IRCONV_ANY)) {
    BPropEntry *bp = narrow_bpc_get(nc->J, ref, IRCONV_TOBIT);
    if (bp) {
      ref = bp->val;
    } else if (++depth < NARROW_MAX_BACKPROP && nc->sp < nc->maxsp) {
      NarrowIns *savesp = nc->sp;
      narrow_stripov_backprop(nc, ir->op1, depth);
      if (nc->sp < nc->maxsp) {
	narrow_stripov_backprop(nc, ir->op2, depth);
	if (nc->sp < nc->maxsp) {
	  *nc->sp++ = NARROWINS(IRT(ir->o - IR_ADDOV + IR_ADD, IRT_INT), ref);
	  return;
	}
      }
      nc->sp = savesp;  
    }
  }
  *nc->sp++ = NARROWINS(NARROW_REF, ref);
}


static int narrow_conv_backprop(NarrowConv *nc, IRRef ref, int depth)
{
  jit_State *J = nc->J;
  IRIns *ir = IR(ref);
  IRRef cref;

  if (nc->sp >= nc->maxsp) return 10;  

  
  if (ir->o == IR_CONV && (ir->op2 & IRCONV_SRCMASK) == IRT_INT) {
    if ((nc->mode & IRCONV_CONVMASK) <= IRCONV_ANY)
      narrow_stripov_backprop(nc, ir->op1, depth+1);
    else
      *nc->sp++ = NARROWINS(NARROW_REF, ir->op1);  
    if (nc->t == IRT_I64)
      *nc->sp++ = NARROWINS(NARROW_SEXT, 0);  
    return 0;
  } else if (ir->o == IR_KNUM) {  
    lua_Number n = ir_knum(ir)->n;
    int64_t i64;
    int32_t k;
    if ((nc->mode & IRCONV_CONVMASK) == IRCONV_TOBIT) {
      
      if (lj_num2int_check(n, i64, k)) {
	*nc->sp++ = NARROWINS(NARROW_INT, 0);
	*nc->sp++ = (NarrowIns)k;
	return 0;
      }
    } else if (lj_num2int_cond(n, i64, k, checki16((int32_t)i64))) {
      
      *nc->sp++ = NARROWINS(NARROW_INT, 0);
      *nc->sp++ = (NarrowIns)k;
      return 0;
    }
    return 10;  
  }

  
  cref = J->chain[fins->o];
  while (cref > ref) {
    IRIns *cr = IR(cref);
    if (cr->op1 == ref &&
	(fins->o == IR_TOBIT ||
	 ((cr->op2 & IRCONV_MODEMASK) == (nc->mode & IRCONV_MODEMASK) &&
	  irt_isguard(cr->t) >= irt_isguard(fins->t)))) {
      *nc->sp++ = NARROWINS(NARROW_REF, cref);
      return 0;  
    }
    cref = cr->prev;
  }

  
  if (ir->o == IR_ADD || ir->o == IR_SUB) {
    
    IRRef mode = nc->mode;
    BPropEntry *bp;
    
    if ((mode & IRCONV_CONVMASK) == IRCONV_INDEX && depth > 0)
      mode += IRCONV_CHECK-IRCONV_INDEX;
    bp = narrow_bpc_get(nc->J, (IRRef1)ref, mode);
    if (bp) {
      *nc->sp++ = NARROWINS(NARROW_REF, bp->val);
      return 0;
    } else if (nc->t == IRT_I64) {
      
      mode = (IRT_INT<<5)|IRT_NUM|IRCONV_INDEX;
      bp = narrow_bpc_get(nc->J, (IRRef1)ref, mode);
      if (bp) {
	*nc->sp++ = NARROWINS(NARROW_REF, bp->val);
	*nc->sp++ = NARROWINS(NARROW_SEXT, 0);
	return 0;
      }
    }
    if (++depth < NARROW_MAX_BACKPROP && nc->sp < nc->maxsp) {
      NarrowIns *savesp = nc->sp;
      int count = narrow_conv_backprop(nc, ir->op1, depth);
      count += narrow_conv_backprop(nc, ir->op2, depth);
      
      if (count <= 1 && nc->sp < nc->maxsp) {
	*nc->sp++ = NARROWINS(IRT(ir->o, nc->t), ref);
	return count;
      }
      nc->sp = savesp;  
    }
  }

  
  *nc->sp++ = NARROWINS(NARROW_CONV, ref);
  return 1;
}


static IRRef narrow_conv_emit(jit_State *J, NarrowConv *nc)
{
  
  IROpT guardot = irt_isguard(fins->t) ? IRTG(IR_ADDOV-IR_ADD, 0) : 0;
  IROpT convot = fins->ot;
  IRRef1 convop2 = fins->op2;
  NarrowIns *next = nc->stack;  
  NarrowIns *last = nc->sp;
  NarrowIns *sp = nc->stack;  
  while (next < last) {  
    NarrowIns ref = *next++;
    IROpT op = narrow_op(ref);
    if (op == NARROW_REF) {
      *sp++ = ref;
    } else if (op == NARROW_CONV) {
      *sp++ = emitir_raw(convot, ref, convop2);  
    } else if (op == NARROW_SEXT) {
      lj_assertJ(sp >= nc->stack+1, "stack underflow");
      sp[-1] = emitir(IRT(IR_CONV, IRT_I64), sp[-1],
		      (IRT_I64<<5)|IRT_INT|IRCONV_SEXT);
    } else if (op == NARROW_INT) {
      lj_assertJ(next < last, "missing arg to NARROW_INT");
      *sp++ = nc->t == IRT_I64 ?
	      lj_ir_kint64(J, (int64_t)(int32_t)*next++) :
	      lj_ir_kint(J, *next++);
    } else {  
      IRRef mode = nc->mode;
      lj_assertJ(sp >= nc->stack+2, "stack underflow");
      sp--;
      
      if ((mode & IRCONV_CONVMASK) == IRCONV_INDEX) {
	if (next == last && irref_isk(narrow_ref(sp[0])) &&
	  (uint32_t)IR(narrow_ref(sp[0]))->i + 0x40000000u < 0x80000000u)
	  guardot = 0;
	else  
	  mode += IRCONV_CHECK-IRCONV_INDEX;
      }
      sp[-1] = emitir(op+guardot, sp[-1], sp[0]);
      
      if (narrow_ref(ref))
	narrow_bpc_set(J, narrow_ref(ref), narrow_ref(sp[-1]), mode);
    }
  }
  lj_assertJ(sp == nc->stack+1, "stack misalignment");
  return nc->stack[0];
}


TRef LJ_FASTCALL lj_opt_narrow_convert(jit_State *J)
{
  if ((J->flags & JIT_F_OPT_NARROW)) {
    NarrowConv nc;
    nc.J = J;
    nc.sp = nc.stack;
    nc.maxsp = &nc.stack[NARROW_MAX_STACK-4];
    nc.t = irt_type(fins->t);
    if (fins->o == IR_TOBIT) {
      nc.mode = IRCONV_TOBIT;  
    } else {
      nc.mode = fins->op2;
    }
    if (narrow_conv_backprop(&nc, fins->op1, 0) <= 1)
      return narrow_conv_emit(J, &nc);
  }
  return NEXTFOLD;
}




static TRef narrow_stripov(jit_State *J, TRef tr, int lastop, IRRef mode)
{
  IRRef ref = tref_ref(tr);
  IRIns *ir = IR(ref);
  int op = ir->o;
  if (op >= IR_ADDOV && op <= lastop) {
    BPropEntry *bp = narrow_bpc_get(J, ref, mode);
    if (bp) {
      return TREF(bp->val, irt_t(IR(bp->val)->t));
    } else {
      IRRef op1 = ir->op1, op2 = ir->op2;  
      op1 = narrow_stripov(J, op1, lastop, mode);
      op2 = narrow_stripov(J, op2, lastop, mode);
      tr = emitir(IRT(op - IR_ADDOV + IR_ADD,
		      ((mode & IRCONV_DSTMASK) >> IRCONV_DSH)), op1, op2);
      narrow_bpc_set(J, ref, tref_ref(tr), mode);
    }
  } else if (LJ_64 && (mode & IRCONV_SEXT) && !irt_is64(ir->t)) {
    tr = emitir(IRT(IR_CONV, IRT_INTP), tr, mode);
  }
  return tr;
}


TRef LJ_FASTCALL lj_opt_narrow_index(jit_State *J, TRef tr)
{
  IRIns *ir;
  lj_assertJ(tref_isnumber(tr), "expected number type");
  if (tref_isnum(tr))  
    return emitir(IRTGI(IR_CONV), tr, IRCONV_INT_NUM|IRCONV_INDEX);
  
  ir = IR(tref_ref(tr));
  if ((ir->o == IR_ADDOV || ir->o == IR_SUBOV) && irref_isk(ir->op2) &&
      (uint32_t)IR(ir->op2)->i + 0x40000000u < 0x80000000u)
    return emitir(IRTI(ir->o - IR_ADDOV + IR_ADD), ir->op1, ir->op2);
  return tr;
}


TRef LJ_FASTCALL lj_opt_narrow_toint(jit_State *J, TRef tr)
{
  if (tref_isstr(tr))
    tr = emitir(IRTG(IR_STRTO, IRT_NUM), tr, 0);
  if (tref_isnum(tr))  
    return emitir(IRTI(IR_CONV), tr, IRCONV_INT_NUM|IRCONV_ANY);
  if (!tref_isinteger(tr))
    lj_trace_err(J, LJ_TRERR_BADTYPE);
  
  return narrow_stripov(J, tr, IR_MULOV, (IRT_INT<<5)|IRT_INT|IRCONV_TOBIT);
}


TRef LJ_FASTCALL lj_opt_narrow_tobit(jit_State *J, TRef tr)
{
  if (tref_isstr(tr))
    tr = emitir(IRTG(IR_STRTO, IRT_NUM), tr, 0);
  if (tref_isnum(tr))  
    return emitir(IRTI(IR_TOBIT), tr, lj_ir_knum_tobit(J));
  if (!tref_isinteger(tr))
    lj_trace_err(J, LJ_TRERR_BADTYPE);
  
  return narrow_stripov(J, tr, IR_SUBOV, (IRT_INT<<5)|IRT_INT|IRCONV_TOBIT);
}

#if LJ_HASFFI

TRef LJ_FASTCALL lj_opt_narrow_cindex(jit_State *J, TRef tr)
{
  lj_assertJ(tref_isnumber(tr), "expected number type");
  if (tref_isnum(tr))
    return emitir(IRT(IR_CONV, IRT_INTP), tr, (IRT_INTP<<5)|IRT_NUM|IRCONV_ANY);
  
  return narrow_stripov(J, tr, IR_MULOV,
			LJ_64 ? ((IRT_INTP<<5)|IRT_INT|IRCONV_SEXT) :
				((IRT_INTP<<5)|IRT_INT|IRCONV_TOBIT));
}
#endif




static TRef conv_str_tonum(jit_State *J, TRef tr, TValue *o)
{
  if (tref_isstr(tr)) {
    tr = emitir(IRTG(IR_STRTO, IRT_NUM), tr, 0);
    
    if (!lj_strscan_num(strV(o), o))  
      lj_trace_err(J, LJ_TRERR_BADTYPE);  
  }
  return tr;
}


TRef lj_opt_narrow_arith(jit_State *J, TRef rb, TRef rc,
			 TValue *vb, TValue *vc, IROp op)
{
  rb = conv_str_tonum(J, rb, vb);
  rc = conv_str_tonum(J, rc, vc);
  
  if ((op >= IR_ADD && op <= (LJ_DUALNUM ? IR_MUL : IR_SUB)) &&
      tref_isinteger(rb) && tref_isinteger(rc) &&
      lj_num2int_ok(lj_vm_foldarith(numberVnum(vb), numberVnum(vc),
				    (int)op - (int)IR_ADD)))
    return emitir(IRTGI((int)op - (int)IR_ADD + (int)IR_ADDOV), rb, rc);
  if (!tref_isnum(rb)) rb = emitir(IRTN(IR_CONV), rb, IRCONV_NUM_INT);
  if (!tref_isnum(rc)) rc = emitir(IRTN(IR_CONV), rc, IRCONV_NUM_INT);
  return emitir(IRTN(op), rb, rc);
}


TRef lj_opt_narrow_unm(jit_State *J, TRef rc, TValue *vc)
{
  rc = conv_str_tonum(J, rc, vc);
  if (tref_isinteger(rc)) {
    uint32_t k = (uint32_t)numberVint(vc);
    if (k != 0 && k != 0x80000000u) {
      TRef zero = lj_ir_kint(J, 0);
      emitir(IRTGI(IR_NE), rc, zero);
      return emitir(IRTGI(IR_SUBOV), zero, rc);
    }
    rc = emitir(IRTN(IR_CONV), rc, IRCONV_NUM_INT);
  }
  return emitir(IRTN(IR_NEG), rc, lj_ir_ksimd(J, LJ_KSIMD_NEG));
}


TRef lj_opt_narrow_mod(jit_State *J, TRef rb, TRef rc, TValue *vb, TValue *vc)
{
  TRef tmp;
  rb = conv_str_tonum(J, rb, vb);
  rc = conv_str_tonum(J, rc, vc);
  if ((LJ_DUALNUM || (J->flags & JIT_F_OPT_NARROW)) &&
      tref_isinteger(rb) && tref_isinteger(rc) &&
      (tvisint(vc) ? intV(vc) != 0 : !tviszero(vc))) {
    emitir(IRTGI(IR_NE), rc, lj_ir_kint(J, 0));
    return emitir(IRTI(IR_MOD), rb, rc);
  }
  
  rb = lj_ir_tonum(J, rb);
  rc = lj_ir_tonum(J, rc);
  tmp = emitir(IRTN(IR_DIV), rb, rc);
  tmp = emitir(IRTN(IR_FPMATH), tmp, IRFPM_FLOOR);
  tmp = emitir(IRTN(IR_MUL), tmp, rc);
  return emitir(IRTN(IR_SUB), rb, tmp);
}




static int narrow_forl(jit_State *J, cTValue *o)
{
  if (tvisint(o)) return 1;
  if (LJ_DUALNUM || (J->flags & JIT_F_OPT_NARROW)) return lj_num2int_ok(numV(o));
  return 0;
}


IRType lj_opt_narrow_forl(jit_State *J, cTValue *tv)
{
  lj_assertJ(tvisnumber(&tv[FORL_IDX]) &&
	     tvisnumber(&tv[FORL_STOP]) &&
	     tvisnumber(&tv[FORL_STEP]),
	     "expected number types");
  
  if (narrow_forl(J, &tv[FORL_IDX]) &&
      narrow_forl(J, &tv[FORL_STOP]) &&
      narrow_forl(J, &tv[FORL_STEP])) {
    
    lua_Number step = numberVnum(&tv[FORL_STEP]);
    lua_Number sum = numberVnum(&tv[FORL_STOP]) + step;
    if (0 <= step ? (sum <= 2147483647.0) : (sum >= -2147483648.0))
      return IRT_INT;
  }
  return IRT_NUM;
}

#undef IR
#undef fins
#undef emitir
#undef emitir_raw

#endif
