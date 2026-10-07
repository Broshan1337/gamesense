

#define lj_opt_split_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT && (LJ_SOFTFP32 || (LJ_32 && LJ_HASFFI))

#include "lj_err.h"
#include "lj_buf.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_ircall.h"
#include "lj_iropt.h"
#include "lj_dispatch.h"
#include "lj_vm.h"




#define IR(ref)		(&J->cur.ir[(ref)])


static IRRef split_emit(jit_State *J, uint16_t ot, IRRef1 op1, IRRef1 op2)
{
  IRRef nref = lj_ir_nextins(J);
  IRIns *ir = IR(nref);
  ir->ot = ot;
  ir->op1 = op1;
  ir->op2 = op2;
  return nref;
}

#if LJ_SOFTFP

static IRRef split_num2int(jit_State *J, IRRef lo, IRRef hi, int check)
{
  IRRef tmp, res;
#if LJ_LE
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), lo, hi);
#else
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), hi, lo);
#endif
  res = split_emit(J, IRTI(IR_CALLN), tmp, IRCALL_softfp_d2i);
  if (check) {
    tmp = split_emit(J, IRTI(IR_CALLN), res, IRCALL_softfp_i2d);
    split_emit(J, IRT(IR_HIOP, IRT_SOFTFP), tmp, tmp);
    split_emit(J, IRTGI(IR_EQ), tmp, lo);
    split_emit(J, IRTG(IR_HIOP, IRT_SOFTFP), tmp+1, hi);
  }
  return res;
}


static IRRef split_call_l(jit_State *J, IRRef1 *hisubst, IRIns *oir,
			  IRIns *ir, IRCallID id)
{
  IRRef tmp, op1 = ir->op1;
  J->cur.nins--;
#if LJ_LE
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), oir[op1].prev, hisubst[op1]);
#else
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), hisubst[op1], oir[op1].prev);
#endif
  ir->prev = tmp = split_emit(J, IRTI(IR_CALLN), tmp, id);
  return split_emit(J, IRT(IR_HIOP, IRT_SOFTFP), tmp, tmp);
}
#endif


static IRRef split_call_li(jit_State *J, IRRef1 *hisubst, IRIns *oir,
			   IRIns *ir, IRCallID id)
{
  IRRef tmp, op1 = ir->op1, op2 = ir->op2;
  J->cur.nins--;
#if LJ_LE
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), oir[op1].prev, hisubst[op1]);
#else
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), hisubst[op1], oir[op1].prev);
#endif
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), tmp, oir[op2].prev);
  ir->prev = tmp = split_emit(J, IRTI(IR_CALLN), tmp, id);
  return split_emit(J, IRT(IR_HIOP, IRT_SOFTFP), tmp, tmp);
}


static IRRef split_call_ll(jit_State *J, IRRef1 *hisubst, IRIns *oir,
			   IRIns *ir, IRCallID id)
{
  IRRef tmp, op1 = ir->op1, op2 = ir->op2;
  J->cur.nins--;
#if LJ_LE
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), oir[op1].prev, hisubst[op1]);
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), tmp, oir[op2].prev);
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), tmp, hisubst[op2]);
#else
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), hisubst[op1], oir[op1].prev);
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), tmp, hisubst[op2]);
  tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), tmp, oir[op2].prev);
#endif
  ir->prev = tmp = split_emit(J, IRTI(IR_CALLN), tmp, id);
  return split_emit(J,
    IRT(IR_HIOP, (LJ_SOFTFP && irt_isnum(ir->t)) ? IRT_SOFTFP : IRT_INT),
    tmp, tmp);
}


static IRRef split_ptr(jit_State *J, IRIns *oir, IRRef ref)
{
  IRRef nref = oir[ref].prev;
  IRIns *ir = IR(nref);
  int32_t ofs = 4;
  if (ir->o == IR_KPTR)
    return lj_ir_kptr(J, (char *)ir_kptr(ir) + ofs);
  if (ir->o == IR_ADD && irref_isk(ir->op2) && !irt_isphi(oir[ref].t)) {
    
    ofs += IR(ir->op2)->i;
    nref = ir->op1;
    if (ofs == 0) return nref;
  }
  return split_emit(J, IRT(IR_ADD, IRT_PTR), nref, lj_ir_kint(J, ofs));
}

#if LJ_HASFFI
static IRRef split_bitshift(jit_State *J, IRRef1 *hisubst,
			    IRIns *oir, IRIns *nir, IRIns *ir)
{
  IROp op = ir->o;
  IRRef kref = nir->op2;
  if (irref_isk(kref)) {  
    int32_t k = (IR(kref)->i & 63);
    IRRef lo = nir->op1, hi = hisubst[ir->op1];
    if (op == IR_BROL || op == IR_BROR) {
      if (op == IR_BROR) k = (-k & 63);
      if (k >= 32) { IRRef t = lo; lo = hi; hi = t; k -= 32; }
      if (k == 0) {
      passthrough:
	J->cur.nins--;
	ir->prev = lo;
	return hi;
      } else {
	TRef k1, k2;
	IRRef t1, t2, t3, t4;
	J->cur.nins--;
	k1 = lj_ir_kint(J, k);
	k2 = lj_ir_kint(J, (-k & 31));
	t1 = split_emit(J, IRTI(IR_BSHL), lo, k1);
	t2 = split_emit(J, IRTI(IR_BSHL), hi, k1);
	t3 = split_emit(J, IRTI(IR_BSHR), lo, k2);
	t4 = split_emit(J, IRTI(IR_BSHR), hi, k2);
	ir->prev = split_emit(J, IRTI(IR_BOR), t1, t4);
	return split_emit(J, IRTI(IR_BOR), t2, t3);
      }
    } else if (k == 0) {
      goto passthrough;
    } else if (k < 32) {
      if (op == IR_BSHL) {
	IRRef t1 = split_emit(J, IRTI(IR_BSHL), hi, kref);
	IRRef t2 = split_emit(J, IRTI(IR_BSHR), lo, lj_ir_kint(J, (-k&31)));
	return split_emit(J, IRTI(IR_BOR), t1, t2);
      } else {
	IRRef t1 = ir->prev, t2;
	lj_assertJ(op == IR_BSHR || op == IR_BSAR, "bad usage");
	nir->o = IR_BSHR;
	t2 = split_emit(J, IRTI(IR_BSHL), hi, lj_ir_kint(J, (-k&31)));
	ir->prev = split_emit(J, IRTI(IR_BOR), t1, t2);
	return split_emit(J, IRTI(op), hi, kref);
      }
    } else {
      if (op == IR_BSHL) {
	if (k == 32)
	  J->cur.nins--;
	else
	  lo = ir->prev;
	ir->prev = lj_ir_kint(J, 0);
	return lo;
      } else {
	lj_assertJ(op == IR_BSHR || op == IR_BSAR, "bad usage");
	if (k == 32) {
	  J->cur.nins--;
	  ir->prev = hi;
	} else {
	  nir->op1 = hi;
	}
	if (op == IR_BSHR)
	  return lj_ir_kint(J, 0);
	else
	  return split_emit(J, IRTI(IR_BSAR), hi, lj_ir_kint(J, 31));
      }
    }
  }
  return split_call_li(J, hisubst, oir, ir,
		       op - IR_BSHL + IRCALL_lj_carith_shl64);
}

static IRRef split_bitop(jit_State *J, IRRef1 *hisubst,
			 IRIns *nir, IRIns *ir)
{
  IROp op = ir->o;
  IRRef hi, kref = nir->op2;
  if (irref_isk(kref)) {  
    int32_t k = IR(kref)->i;
    if (k == 0 || k == -1) {
      if (op == IR_BAND) k = ~k;
      if (k == 0) {
	J->cur.nins--;
	ir->prev = nir->op1;
      } else if (op == IR_BXOR) {
	nir->o = IR_BNOT;
	nir->op2 = 0;
      } else {
	J->cur.nins--;
	ir->prev = kref;
      }
    }
  }
  hi = hisubst[ir->op1];
  kref = hisubst[ir->op2];
  if (irref_isk(kref)) {  
    int32_t k = IR(kref)->i;
    if (k == 0 || k == -1) {
      if (op == IR_BAND) k = ~k;
      if (k == 0) {
	return hi;
      } else if (op == IR_BXOR) {
	return split_emit(J, IRTI(IR_BNOT), hi, 0);
      } else {
	return kref;
      }
    }
  }
  return split_emit(J, IRTI(op), hi, kref);
}
#endif


static void split_subst_snap(jit_State *J, SnapShot *snap, IRIns *oir)
{
  SnapEntry *map = &J->cur.snapmap[snap->mapofs];
  MSize n, nent = snap->nent;
  for (n = 0; n < nent; n++) {
    SnapEntry sn = map[n];
    IRIns *ir = &oir[snap_ref(sn)];
    if (!(LJ_SOFTFP && (sn & SNAP_SOFTFPNUM) && irref_isk(snap_ref(sn))))
      map[n] = ((sn & 0xffff0000) | ir->prev);
  }
}


static void split_ir(jit_State *J)
{
  IRRef nins = J->cur.nins, nk = J->cur.nk;
  MSize irlen = nins - nk;
  MSize need = (irlen+1)*(sizeof(IRIns) + sizeof(IRRef1));
  IRIns *oir = (IRIns *)lj_buf_tmp(J->L, need);
  IRRef1 *hisubst;
  IRRef ref, snref;
  SnapShot *snap;

  
  memcpy(oir, IR(nk), irlen*sizeof(IRIns));
  
  hisubst = (IRRef1 *)&oir[irlen] - nk;
  oir -= nk;

  
  J->cur.nins = REF_FIRST;
  J->loopref = 0;

  
  for (ref = nk; ref <= REF_BASE; ref++) {
    IRIns *ir = &oir[ref];
    if ((LJ_SOFTFP && ir->o == IR_KNUM) || ir->o == IR_KINT64) {
      
      TValue tv = *ir_k64(ir);
      ir->prev = lj_ir_kint(J, (int32_t)tv.u32.lo);
      hisubst[ref] = lj_ir_kint(J, (int32_t)tv.u32.hi);
    } else {
      ir->prev = ref;  
      hisubst[ref] = 0;
    }
    if (irt_is64(ir->t) && ir->o != IR_KNULL)
      ref++;
  }

  
  snap = J->cur.snap;
  snref = snap->ref;
  for (ref = REF_FIRST; ref < nins; ref++) {
    IRIns *ir = &oir[ref];
    IRRef nref = lj_ir_nextins(J);
    IRIns *nir = IR(nref);
    IRRef hi = 0;

    if (ref >= snref) {
      snap->ref = nref;
      split_subst_snap(J, snap++, oir);
      snref = snap < &J->cur.snap[J->cur.nsnap] ? snap->ref : ~(IRRef)0;
    }

    
    nir->op1 = ir->op1 < nk ? ir->op1 : oir[ir->op1].prev;
    nir->op2 = ir->op2 < nk ? ir->op2 : oir[ir->op2].prev;
    ir->prev = nref;  
    nir->o = ir->o;
    nir->t.irt = ir->t.irt & ~(IRT_MARK|IRT_ISPHI);
    hisubst[ref] = 0;

    
#if LJ_SOFTFP
    if (irt_isnum(ir->t)) {
      nir->t.irt = IRT_INT | (nir->t.irt & IRT_GUARD);  
      
      switch (ir->o) {
      case IR_ADD:
	hi = split_call_ll(J, hisubst, oir, ir, IRCALL_softfp_add);
	break;
      case IR_SUB:
	hi = split_call_ll(J, hisubst, oir, ir, IRCALL_softfp_sub);
	break;
      case IR_MUL:
	hi = split_call_ll(J, hisubst, oir, ir, IRCALL_softfp_mul);
	break;
      case IR_DIV:
	hi = split_call_ll(J, hisubst, oir, ir, IRCALL_softfp_div);
	break;
      case IR_POW:
	hi = split_call_ll(J, hisubst, oir, ir, IRCALL_pow);
	break;
      case IR_FPMATH:
	hi = split_call_l(J, hisubst, oir, ir, IRCALL_lj_vm_floor + ir->op2);
	break;
      case IR_LDEXP:
	hi = split_call_li(J, hisubst, oir, ir, IRCALL_ldexp);
	break;
      case IR_NEG: case IR_ABS:
	nir->o = IR_CONV;  
	nir->op2 = (IRT_INT << 5) | IRT_INT;
	hi = split_emit(J, IRT(ir->o == IR_NEG ? IR_BXOR : IR_BAND, IRT_SOFTFP),
	       hisubst[ir->op1],
	       lj_ir_kint(J, (int32_t)(0x7fffffffu + (ir->o == IR_NEG))));
	break;
      case IR_SLOAD:
	if ((nir->op2 & IRSLOAD_CONVERT)) {  
	  nir->op2 &= ~IRSLOAD_CONVERT;
	  ir->prev = nref = split_emit(J, IRTI(IR_CALLN), nref,
				       IRCALL_softfp_i2d);
	  hi = split_emit(J, IRT(IR_HIOP, IRT_SOFTFP), nref, nref);
	  break;
	}
	
      case IR_ALOAD: case IR_HLOAD: case IR_ULOAD: case IR_VLOAD:
      case IR_STRTO:
	hi = split_emit(J, IRT(IR_HIOP, IRT_SOFTFP), nref, nref);
	break;
      case IR_FLOAD:
	lj_assertJ(ir->op1 == REF_NIL, "expected FLOAD from GG_State");
	hi = lj_ir_kint(J, *(int32_t*)((char*)J2GG(J) + ir->op2 + LJ_LE*4));
	nir->op2 += LJ_BE*4;
	break;
      case IR_XLOAD: {
	IRIns inslo = *nir;  
	J->cur.nins--;
	hi = split_ptr(J, oir, ir->op1);  
#if LJ_BE
	hi = split_emit(J, IRT(IR_XLOAD, IRT_INT), hi, ir->op2);
	inslo.t.irt = IRT_SOFTFP | (inslo.t.irt & IRT_GUARD);
#endif
	nref = lj_ir_nextins(J);
	nir = IR(nref);
	*nir = inslo;  
#if LJ_LE
	hi = split_emit(J, IRT(IR_XLOAD, IRT_SOFTFP), hi, ir->op2);
	ir->prev = nref;
#else
	ir->prev = hi; hi = nref;
#endif
	break;
	}
      case IR_ASTORE: case IR_HSTORE: case IR_USTORE: case IR_XSTORE:
	split_emit(J, IRT(IR_HIOP, IRT_SOFTFP), nir->op1, hisubst[ir->op2]);
	break;
      case IR_CONV: {  
	IRType st = (IRType)(ir->op2 & IRCONV_SRCMASK);
	UNUSED(st);
#if LJ_32 && LJ_HASFFI
	if (st == IRT_I64 || st == IRT_U64) {
	  hi = split_call_l(J, hisubst, oir, ir,
		 st == IRT_I64 ? IRCALL_fp64_l2d : IRCALL_fp64_ul2d);
	  break;
	}
#endif
	lj_assertJ(st == IRT_INT ||
		   (LJ_32 && LJ_HASFFI && (st == IRT_U32 || st == IRT_FLOAT)),
		   "bad source type for CONV");
	nir->o = IR_CALLN;
#if LJ_32 && LJ_HASFFI
	nir->op2 = st == IRT_INT ? IRCALL_softfp_i2d :
		   st == IRT_FLOAT ? IRCALL_softfp_f2d :
		   IRCALL_softfp_ui2d;
#else
	nir->op2 = IRCALL_softfp_i2d;
#endif
	hi = split_emit(J, IRT(IR_HIOP, IRT_SOFTFP), nref, nref);
	break;
	}
      case IR_CALLN:
      case IR_CALLL:
      case IR_CALLS:
      case IR_CALLXS:
	goto split_call;
      case IR_PHI:
	if (nir->op1 == nir->op2)
	  J->cur.nins--;  
	if (hisubst[ir->op1] != hisubst[ir->op2])
	  split_emit(J, IRT(IR_PHI, IRT_SOFTFP),
		     hisubst[ir->op1], hisubst[ir->op2]);
	break;
      case IR_HIOP:
	J->cur.nins--;  
	ir->prev = nir->op1;
	hi = nir->op2;
	break;
      default:
	lj_assertJ(ir->o <= IR_NE || ir->o == IR_MIN || ir->o == IR_MAX,
		   "bad IR op %d", ir->o);
	hi = split_emit(J, IRTG(IR_HIOP, IRT_SOFTFP),
			hisubst[ir->op1], hisubst[ir->op2]);
	break;
      }
    } else
#endif
#if LJ_32 && LJ_HASFFI
    if (irt_isint64(ir->t)) {
      IRRef hiref = hisubst[ir->op1];
      nir->t.irt = IRT_INT | (nir->t.irt & IRT_GUARD);  
      switch (ir->o) {
      case IR_ADD:
      case IR_SUB:
	
	if (irref_isk(nir->op2) && IR(nir->op2)->i == 0) {
	  ir->prev = nir->op1;  
	  nir->op1 = hiref; nir->op2 = hisubst[ir->op2];
	  hi = nref;
	  break;
	}
	
      case IR_NEG:
	hi = split_emit(J, IRTI(IR_HIOP), hiref, hisubst[ir->op2]);
	break;
      case IR_MUL:
	hi = split_call_ll(J, hisubst, oir, ir, IRCALL_lj_carith_mul64);
	break;
      case IR_DIV:
	hi = split_call_ll(J, hisubst, oir, ir,
			   irt_isi64(ir->t) ? IRCALL_lj_carith_divi64 :
					      IRCALL_lj_carith_divu64);
	break;
      case IR_MOD:
	hi = split_call_ll(J, hisubst, oir, ir,
			   irt_isi64(ir->t) ? IRCALL_lj_carith_modi64 :
					      IRCALL_lj_carith_modu64);
	break;
      case IR_POW:
	hi = split_call_ll(J, hisubst, oir, ir,
			   irt_isi64(ir->t) ? IRCALL_lj_carith_powi64 :
					      IRCALL_lj_carith_powu64);
	break;
      case IR_BNOT:
	hi = split_emit(J, IRTI(IR_BNOT), hiref, 0);
	break;
      case IR_BSWAP:
	ir->prev = split_emit(J, IRTI(IR_BSWAP), hiref, 0);
	hi = nref;
	break;
      case IR_BAND: case IR_BOR: case IR_BXOR:
	hi = split_bitop(J, hisubst, nir, ir);
	break;
      case IR_BSHL: case IR_BSHR: case IR_BSAR: case IR_BROL: case IR_BROR:
	hi = split_bitshift(J, hisubst, oir, nir, ir);
	break;
      case IR_FLOAD:
	lj_assertJ(ir->op2 == IRFL_CDATA_INT64, "only INT64 supported");
	hi = split_emit(J, IRTI(IR_FLOAD), nir->op1, IRFL_CDATA_INT64_4);
#if LJ_BE
	ir->prev = hi; hi = nref;
#endif
	break;
      case IR_XLOAD:
	hi = split_emit(J, IRTI(IR_XLOAD), split_ptr(J, oir, ir->op1), ir->op2);
#if LJ_BE
	ir->prev = hi; hi = nref;
#endif
	break;
      case IR_XSTORE:
	split_emit(J, IRTI(IR_HIOP), nir->op1, hisubst[ir->op2]);
	break;
      case IR_CONV: {  
	IRType st = (IRType)(ir->op2 & IRCONV_SRCMASK);
#if LJ_SOFTFP
	lj_assertJ(st != IRT_FLOAT, "bad CONV *64.float emitted");
	if (st == IRT_NUM) {  
	  hi = split_call_l(J, hisubst, oir, ir, IRCALL_lj_vm_num2u64);
	}
#else
	if (st == IRT_NUM || st == IRT_FLOAT) {  
	  hi = split_emit(J, IRTI(IR_HIOP), nir->op1, nref);
	}
#endif
	else if (st == IRT_I64 || st == IRT_U64) {  
	  
	  hi = hiref;
	  goto fwdlo;
	} else if ((ir->op2 & IRCONV_SEXT)) {  
	  IRRef k31 = lj_ir_kint(J, 31);
	  nir = IR(nref);  
	  ir->prev = nir->op1;  
	  nir->o = IR_BSAR;  
	  nir->op2 = k31;
	  hi = nref;
	} else {  
	  hi = lj_ir_kint(J, 0);
	  goto fwdlo;
	}
	break;
	}
      case IR_CALLXS:
	goto split_call;
      case IR_PHI: {
	IRRef hiref2;
	if ((irref_isk(nir->op1) && irref_isk(nir->op2)) ||
	    nir->op1 == nir->op2)
	  J->cur.nins--;  
	hiref2 = hisubst[ir->op2];
	if (!((irref_isk(hiref) && irref_isk(hiref2)) || hiref == hiref2))
	  split_emit(J, IRTI(IR_PHI), hiref, hiref2);
	break;
	}
      case IR_HIOP:
	J->cur.nins--;  
	ir->prev = nir->op1;
	hi = nir->op2;
	break;
      default:
	lj_assertJ(ir->o <= IR_NE, "bad IR op %d", ir->o);  
	split_emit(J, IRTGI(IR_HIOP), hiref, hisubst[ir->op2]);
	break;
      }
    } else
#endif
#if LJ_SOFTFP
    if (ir->o == IR_SLOAD) {
      if ((nir->op2 & IRSLOAD_CONVERT)) {  
	nir->op2 &= ~IRSLOAD_CONVERT;
	if (!(nir->op2 & IRSLOAD_TYPECHECK))
	  nir->t.irt = IRT_INT;  
	split_emit(J, IRT(IR_HIOP, IRT_SOFTFP), nref, nref);
	ir->prev = split_num2int(J, nref, nref+1, irt_isguard(ir->t));
      }
    } else if (ir->o == IR_TOBIT) {
      IRRef tmp, op1 = ir->op1;
      J->cur.nins--;
#if LJ_LE
      tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), oir[op1].prev, hisubst[op1]);
#else
      tmp = split_emit(J, IRT(IR_CARG, IRT_NIL), hisubst[op1], oir[op1].prev);
#endif
      ir->prev = split_emit(J, IRTI(IR_CALLN), tmp, IRCALL_lj_vm_tobit);
    } else if (ir->o == IR_TOSTR || ir->o == IR_TMPREF) {
      if (hisubst[ir->op1]) {
	if (irref_isk(ir->op1))
	  nir->op1 = ir->op1;
	else
	  split_emit(J, IRT(IR_HIOP, IRT_NIL), hisubst[ir->op1], nref);
      }
    } else if (ir->o == IR_HREF || ir->o == IR_NEWREF) {
      if (irref_isk(ir->op2) && hisubst[ir->op2])
	nir->op2 = ir->op2;
    } else
#endif
    if (ir->o == IR_CONV) {  
      IRType st = (IRType)(ir->op2 & IRCONV_SRCMASK);
#if LJ_32 && LJ_HASFFI
      if (st == IRT_I64 || st == IRT_U64) {  
#if LJ_SOFTFP
	if (irt_isfloat(ir->t)) {
	  split_call_l(J, hisubst, oir, ir,
		       st == IRT_I64 ? IRCALL_fp64_l2f : IRCALL_fp64_ul2f);
	  J->cur.nins--;  
	}
#else
	if (irt_isfp(ir->t)) {  
	  ir->prev = split_emit(J, IRT(IR_HIOP, irt_type(ir->t)),
				hisubst[ir->op1], nref);
	}
#endif
	else {  
	fwdlo:
	  ir->prev = nir->op1;  
	  
	  nir->ot = IRT(IR_NOP, IRT_NIL);
	  nir->op1 = nir->op2 = 0;
	}
      }
#endif
#if LJ_SOFTFP && LJ_32 && LJ_HASFFI
      else if (irt_isfloat(ir->t)) {
	if (st == IRT_NUM) {
	  split_call_l(J, hisubst, oir, ir, IRCALL_softfp_d2f);
	  J->cur.nins--;  
	} else {
	  nir->o = IR_CALLN;
	  nir->op2 = st == IRT_INT ? IRCALL_softfp_i2f : IRCALL_softfp_ui2f;
	}
      } else if (st == IRT_FLOAT) {
	lj_assertJ(!irt_isu32(ir->t), "bad CONV u32.fp emitted");
	nir->o = IR_CALLN;
	nir->op2 = IRCALL_softfp_f2i;
      } else
#endif
#if LJ_SOFTFP
      if (st == IRT_NUM || (LJ_32 && LJ_HASFFI && st == IRT_FLOAT)) {
	if (irt_isguard(ir->t)) {
	  lj_assertJ(st == IRT_NUM && irt_isint(ir->t), "bad CONV types");
	  J->cur.nins--;
	  ir->prev = split_num2int(J, nir->op1, hisubst[ir->op1], 1);
	} else {
	  split_call_l(J, hisubst, oir, ir,
#if LJ_32 && LJ_HASFFI
	    st == IRT_NUM ? IRCALL_softfp_d2i : IRCALL_softfp_f2i
#else
	    IRCALL_softfp_d2i
#endif
	  );
	  J->cur.nins--;  
	}
      }
#endif
    } else if (ir->o == IR_CALLXS) {
      IRRef hiref;
    split_call:
      hiref = hisubst[ir->op1];
      if (hiref) {
	IROpT ot = nir->ot;
	IRRef op2 = nir->op2;
	nir->ot = IRT(IR_CARG, IRT_NIL);
#if LJ_LE
	nir->op2 = hiref;
#else
	nir->op2 = nir->op1; nir->op1 = hiref;
#endif
	ir->prev = nref = split_emit(J, ot, nref, op2);
      }
      if (LJ_SOFTFP ? irt_is64(ir->t) : irt_isint64(ir->t))
	hi = split_emit(J,
	  IRT(IR_HIOP, (LJ_SOFTFP && irt_isnum(ir->t)) ? IRT_SOFTFP : IRT_INT),
	  nref, nref);
    } else if (ir->o == IR_CARG) {
      IRRef hiref = hisubst[ir->op1];
      if (hiref) {
	IRRef op2 = nir->op2;
#if LJ_LE
	nir->op2 = hiref;
#else
	nir->op2 = nir->op1; nir->op1 = hiref;
#endif
	ir->prev = nref = split_emit(J, IRT(IR_CARG, IRT_NIL), nref, op2);
	nir = IR(nref);
      }
      hiref = hisubst[ir->op2];
      if (hiref) {
#if !LJ_TARGET_X86
	int carg = 0;
	IRIns *cir;
	for (cir = IR(nir->op1); cir->o == IR_CARG; cir = IR(cir->op1))
	  carg++;
	if ((carg & 1) == 0) {  
	  IRRef op2 = nir->op2;
	  nir->op2 = REF_NIL;
	  nref = split_emit(J, IRT(IR_CARG, IRT_NIL), nref, op2);
	  nir = IR(nref);
	}
#endif
#if LJ_BE
	{ IRRef tmp = nir->op2; nir->op2 = hiref; hiref = tmp; }
#endif
	ir->prev = split_emit(J, IRT(IR_CARG, IRT_NIL), nref, hiref);
      }
    } else if (ir->o == IR_CNEWI) {
      if (hisubst[ir->op2])
	split_emit(J, IRT(IR_HIOP, IRT_NIL), nref, hisubst[ir->op2]);
    } else if (ir->o == IR_LOOP) {
      J->loopref = nref;  
    }
    hisubst[ref] = hi;  
  }
  if (snref == nins) {  
    snap->ref = J->cur.nins;
    split_subst_snap(J, snap, oir);
  }

  
  for (ref = J->cur.nins-1; ref >= REF_FIRST; ref--) {
    IRIns *ir = IR(ref);
    if (ir->o != IR_PHI) break;
    if (!irref_isk(ir->op1)) irt_setphi(IR(ir->op1)->t);
    if (ir->op2 > J->loopref) irt_setphi(IR(ir->op2)->t);
  }
}


static TValue *cpsplit(lua_State *L, lua_CFunction dummy, void *ud)
{
  jit_State *J = (jit_State *)ud;
  split_ir(J);
  UNUSED(L); UNUSED(dummy);
  return NULL;
}

#if defined(LUA_USE_ASSERT) || LJ_SOFTFP

static int split_needsplit(jit_State *J)
{
  IRIns *ir, *irend;
  IRRef ref;
  for (ir = IR(REF_FIRST), irend = IR(J->cur.nins); ir < irend; ir++)
    if (LJ_SOFTFP ? irt_is64orfp(ir->t) : irt_isint64(ir->t))
      return 1;
  if (LJ_SOFTFP) {
    for (ref = J->chain[IR_SLOAD]; ref; ref = IR(ref)->prev)
      if ((IR(ref)->op2 & IRSLOAD_CONVERT))
	return 1;
    if (J->chain[IR_TOBIT])
      return 1;
  }
  for (ref = J->chain[IR_CONV]; ref; ref = IR(ref)->prev) {
    IRType st = (IR(ref)->op2 & IRCONV_SRCMASK);
    if ((LJ_SOFTFP && (st == IRT_NUM || st == IRT_FLOAT)) ||
	st == IRT_I64 || st == IRT_U64)
      return 1;
  }
  return 0;  
}
#endif


void lj_opt_split(jit_State *J)
{
#if LJ_SOFTFP
  if (!J->needsplit)
    J->needsplit = split_needsplit(J);
#else
  lj_assertJ(J->needsplit >= split_needsplit(J), "bad SPLIT state");
#endif
  if (J->needsplit) {
    int errcode = lj_vm_cpcall(J->L, NULL, J, cpsplit);
    if (errcode) {
      
      J->cur.nins = J->cur.nk = REF_BASE;
      J->cur.nsnap = 0;
      lj_err_throw(J->L, errcode);  
    }
  }
}

#undef IR

#endif
