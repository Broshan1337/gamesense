

#define lj_opt_mem_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_tab.h"
#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_iropt.h"
#include "lj_ircall.h"
#include "lj_dispatch.h"


#define IR(ref)		(&J->cur.ir[(ref)])
#define fins		(&J->fold.ins)
#define fleft		(J->fold.left)
#define fright		(J->fold.right)




typedef enum {
  ALIAS_NO,	
  ALIAS_MAY,	
  ALIAS_MUST	
} AliasRet;




static AliasRet aa_escape(jit_State *J, IRIns *ir, IRIns *stop)
{
  IRRef ref = (IRRef)(ir - J->cur.ir);  
  for (ir++; ir < stop; ir++)
    if (ir->op2 == ref &&
	(ir->o == IR_ASTORE || ir->o == IR_HSTORE ||
	 ir->o == IR_USTORE || ir->o == IR_FSTORE))
      return ALIAS_MAY;  
  return ALIAS_NO;  
}


static AliasRet aa_table(jit_State *J, IRRef ta, IRRef tb)
{
  IRIns *taba = IR(ta), *tabb = IR(tb);
  int newa, newb;
  lj_assertJ(ta != tb, "bad usage");
  lj_assertJ(irt_istab(taba->t) && irt_istab(tabb->t), "bad usage");
  
  newa = (taba->o == IR_TNEW || taba->o == IR_TDUP);
  newb = (tabb->o == IR_TNEW || tabb->o == IR_TDUP);
  if (newa && newb)
    return ALIAS_NO;  
  if (newb) {  
    IRIns *tmp = taba; taba = tabb; tabb = tmp;
  } else if (!newa) {
    return ALIAS_MAY;  
  }
  return aa_escape(J, taba, tabb);
}


static int fwd_aa_tab_clear(jit_State *J, IRRef lim, IRRef ta)
{
  IRRef ref = J->chain[IR_CALLS];
  while (ref > lim) {
    IRIns *calls = IR(ref);
    if (calls->op2 == IRCALL_lj_tab_clear &&
	(ta == calls->op1 || aa_table(J, ta, calls->op1) != ALIAS_NO))
      return 0;  
    ref = calls->prev;
  }
  return 1;  
}


int LJ_FASTCALL lj_opt_fwd_tptr(jit_State *J, IRRef lim)
{
  IRRef ta = fins->op1;
  IRRef ref = J->chain[IR_NEWREF];
  while (ref > lim) {
    IRIns *newref = IR(ref);
    if (ta == newref->op1 || aa_table(J, ta, newref->op1) != ALIAS_NO)
      return 0;  
    ref = newref->prev;
  }
  return fwd_aa_tab_clear(J, lim, ta);
}


static AliasRet aa_ahref(jit_State *J, IRIns *refa, IRIns *refb)
{
  IRRef ka = refa->op2;
  IRRef kb = refb->op2;
  IRIns *keya, *keyb;
  IRRef ta, tb;
  if (refa == refb)
    return ALIAS_MUST;  
  keya = IR(ka);
  if (keya->o == IR_KSLOT) { ka = keya->op1; keya = IR(ka); }
  keyb = IR(kb);
  if (keyb->o == IR_KSLOT) { kb = keyb->op1; keyb = IR(kb); }
  ta = (refa->o==IR_HREFK || refa->o==IR_AREF) ? IR(refa->op1)->op1 : refa->op1;
  tb = (refb->o==IR_HREFK || refb->o==IR_AREF) ? IR(refb->op1)->op1 : refb->op1;
  if (ka == kb) {
    
    if (ta == tb)
      return ALIAS_MUST;  
    else
      return aa_table(J, ta, tb);  
  }
  if (irref_isk(ka) && irref_isk(kb))
    return ALIAS_NO;  
  if (refa->o == IR_AREF) {
    
    int32_t ofsa = 0, ofsb = 0;
    IRRef basea = ka, baseb = kb;
    lj_assertJ(refb->o == IR_AREF, "expected AREF");
    
    if (keya->o == IR_ADD && irref_isk(keya->op2)) {
      basea = keya->op1;
      ofsa = IR(keya->op2)->i;
      if (basea == kb && ofsa != 0)
	return ALIAS_NO;  
    }
    if (keyb->o == IR_ADD && irref_isk(keyb->op2)) {
      baseb = keyb->op1;
      ofsb = IR(keyb->op2)->i;
      if (ka == baseb && ofsb != 0)
	return ALIAS_NO;  
    }
    if (basea == baseb && ofsa != ofsb)
      return ALIAS_NO;  
  } else {
    
    lj_assertJ((refa->o==IR_HREF || refa->o==IR_HREFK || refa->o==IR_NEWREF) &&
	       (refb->o==IR_HREF || refb->o==IR_HREFK || refb->o==IR_NEWREF),
	       "bad xREF IR op %d or %d", refa->o, refb->o);
    if (!irt_sametype(keya->t, keyb->t))
      return ALIAS_NO;  
  }
  if (ta == tb)
    return ALIAS_MAY;  
  else
    return aa_table(J, ta, tb);  
}


static TRef fwd_ahload(jit_State *J, IRRef xref)
{
  IRIns *xr = IR(xref);
  IRRef lim = xref;  
  IRRef ref;

  
  ref = J->chain[fins->o+IRDELTA_L2S];
  while (ref > xref) {
    IRIns *store = IR(ref);
    switch (aa_ahref(J, xr, IR(store->op1))) {
    case ALIAS_NO:   break;  
    case ALIAS_MAY:  lim = ref; goto cselim;  
    case ALIAS_MUST: return store->op2;  
    }
    ref = store->prev;
  }

  
  {
    IRIns *ir = (xr->o == IR_HREFK || xr->o == IR_AREF) ? IR(xr->op1) : xr;
    IRRef tab = ir->op1;
    ir = IR(tab);
    if ((ir->o == IR_TNEW || (ir->o == IR_TDUP && irref_isk(xr->op2))) &&
	fwd_aa_tab_clear(J, tab, tab)) {
      
      if (xr->o == IR_AREF) {
	IRRef ref2 = J->chain[IR_NEWREF];
	while (ref2 > tab) {
	  IRIns *newref = IR(ref2);
	  if (irt_isnum(IR(newref->op2)->t))
	    goto cselim;
	  ref2 = newref->prev;
	}
      } else {
	IRIns *key = IR(xr->op2);
	if (key->o == IR_KSLOT) key = IR(key->op1);
	if (irt_isnum(key->t) && J->chain[IR_NEWREF] > tab)
	  goto cselim;
      }
      
      while (ref > tab) {
	IRIns *store = IR(ref);
	switch (aa_ahref(J, xr, IR(store->op1))) {
	case ALIAS_NO:   break;  
	case ALIAS_MAY:  goto cselim;  
	case ALIAS_MUST: return store->op2;  
	}
	ref = store->prev;
      }
      
      if (ir->o == IR_TNEW) {
	return TREF_NIL;
      } else {
	TValue keyv;
	cTValue *tv;
	IRIns *key = IR(xr->op2);
	if (key->o == IR_KSLOT) key = IR(key->op1);
	lj_ir_kvalue(J->L, &keyv, key);
	tv = lj_tab_get(J->L, ir_ktab(IR(ir->op1)), &keyv);
	if (tvispri(tv))
	  return TREF_PRI(itype2irt(tv));
	else if (tvisnum(tv))
	  return lj_ir_knum_u64(J, tv->u64);
	else if (tvisint(tv))
	  return lj_ir_kint(J, intV(tv));
	else if (tvistab(tv)) 
	  return TREF_NIL;
	else if (tvisstr(tv))
	  return lj_ir_kstr(J, strV(tv));
      }
      
    }
  }

cselim:
  
  ref = J->chain[fins->o];
  while (ref > lim) {
    IRIns *load = IR(ref);
    if (load->op1 == xref)
      return ref;  
    ref = load->prev;
  }
  return 0;  
}


static TRef fwd_aload_reassoc(jit_State *J)
{
  IRIns *irx = IR(fins->op1);
  IRIns *key = IR(irx->op2);
  if (key->o == IR_ADD && irref_isk(key->op2)) {
    IRIns *add2 = IR(key->op1);
    if (add2->o == IR_ADD && irref_isk(add2->op2) &&
	IR(key->op2)->i == -IR(add2->op2)->i) {
      IRRef ref = J->chain[IR_AREF];
      IRRef lim = add2->op1;
      if (irx->op1 > lim) lim = irx->op1;
      while (ref > lim) {
	IRIns *ir = IR(ref);
	if (ir->op1 == irx->op1 && ir->op2 == add2->op1)
	  return fwd_ahload(J, ref);
	ref = ir->prev;
      }
    }
  }
  return 0;
}


TRef LJ_FASTCALL lj_opt_fwd_aload(jit_State *J)
{
  IRRef ref;
  if ((ref = fwd_ahload(J, fins->op1)) ||
      (ref = fwd_aload_reassoc(J)))
    return ref;
  return EMITFOLD;
}


TRef LJ_FASTCALL lj_opt_fwd_hload(jit_State *J)
{
  IRRef ref = fwd_ahload(J, fins->op1);
  if (ref)
    return ref;
  return EMITFOLD;
}


TRef LJ_FASTCALL lj_opt_fwd_hrefk(jit_State *J)
{
  IRRef tab = fleft->op1;
  IRRef ref = J->chain[IR_NEWREF];
  while (ref > tab) {
    IRIns *newref = IR(ref);
    if (tab == newref->op1) {
      if (fright->op1 == newref->op2 && fwd_aa_tab_clear(J, ref, tab))
	return ref;  
      else
	goto docse;
    } else if (aa_table(J, tab, newref->op1) != ALIAS_NO) {
      goto docse;
    }
    ref = newref->prev;
  }
  
  if (IR(tab)->o == IR_TDUP && fwd_aa_tab_clear(J, tab, tab))
    fins->t.irt &= ~IRT_GUARD;  
docse:
  return CSEFOLD;
}


int LJ_FASTCALL lj_opt_fwd_href_nokey(jit_State *J)
{
  IRRef lim = fins->op1;  
  IRRef ref;

  
  if (irt_isnum(fright->t) && J->chain[IR_NEWREF] > lim) {
    ref = J->chain[IR_ASTORE];
    while (ref > lim) {
      if (ref < J->chain[IR_NEWREF])
	return 0;  
      ref = IR(ref)->prev;
    }
  }

  
  ref = J->chain[IR_HSTORE];
  while (ref > lim) {
    IRIns *store = IR(ref);
    if (aa_ahref(J, fins, IR(store->op1)) != ALIAS_NO)
      return 0;  
    ref = store->prev;
  }

  return 1;  
}


TRef LJ_FASTCALL lj_opt_dse_ahstore(jit_State *J)
{
  IRRef xref = fins->op1;  
  IRRef val = fins->op2;  
  IRIns *xr = IR(xref);
  IRRef1 *refp = &J->chain[fins->o];
  IRRef ref = *refp;
  while (ref > xref) {  
    IRIns *store = IR(ref);
    switch (aa_ahref(J, xr, IR(store->op1))) {
    case ALIAS_NO:
      break;  
    case ALIAS_MAY:	
      if (store->op2 != val)  
	goto doemit;
      break;  
    case ALIAS_MUST:	
      if (store->op2 == val)  
	return DROPFOLD;
      
      if (ref > J->chain[IR_LOOP]) {  
	IRIns *ir;
	
	for (ir = IR(J->cur.nins-1); ir > store; ir--)
	  if (irt_isguard(ir->t) || ir->o == IR_ALEN)
	    goto doemit;  
	
	*refp = store->prev;
	lj_ir_nop(store);
	
      }
      goto doemit;
    }
    ref = *(refp = &store->prev);
  }
doemit:
  return EMITFOLD;  
}


TRef LJ_FASTCALL lj_opt_fwd_alen(jit_State *J)
{
  IRRef tab = fins->op1;  
  IRRef lim = tab;  
  IRRef ref;
  IROp op;

  
  ref = J->chain[IR_HSTORE];
  while (ref > lim) {
    IRIns *store = IR(ref);
    IRIns *href = IR(store->op1);
    IRIns *key = IR(href->op2);
    if (irt_isnum(key->o == IR_KSLOT ? IR(key->op1)->t : key->t)) {
      lim = ref;  
      break;
    }
    ref = store->prev;
  }

  
  ref = J->chain[IR_ALEN];
  while (ref > lim) {
    
    if (IR(ref)->op1 == tab) {
      IRRef sref;

      
      if (!fwd_aa_tab_clear(J, ref, tab))
	break;

      
      sref = J->chain[IR_ASTORE];
      while (sref > ref) {
	IRIns *store = IR(sref);
	IRIns *aref = IR(store->op1);
	IRIns *fref = IR(aref->op1);
	if (tab == fref->op1) {  
	  
	  IRIns *idx = IR(aref->op2);
	  if (!irt_isnil(store->t) &&
	      idx->o == IR_ADD && idx->op1 == ref &&
	      IR(idx->op2)->o == IR_KINT && IR(idx->op2)->i == 1) {
	    
	    fins->op2 = aref->op2;  
	  }
	  goto doemit;  
	} else if (aa_table(J, tab, fref->op1) != ALIAS_NO) {
	  goto doemit;  
	}
	sref = store->prev;
      }

      return ref;  
    }
    ref = IR(ref)->prev;
  }

  
  op = IR(tab)->o;
  if (lim == tab &&
      (op == IR_TNEW || op == IR_TDUP) &&
      fwd_aa_tab_clear(J, tab, tab)) {
    
    int32_t len = 0;
    IRRef sref = J->chain[IR_ASTORE];
    while (sref > ref) {
      IRIns *store = IR(sref);
      IRIns *aref = IR(store->op1);
      IRIns *fref = IR(aref->op1);
      if (tab == fref->op1 || aa_table(J, tab, fref->op1) != ALIAS_NO) {
	goto doemit;  
      }
      sref = store->prev;
    }
    if (op == IR_TDUP) len = (int32_t)lj_tab_len(ir_ktab(IR(IR(tab)->op1)));
    return lj_ir_kint(J, len);
  }

doemit:
  return EMITFOLD;
}




static AliasRet aa_uref(IRIns *refa, IRIns *refb)
{
  if (refa->op1 == refb->op1) {  
    if (refa->op2 == refb->op2)
      return ALIAS_MUST;  
    else
      return ALIAS_NO;  
  } else {  
    if (((refa->op2 ^ refb->op2) & 0xff)) {
      return ALIAS_NO;  
    } else if (refa->o != refb->o) {
      
      if (irt_type(refa->t) == IRT_IGC) refa->t.irt += IRT_PGC-IRT_IGC;
      else if (irt_type(refb->t) == IRT_IGC) refb->t.irt += IRT_PGC-IRT_IGC;
      return ALIAS_NO;
    } else {
      
      return ALIAS_MAY;
    }
  }
}


TRef LJ_FASTCALL lj_opt_fwd_uload(jit_State *J)
{
  IRRef uref = fins->op1;
  IRRef lim = REF_BASE;  
  IRIns *xr = IR(uref);
  IRRef ref;

  
  ref = J->chain[IR_USTORE];
  while (ref > lim) {
    IRIns *store = IR(ref);
    switch (aa_uref(xr, IR(store->op1))) {
    case ALIAS_NO:   break;  
    case ALIAS_MAY:  lim = ref; goto cselim;  
    case ALIAS_MUST: return store->op2;  
    }
    ref = store->prev;
  }

cselim:
  
  ref = J->chain[IR_ULOAD];
  while (ref > lim) {
    IRIns *ir = IR(ref);
    if (ir->op1 == uref ||
	(IR(ir->op1)->op12 == IR(uref)->op12 && IR(ir->op1)->o == IR(uref)->o))
      return ref;  
    ref = ir->prev;
  }
  return lj_ir_emit(J);
}


TRef LJ_FASTCALL lj_opt_dse_ustore(jit_State *J)
{
  IRRef xref = fins->op1;  
  IRRef val = fins->op2;  
  IRIns *xr = IR(xref);
  IRRef1 *refp = &J->chain[IR_USTORE];
  IRRef ref = *refp;
  while (ref > xref) {  
    IRIns *store = IR(ref);
    switch (aa_uref(xr, IR(store->op1))) {
    case ALIAS_NO:
      break;  
    case ALIAS_MAY:	
      if (store->op2 != val)  
	goto doemit;
      break;  
    case ALIAS_MUST:	
      if (store->op2 == val)  
	return DROPFOLD;
      
      if (ref > J->chain[IR_LOOP]) {  
	IRIns *ir;
	
	for (ir = IR(J->cur.nins-1); ir > store; ir--)
	  if (irt_isguard(ir->t))
	    goto doemit;  
	
	*refp = store->prev;
	lj_ir_nop(store);
	if (ref+1 < J->cur.nins &&
	    store[1].o == IR_OBAR && store[1].op1 == xref) {
	  IRRef1 *bp = &J->chain[IR_OBAR];
	  IRIns *obar;
	  for (obar = IR(*bp); *bp > ref+1; obar = IR(*bp))
	    bp = &obar->prev;
	  
	  *bp = obar->prev;
	  lj_ir_nop(obar);
	}
	
      }
      goto doemit;
    }
    ref = *(refp = &store->prev);
  }
doemit:
  return EMITFOLD;  
}




static AliasRet aa_fref(jit_State *J, IRIns *refa, IRIns *refb)
{
  if (refa->op2 != refb->op2)
    return ALIAS_NO;  
  if (refa->op1 == refb->op1)
    return ALIAS_MUST;  
  else if (refa->op2 >= IRFL_TAB_META && refa->op2 <= IRFL_TAB_NOMM)
    return aa_table(J, refa->op1, refb->op1);  
  else
    return ALIAS_MAY;  
}


TRef LJ_FASTCALL lj_opt_fwd_fload(jit_State *J)
{
  IRRef oref = fins->op1;  
  IRRef fid = fins->op2;  
  IRRef lim = oref;  
  IRRef ref;

  
  ref = J->chain[IR_FSTORE];
  while (ref > oref) {
    IRIns *store = IR(ref);
    switch (aa_fref(J, fins, IR(store->op1))) {
    case ALIAS_NO:   break;  
    case ALIAS_MAY:  lim = ref; goto cselim;  
    case ALIAS_MUST: return store->op2;  
    }
    ref = store->prev;
  }

  
  if (fid == IRFL_TAB_META) {
    IRIns *ir = IR(oref);
    if (ir->o == IR_TNEW || ir->o == IR_TDUP)
      return lj_ir_knull(J, IRT_TAB);
  }

cselim:
  
  return lj_opt_cselim(J, lim);
}


TRef LJ_FASTCALL lj_opt_dse_fstore(jit_State *J)
{
  IRRef fref = fins->op1;  
  IRRef val = fins->op2;  
  IRIns *xr = IR(fref);
  IRRef1 *refp = &J->chain[IR_FSTORE];
  IRRef ref = *refp;
  while (ref > fref) {  
    IRIns *store = IR(ref);
    switch (aa_fref(J, xr, IR(store->op1))) {
    case ALIAS_NO:
      break;  
    case ALIAS_MAY:
      if (store->op2 != val)  
	goto doemit;
      break;  
    case ALIAS_MUST:
      if (store->op2 == val &&
	  !(xr->op2 >= IRFL_SBUF_W && xr->op2 <= IRFL_SBUF_R))
	return DROPFOLD;  
      
      if (ref > J->chain[IR_LOOP]) {  
	IRIns *ir;
	
	for (ir = IR(J->cur.nins-1); ir > store; ir--)
	  if (irt_isguard(ir->t) || (ir->o == IR_FLOAD && ir->op2 == xr->op2))
	    goto doemit;  
	
	*refp = store->prev;
	lj_ir_nop(store);
	
      }
      goto doemit;
    }
    ref = *(refp = &store->prev);
  }
doemit:
  return EMITFOLD;  
}


int LJ_FASTCALL lj_opt_fwd_sbuf(jit_State *J, IRRef lim)
{
  IRRef ref;
  if (J->chain[IR_BUFPUT] > lim)
    return 0;  
  ref = J->chain[IR_CALLS];
  while (ref > lim) {
    IRIns *ir = IR(ref);
    if (ir->op2 >= IRCALL_lj_strfmt_putint && ir->op2 < IRCALL_lj_buf_tostr)
      return 0;  
    ref = ir->prev;
  }
  ref = J->chain[IR_CALLL];
  while (ref > lim) {
    IRIns *ir = IR(ref);
    if (ir->op2 >= IRCALL_lj_strfmt_putint && ir->op2 < IRCALL_lj_buf_tostr)
      return 0;  
    ref = ir->prev;
  }
  return 1;  
}




static IRIns *aa_findcnew(jit_State *J, IRIns *ir)
{
  while (ir->o == IR_ADD) {
    if (!irref_isk(ir->op1)) {
      IRIns *ir1 = aa_findcnew(J, IR(ir->op1));  
      if (ir1) return ir1;
    }
    if (irref_isk(ir->op2)) return NULL;
    ir = IR(ir->op2);  
  }
  return ir->o == IR_CNEW ? ir : NULL;
}


static AliasRet aa_cnew(jit_State *J, IRIns *refa, IRIns *refb)
{
  IRIns *cnewa = aa_findcnew(J, refa);
  IRIns *cnewb = aa_findcnew(J, refb);
  if (cnewa == cnewb)
    return ALIAS_MAY;  
  if (cnewa && cnewb)
    return ALIAS_NO;  
  if (cnewb) { cnewa = cnewb; refb = refa; }
  return aa_escape(J, cnewa, refb);
}


static AliasRet aa_xref(jit_State *J, IRIns *refa, IRIns *xa, IRIns *xb)
{
  ptrdiff_t ofsa = 0, ofsb = 0;
  IRIns *refb = IR(xb->op1);
  IRIns *basea = refa, *baseb = refb;
  if (refa == refb && irt_sametype(xa->t, xb->t))
    return ALIAS_MUST;  
  
  if (refa->o == IR_ADD && irref_isk(refa->op2)) {
    IRIns *irk = IR(refa->op2);
    basea = IR(refa->op1);
    ofsa = (LJ_64 && irk->o == IR_KINT64) ? (ptrdiff_t)ir_k64(irk)->u64 :
					    (ptrdiff_t)irk->i;
  }
  if (refb->o == IR_ADD && irref_isk(refb->op2)) {
    IRIns *irk = IR(refb->op2);
    baseb = IR(refb->op1);
    ofsb = (LJ_64 && irk->o == IR_KINT64) ? (ptrdiff_t)ir_k64(irk)->u64 :
					    (ptrdiff_t)irk->i;
  }
  
  if (basea->o == IR_KPTR && baseb->o == IR_KPTR) {
    ofsb += (char *)ir_kptr(baseb) - (char *)ir_kptr(basea);
    baseb = basea;
  }
  
  if (basea == baseb) {
    ptrdiff_t sza = irt_size(xa->t), szb = irt_size(xb->t);
    if (ofsa == ofsb) {
      if (sza == szb && irt_isfp(xa->t) == irt_isfp(xb->t))
	return ALIAS_MUST;  
    } else if (ofsa + sza <= ofsb || ofsb + szb <= ofsa) {
      return ALIAS_NO;  
    }
    
    return ALIAS_MAY;  
  }
  if (!irt_sametype(xa->t, xb->t) &&
      !(irt_typerange(xa->t, IRT_I8, IRT_U64) &&
	((xa->t.irt - IRT_I8) ^ (xb->t.irt - IRT_I8)) == 1))
    return ALIAS_NO;
  
  return aa_cnew(J, basea, baseb);  
}


static IRRef reassoc_trycse(jit_State *J, IROp op, IRRef op1, IRRef op2)
{
  IRRef ref = J->chain[op];
  IRRef lim = op1;
  if (op2 > lim) { lim = op2; op2 = op1; op1 = lim; }
  while (ref > lim) {
    IRIns *ir = IR(ref);
    if (ir->op1 == op1 && ir->op2 == op2)
      return ref;
    ref = ir->prev;
  }
  return 0;
}


static IRRef reassoc_xref(jit_State *J, IRIns *ir)
{
  ptrdiff_t ofs = 0;
  if (ir->o == IR_ADD && irref_isk(ir->op2)) {  
    IRIns *irk = IR(ir->op2);
    ofs = (LJ_64 && irk->o == IR_KINT64) ? (ptrdiff_t)ir_k64(irk)->u64 :
					   (ptrdiff_t)irk->i;
    ir = IR(ir->op1);
  }
  if (ir->o == IR_ADD) {  
    
    IRIns *ir2, *ir1 = IR(ir->op1);
    int32_t shift = 0;
    IRRef idxref;
    
    if (ir1->o == IR_BSHL && irref_isk(ir1->op2))
      shift = IR(ir1->op2)->i;
    else if (ir1->o == IR_ADD && ir1->op1 == ir1->op2)
      shift = 1;
    else
      ir1 = ir;
    ir2 = IR(ir1->op1);
    
    if (ir2->o == IR_ADD && irt_isint(ir2->t) && irref_isk(ir2->op2))
      ofs += (ptrdiff_t)IR(ir2->op2)->i << shift;
    else
      return 0;
    idxref = ir2->op1;
    
    if (ir1 != ir &&
	!(idxref = reassoc_trycse(J, ir1->o, idxref,
				  ir1->o == IR_BSHL ? ir1->op2 : idxref)))
      return 0;
    if (!(idxref = reassoc_trycse(J, IR_ADD, idxref, ir->op2)))
      return 0;
    if (ofs != 0) {
      IRRef refk = tref_ref(lj_ir_kintp(J, ofs));
      if (!(idxref = reassoc_trycse(J, IR_ADD, idxref, refk)))
	return 0;
    }
    return idxref;  
  }
  return 0;  
}


TRef LJ_FASTCALL lj_opt_fwd_xload(jit_State *J)
{
  IRRef xref = fins->op1;
  IRIns *xr = IR(xref);
  IRRef lim = xref;  
  IRRef ref;

  if ((fins->op2 & IRXLOAD_READONLY))
    goto cselim;
  if ((fins->op2 & IRXLOAD_VOLATILE))
    goto doemit;

  
  ref = J->chain[IR_XSTORE];
retry:
  if (J->chain[IR_CALLXS] > lim) lim = J->chain[IR_CALLXS];
  if (J->chain[IR_XBAR] > lim) lim = J->chain[IR_XBAR];
  while (ref > lim) {
    IRIns *store = IR(ref);
    switch (aa_xref(J, xr, fins, store)) {
    case ALIAS_NO:   break;  
    case ALIAS_MAY:  lim = ref; goto cselim;  
    case ALIAS_MUST:
      
      if (!irt_sametype(fins->t, IR(store->op2)->t)) {
	IRType dt = irt_type(fins->t), st = irt_type(IR(store->op2)->t);
	if (dt == IRT_I8 || dt == IRT_I16) {  
	  st = dt | IRCONV_SEXT;
	  dt = IRT_INT;
	} else if (dt == IRT_U8 || dt == IRT_U16) {  
	  st = dt;
	  dt = IRT_INT;
	}
	fins->ot = IRT(IR_CONV, dt);
	fins->op1 = store->op2;
	fins->op2 = (dt<<5)|st;
	return RETRYFOLD;
      }
      return store->op2;  
    }
    ref = store->prev;
  }

cselim:
  
  ref = J->chain[IR_XLOAD];
  while (ref > lim) {
    
    if (IR(ref)->op1 == xref && irt_sametype(IR(ref)->t, fins->t))
      return ref;
    ref = IR(ref)->prev;
  }

  
  if (!(fins->op2 & IRXLOAD_READONLY) && J->chain[IR_LOOP] &&
      xref == fins->op1 && (xref = reassoc_xref(J, xr)) != 0) {
    ref = J->chain[IR_XSTORE];
    while (ref > lim)  
      ref = IR(ref)->prev;
    lim = xref;
    xr = IR(xref);
    goto retry;  
  }
doemit:
  return EMITFOLD;
}


TRef LJ_FASTCALL lj_opt_dse_xstore(jit_State *J)
{
  IRRef xref = fins->op1;
  IRIns *xr = IR(xref);
  IRRef lim = xref;  
  IRRef val = fins->op2;  
  IRRef1 *refp = &J->chain[IR_XSTORE];
  IRRef ref = *refp;
  if (J->chain[IR_CALLXS] > lim) lim = J->chain[IR_CALLXS];
  if (J->chain[IR_XBAR] > lim) lim = J->chain[IR_XBAR];
  if (J->chain[IR_XSNEW] > lim) lim = J->chain[IR_XSNEW];
  while (ref > lim) {  
    IRIns *store = IR(ref);
    switch (aa_xref(J, xr, fins, store)) {
    case ALIAS_NO:
      break;  
    case ALIAS_MAY:
      if (store->op2 != val)  
	goto doemit;
      break;  
    case ALIAS_MUST:
      if (store->op2 == val)  
	return DROPFOLD;
      
      if (ref > J->chain[IR_LOOP]) {  
	IRIns *ir;
	
	for (ir = IR(J->cur.nins-1); ir > store; ir--)
	  if (irt_isguard(ir->t) || ir->o == IR_XLOAD)
	    goto doemit;  
	
	*refp = store->prev;
	lj_ir_nop(store);
	
      }
      goto doemit;
    }
    ref = *(refp = &store->prev);
  }
doemit:
  return EMITFOLD;  
}




int lj_opt_fwd_wasnonnil(jit_State *J, IROpT loadop, IRRef xref)
{
  
  IRRef ref = J->chain[loadop+IRDELTA_L2S];
  while (ref > xref) {
    IRIns *store = IR(ref);
    if (store->op1 == xref) {  
      
      return !irt_isnil(store->t);
    } else if (irt_isnil(store->t)) {  
      IRRef skref = IR(store->op1)->op2;
      IRRef xkref = IR(xref)->op2;
      
      if (loadop == IR_ALOAD || irt_sametype(IR(skref)->t, IR(xkref)->t)) {
	if (skref == xkref || !irref_isk(skref) || !irref_isk(xkref))
	  return 0;  
	
      } else if (irt_isp32(IR(skref)->t) != irt_isp32(IR(xkref)->t)) {
	return 0;  
      }  
    }  
    ref = store->prev;
  }

  
  ref = J->chain[loadop];
  while (ref > xref) {
    IRIns *load = IR(ref);
    if (load->op1 == xref) {  
      
      return !irt_isnil(load->t);
    }  
    ref = load->prev;
  }
  return 0;  
}



#undef IR
#undef fins
#undef fleft
#undef fright

#endif
