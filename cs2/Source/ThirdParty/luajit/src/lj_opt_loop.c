

#define lj_opt_loop_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_err.h"
#include "lj_buf.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_iropt.h"
#include "lj_trace.h"
#include "lj_snap.h"
#include "lj_vm.h"




#define IR(ref)		(&J->cur.ir[(ref)])


#define emitir(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_opt_fold(J))


#define emitir_raw(ot, a, b)	(lj_ir_set(J, (ot), (a), (b)), lj_ir_emit(J))




static void loop_emit_phi(jit_State *J, IRRef1 *subst, IRRef1 *phi, IRRef nphi,
			  SnapNo onsnap)
{
  int passx = 0;
  IRRef i, j, nslots;
  IRRef invar = J->chain[IR_LOOP];
  
  for (i = 0, j = 0; i < nphi; i++) {
    IRRef lref = phi[i];
    IRRef rref = subst[lref];
    if (lref == rref || rref == REF_DROP) {  
      irt_clearphi(IR(lref)->t);
    } else {
      phi[j++] = (IRRef1)lref;
      if (!(IR(rref)->op1 == lref || IR(rref)->op2 == lref)) {
	
	irt_setmark(IR(lref)->t);
	passx = 1;
      }
    }
  }
  nphi = j;
  
  if (passx) {
    SnapNo s;
    for (i = J->cur.nins-1; i > invar; i--) {
      IRIns *ir = IR(i);
      if (!irref_isk(ir->op2)) irt_clearmark(IR(ir->op2)->t);
      if (!irref_isk(ir->op1)) {
	irt_clearmark(IR(ir->op1)->t);
	if (ir->op1 < invar &&
	    ir->o >= IR_CALLN && ir->o <= IR_CARG) {  
	  ir = IR(ir->op1);
	  while (ir->o == IR_CARG) {
	    if (!irref_isk(ir->op2)) irt_clearmark(IR(ir->op2)->t);
	    if (irref_isk(ir->op1)) break;
	    ir = IR(ir->op1);
	    irt_clearmark(ir->t);
	  }
	}
      }
    }
    for (s = J->cur.nsnap-1; s >= onsnap; s--) {
      SnapShot *snap = &J->cur.snap[s];
      SnapEntry *map = &J->cur.snapmap[snap->mapofs];
      MSize n, nent = snap->nent;
      for (n = 0; n < nent; n++) {
	IRRef ref = snap_ref(map[n]);
	if (!irref_isk(ref)) irt_clearmark(IR(ref)->t);
      }
    }
  }
  
  nslots = J->baseslot+J->maxslot;
  for (i = 1; i < nslots; i++) {
    IRRef ref = tref_ref(J->slot[i]);
    while (!irref_isk(ref) && ref != subst[ref]) {
      IRIns *ir = IR(ref);
      irt_clearmark(ir->t);  
      if (irt_isphi(ir->t) || irt_ispri(ir->t))
	break;
      irt_setphi(ir->t);
      if (nphi >= LJ_MAX_PHI)
	lj_trace_err(J, LJ_TRERR_PHIOV);
      phi[nphi++] = (IRRef1)ref;
      ref = subst[ref];
      if (ref > invar)
	break;
    }
  }
  
  while (passx) {
    passx = 0;
    for (i = 0; i < nphi; i++) {
      IRRef lref = phi[i];
      IRIns *ir = IR(lref);
      if (!irt_ismarked(ir->t)) {  
	IRIns *irr = IR(subst[lref]);
	if (irt_ismarked(irr->t)) {  
	  irt_clearmark(irr->t);  
	  passx = 1;  
	}
      }
    }
  }
  
  for (i = 0; i < nphi; i++) {
    IRRef lref = phi[i];
    IRIns *ir = IR(lref);
    if (!irt_ismarked(ir->t)) {  
      IRRef rref = subst[lref];
      if (rref > invar)
	irt_setphi(IR(rref)->t);
      emitir_raw(IRT(IR_PHI, irt_type(ir->t)), lref, rref);
    } else {  
      irt_clearmark(ir->t);
      irt_clearphi(ir->t);
    }
  }
}




static void loop_subst_snap(jit_State *J, SnapShot *osnap,
			    SnapEntry *loopmap, IRRef1 *subst)
{
  SnapEntry *nmap, *omap = &J->cur.snapmap[osnap->mapofs];
  SnapEntry *nextmap = &J->cur.snapmap[snap_nextofs(&J->cur, osnap)];
  MSize nmapofs;
  MSize on, ln, nn, onent = osnap->nent;
  BCReg nslots = osnap->nslots;
  SnapShot *snap = &J->cur.snap[J->cur.nsnap];
  if (irt_isguard(J->guardemit)) {  
    nmapofs = J->cur.nsnapmap;
    J->cur.nsnap++;  
  } else {  
    snap--;
    nmapofs = snap->mapofs;
  }
  J->guardemit.irt = 0;
  
  snap->mapofs = (uint32_t)nmapofs;
  snap->ref = (IRRef1)J->cur.nins;
  snap->mcofs = 0;
  snap->nslots = nslots;
  snap->topslot = osnap->topslot;
  snap->count = 0;
  nmap = &J->cur.snapmap[nmapofs];
  
  on = ln = nn = 0;
  while (on < onent) {
    SnapEntry osn = omap[on], lsn = loopmap[ln];
    if (snap_slot(lsn) < snap_slot(osn)) {  
      nmap[nn++] = lsn;
      ln++;
    } else {  
      if (snap_slot(lsn) == snap_slot(osn)) ln++;  
      if (!irref_isk(snap_ref(osn)))
	osn = snap_setref(osn, subst[snap_ref(osn)]);
      nmap[nn++] = osn;
      on++;
    }
  }
  while (snap_slot(loopmap[ln]) < nslots)  
    nmap[nn++] = loopmap[ln++];
  snap->nent = (uint8_t)nn;
  omap += onent;
  nmap += nn;
  while (omap < nextmap)  
    *nmap++ = *omap++;
  J->cur.nsnapmap = (uint32_t)(nmap - J->cur.snapmap);
}

typedef struct LoopState {
  jit_State *J;
  IRRef1 *subst;
  MSize sizesubst;
} LoopState;


static void loop_unroll(LoopState *lps)
{
  jit_State *J = lps->J;
  IRRef1 phi[LJ_MAX_PHI];
  uint32_t nphi = 0;
  IRRef1 *subst;
  SnapNo onsnap;
  SnapShot *osnap, *loopsnap;
  SnapEntry *loopmap, *psentinel;
  IRRef ins, invar;

  
  invar = J->cur.nins;
  lps->sizesubst = invar - REF_BIAS;
  lps->subst = lj_mem_newvec(J->L, lps->sizesubst, IRRef1);
  subst = lps->subst - REF_BIAS;
  subst[REF_BASE] = REF_BASE;

  
  emitir_raw(IRTG(IR_LOOP, IRT_NIL), 0, 0);

  
  onsnap = J->cur.nsnap;
  lj_snap_grow_buf(J, 2*onsnap-2);
  lj_snap_grow_map(J, J->cur.nsnapmap*2+(onsnap-2)*J->cur.snap[onsnap-1].nent);

  
  loopsnap = &J->cur.snap[onsnap-1];
  loopmap = &J->cur.snapmap[loopsnap->mapofs];
  
  psentinel = &loopmap[loopsnap->nent];
  lj_assertJ(*psentinel == J->cur.snapmap[J->cur.snap[0].nent],
	     "mismatched PC for loop snapshot");
  *psentinel = SNAP(255, 0, 0);  

  
  osnap = &J->cur.snap[1];

  
  for (ins = REF_FIRST; ins < invar; ins++) {
    IRIns *ir;
    IRRef op1, op2;

    if (ins >= osnap->ref)  
      loop_subst_snap(J, osnap++, loopmap, subst);  

    
    ir = IR(ins);
    op1 = ir->op1;
    if (!irref_isk(op1)) op1 = subst[op1];
    op2 = ir->op2;
    if (!irref_isk(op2)) op2 = subst[op2];
    if (irm_kind(lj_ir_mode[ir->o]) == IRM_N &&
	op1 == ir->op1 && op2 == ir->op2) {  
      subst[ins] = (IRRef1)ins;  
    } else {
      
      IRType1 t = ir->t;  
      IRRef ref = tref_ref(emitir(ir->ot & ~IRT_ISPHI, op1, op2));
      subst[ins] = (IRRef1)ref;
      if (ref != ins) {
	IRIns *irr = IR(ref);
	if (ref < invar) {  
	  
	  if (!irref_isk(ref) && !irt_isphi(irr->t) && !irt_ispri(irr->t)) {
	    irt_setphi(irr->t);
	    if (nphi >= LJ_MAX_PHI)
	      lj_trace_err(J, LJ_TRERR_PHIOV);
	    phi[nphi++] = (IRRef1)ref;
	  }
	  
	  if (!irt_sametype(t, irr->t)) {
	    if (irt_isinteger(t) && irt_isinteger(irr->t))
	      continue;
	    else if (irt_isnum(t) && irt_isinteger(irr->t))  
	      ref = tref_ref(emitir(IRTN(IR_CONV), ref, IRCONV_NUM_INT));
	    else if (irt_isnum(irr->t) && irt_isinteger(t))  
	      ref = tref_ref(emitir(IRTGI(IR_CONV), ref,
				    IRCONV_INT_NUM|IRCONV_CHECK));
	    else
	      lj_trace_err(J, LJ_TRERR_TYPEINS);
	    subst[ins] = (IRRef1)ref;
	    irr = IR(ref);
	    goto phiconv;
	  }
	} else if (ref != REF_DROP && ref > invar &&
		   ((irr->o == IR_CONV && irr->op1 < invar) ||
		    (irr->o == IR_ALEN && irr->op2 < invar &&
					  irr->op2 != REF_NIL))) {
	  
	  ref = irr->o == IR_CONV ? irr->op1 : irr->op2;
	  irr = IR(ref);
	phiconv:
	  if (ref < invar && !irref_isk(ref) && !irt_isphi(irr->t)) {
	    irt_setphi(irr->t);
	    if (nphi >= LJ_MAX_PHI)
	      lj_trace_err(J, LJ_TRERR_PHIOV);
	    phi[nphi++] = (IRRef1)ref;
	  }
	}
      }
    }
  }
  if (!irt_isguard(J->guardemit))  
    J->cur.nsnapmap = (uint32_t)J->cur.snap[--J->cur.nsnap].mapofs;
  lj_assertJ(J->cur.nsnapmap <= J->sizesnapmap, "bad snapshot map index");
  *psentinel = J->cur.snapmap[J->cur.snap[0].nent];  

  loop_emit_phi(J, subst, phi, nphi, onsnap);
}


static void loop_undo(jit_State *J, IRRef ins, SnapNo nsnap, MSize nsnapmap)
{
  ptrdiff_t i;
  SnapShot *snap = &J->cur.snap[nsnap-1];
  SnapEntry *map = J->cur.snapmap;
  map[snap->mapofs + snap->nent] = map[J->cur.snap[0].nent];  
  J->cur.nsnapmap = (uint32_t)nsnapmap;
  J->cur.nsnap = nsnap;
  J->guardemit.irt = 0;
  lj_ir_rollback(J, ins);
  for (i = 0; i < BPROP_SLOTS; i++) {  
    BPropEntry *bp = &J->bpropcache[i];
    if (bp->val >= ins)
      bp->key = 0;
  }
  for (ins--; ins >= REF_FIRST; ins--) {  
    IRIns *ir = IR(ins);
    irt_clearphi(ir->t);
    irt_clearmark(ir->t);
  }
}


static TValue *cploop_opt(lua_State *L, lua_CFunction dummy, void *ud)
{
  UNUSED(L); UNUSED(dummy);
  loop_unroll((LoopState *)ud);
  return NULL;
}


int lj_opt_loop(jit_State *J)
{
  IRRef nins = J->cur.nins;
  SnapNo nsnap = J->cur.nsnap;
  MSize nsnapmap = J->cur.nsnapmap;
  LoopState lps;
  int errcode;
  lps.J = J;
  lps.subst = NULL;
  lps.sizesubst = 0;
  errcode = lj_vm_cpcall(J->L, NULL, &lps, cploop_opt);
  lj_mem_freevec(J2G(J), lps.subst, lps.sizesubst, IRRef1);
  if (LJ_UNLIKELY(errcode)) {
    lua_State *L = J->L;
    if (errcode == LUA_ERRRUN && tvisnumber(L->top-1)) {  
      int32_t e = numberVint(L->top-1);
      switch ((TraceError)e) {
      case LJ_TRERR_TYPEINS:  
      case LJ_TRERR_GFAIL:  
	
	if (--J->instunroll < 0)  
	  break;
	L->top--;  
	loop_undo(J, nins, nsnap, nsnapmap);
	return 1;  
      default:
	break;
      }
    }
    lj_err_throw(L, errcode);  
  }
  return 0;  
}

#undef IR
#undef emitir
#undef emitir_raw

#endif
