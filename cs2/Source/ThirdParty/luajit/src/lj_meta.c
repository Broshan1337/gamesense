

#define lj_meta_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_str.h"
#include "lj_tab.h"
#include "lj_meta.h"
#include "lj_frame.h"
#include "lj_bc.h"
#include "lj_vm.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"
#include "lj_lib.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_cdata.h"
#include "lj_carith.h"
#endif




void lj_meta_init(lua_State *L)
{
#define MMNAME(name)	"__" #name
  const char *metanames = MMDEF(MMNAME);
#undef MMNAME
  global_State *g = G(L);
  const char *p, *q;
  uint32_t mm;
  for (mm = 0, p = metanames; *p; mm++, p = q) {
    GCstr *s;
    for (q = p+2; *q && *q != '_'; q++) ;
    s = lj_str_new(L, p, (size_t)(q-p));
    
    setgcref(g->gcroot[GCROOT_MMNAME+mm], obj2gco(s));
  }
}


cTValue *lj_meta_cache(GCtab *mt, MMS mm, GCstr *name)
{
  cTValue *mo = lj_tab_getstr(mt, name);
  lj_assertX(mm <= MM_FAST, "bad metamethod %d", mm);
  if (!mo || tvisnil(mo)) {  
    mt->nomm |= (uint8_t)(1u<<mm);  
    return NULL;
  }
  return mo;
}


cTValue *lj_meta_lookup(lua_State *L, cTValue *o, MMS mm)
{
  GCtab *mt;
  if (tvistab(o))
    mt = tabref(tabV(o)->metatable);
  else if (tvisudata(o))
    mt = tabref(udataV(o)->metatable);
  else
    mt = tabref(basemt_obj(G(L), o));
  if (mt) {
    cTValue *mo = lj_tab_getstr(mt, mmname_str(G(L), mm));
    if (mo)
      return mo;
  }
  return niltv(L);
}

#if LJ_HASFFI

int lj_meta_tailcall(lua_State *L, cTValue *tv)
{
  TValue *base = L->base;
  TValue *top = L->top;
  const BCIns *pc = frame_pc(base-1);  
  copyTV(L, base-1-LJ_FR2, tv);  
  if (LJ_FR2)
    (top++)->u64 = LJ_CONT_TAILCALL;
  else
    top->u32.lo = LJ_CONT_TAILCALL;
  setframe_pc(top++, pc);
  setframe_gc(top, obj2gco(L), LJ_TTHREAD);  
  if (LJ_FR2) top++;
  setframe_ftsz(top, ((char *)(top+1) - (char *)base) + FRAME_CONT);
  L->base = L->top = top+1;
  
  return 0;
}
#endif


static TValue *mmcall(lua_State *L, ASMFunction cont, cTValue *mo,
		    cTValue *a, cTValue *b)
{
  
  TValue *top = L->top;
  if (curr_funcisL(L)) top = curr_topL(L);
  setcont(top++, cont);  
  if (LJ_FR2) setnilV(top++);
  copyTV(L, top++, mo);  
  if (LJ_FR2) setnilV(top++);
  copyTV(L, top, a);
  copyTV(L, top+1, b);
  return top;  
}




cTValue *lj_meta_tget(lua_State *L, cTValue *o, cTValue *k)
{
  int loop;
  for (loop = 0; loop < LJ_MAX_IDXCHAIN; loop++) {
    cTValue *mo;
    if (LJ_LIKELY(tvistab(o))) {
      GCtab *t = tabV(o);
      cTValue *tv = lj_tab_get(L, t, k);
      if (!tvisnil(tv) ||
	  !(mo = lj_meta_fast(L, tabref(t->metatable), MM_index)))
	return tv;
    } else if (tvisnil(mo = lj_meta_lookup(L, o, MM_index))) {
      lj_err_optype(L, o, LJ_ERR_OPINDEX);
      return NULL;  
    }
    if (tvisfunc(mo)) {
      L->top = mmcall(L, lj_cont_ra, mo, o, k);
      return NULL;  
    }
    o = mo;
  }
  lj_err_msg(L, LJ_ERR_GETLOOP);
  return NULL;  
}


TValue *lj_meta_tset(lua_State *L, cTValue *o, cTValue *k)
{
  TValue tmp;
  int loop;
  for (loop = 0; loop < LJ_MAX_IDXCHAIN; loop++) {
    cTValue *mo;
    if (LJ_LIKELY(tvistab(o))) {
      GCtab *t = tabV(o);
      cTValue *tv = lj_tab_get(L, t, k);
      if (LJ_LIKELY(!tvisnil(tv))) {
	t->nomm = 0;  
	lj_gc_anybarriert(L, t);
	return (TValue *)tv;
      } else if (!(mo = lj_meta_fast(L, tabref(t->metatable), MM_newindex))) {
	t->nomm = 0;  
	lj_gc_anybarriert(L, t);
	if (tv != niltv(L))
	  return (TValue *)tv;
	if (tvisnil(k)) lj_err_msg(L, LJ_ERR_NILIDX);
	else if (tvisint(k)) { setnumV(&tmp, (lua_Number)intV(k)); k = &tmp; }
	else if (tvisnum(k) && tvisnan(k)) lj_err_msg(L, LJ_ERR_NANIDX);
	return lj_tab_newkey(L, t, k);
      }
    } else if (tvisnil(mo = lj_meta_lookup(L, o, MM_newindex))) {
      lj_err_optype(L, o, LJ_ERR_OPINDEX);
      return NULL;  
    }
    if (tvisfunc(mo)) {
      L->top = mmcall(L, lj_cont_nop, mo, o, k);
      
      return NULL;  
    }
    copyTV(L, &tmp, mo);
    o = &tmp;
  }
  lj_err_msg(L, LJ_ERR_SETLOOP);
  return NULL;  
}

static cTValue *str2num(cTValue *o, TValue *n)
{
  if (tvisnum(o))
    return o;
  else if (tvisint(o))
    return (setnumV(n, (lua_Number)intV(o)), n);
  else if (tvisstr(o) && lj_strscan_num(strV(o), n))
    return n;
  else
    return NULL;
}


TValue *lj_meta_arith(lua_State *L, TValue *ra, cTValue *rb, cTValue *rc,
		      BCReg op)
{
  MMS mm = bcmode_mm(op);
  TValue tempb, tempc;
  cTValue *b, *c;
  if ((b = str2num(rb, &tempb)) != NULL &&
      (c = str2num(rc, &tempc)) != NULL) {  
    setnumV(ra, lj_vm_foldarith(numV(b), numV(c), (int)mm-MM_add));
    return NULL;
  } else {
    cTValue *mo = lj_meta_lookup(L, rb, mm);
    if (tvisnil(mo)) {
      mo = lj_meta_lookup(L, rc, mm);
      if (tvisnil(mo)) {
	if (str2num(rb, &tempb) == NULL) rc = rb;
	lj_err_optype(L, rc, LJ_ERR_OPARITH);
	return NULL;  
      }
    }
    return mmcall(L, lj_cont_ra, mo, rb, rc);
  }
}


void lj_meta_bitop(lua_State *L, TValue *ra, cTValue *rb, cTValue *rc, BCReg op)
{
#if LJ_HASFFI
  CTypeID id = 0, id_ignore = 0;
  uint64_t b = lj_carith_checkbit64(L, rb, &id);
  uint64_t c = lj_carith_checkbit64(L, rc, op >= BC_BSHL ? &id_ignore : &id);
  if (id) {
    if (tvisnum(rb)) {
      b = id == CTID_UINT64 ? lj_num2u64(numV(rb)) : (uint64_t)lj_num2i64(numV(rb));
    }
    if (tvisnum(rc)) {
      c = id == CTID_UINT64 ? lj_num2u64(numV(rc)) : (uint64_t)lj_num2i64(numV(rc));
    }
  }
  switch (op) {
  case BC_BNOT: b = ~b; break;
  case BC_BAND: b &= c; break;
  case BC_BOR: b |= c; break;
  case BC_BXOR: b ^= c; break;
  default:
    if (id) {
      b = lj_carith_shift64(b, (int32_t)c, op-BC_BSHL);
    } else if (op == BC_BSHL) {
      b = (uint64_t)((uint32_t)b << ((uint32_t)c & 31));
    } else if (op == BC_BSHR) {
      b = (uint64_t)((uint32_t)b >> ((uint32_t)c & 31));
    } else {
      lj_assertL(op == BC_BSAR, "bad bytecode op %d", op);
      b = (uint64_t)(uint32_t)((int32_t)b >> ((uint32_t)c & 31));
    }
    break;
  }
  if (id) {
    GCcdata *cd = lj_cdata_new_(L, id, 8);
    *(uint64_t *)cdataptr(cd) = b;
    setcdataV(L, ra, cd);
  } else {
    setintV(ra, (int32_t)b);
  }
#else
#if LJ_DUALNUM
  uint32_t b = 0, c = 0;
  if (tvisint(rb)) b = (uint32_t)intV(rb);
  else if (tvisnum(rb)) b = (uint32_t)lj_num2bit(numV(rb));
  else goto err;
  if (tvisint(rc)) c = (uint32_t)intV(rc);
  else if (tvisnum(rc)) c = (uint32_t)lj_num2bit(numV(rc));
  else goto err;
  switch (op) {
  case BC_BNOT: b = ~b; break;
  case BC_BAND: b &= c; break;
  case BC_BOR: b |= c; break;
  case BC_BXOR: b ^= c; break;
  case BC_BSHL: b <<= (c & 31); break;
  case BC_BSHR: b >>= (c & 31); break;
  case BC_BSAR: b = (uint32_t)((int32_t)b >> (c & 31)); break;
  default:
    lj_assertL(0, "bad bytecode op %d", op);
    break;
  }
  setintV(ra, (int32_t)b);
  return;
err:
#endif
  UNUSED(ra); UNUSED(op);
  lj_err_optype(L, tvisnumber(rb) ? rc : rb, LJ_ERR_OPARITH);
#endif
}


TValue *lj_meta_cat(lua_State *L, TValue *top, int left)
{
  int fromc = 0;
  if (left < 0) { left = -left; fromc = 1; }
  do {
    if (!(tvisstr(top) || tvisnumber(top) || tvisbuf(top)) ||
	!(tvisstr(top-1) || tvisnumber(top-1) || tvisbuf(top-1))) {
      cTValue *mo = lj_meta_lookup(L, top-1, MM_concat);
      if (tvisnil(mo)) {
	mo = lj_meta_lookup(L, top, MM_concat);
	if (tvisnil(mo)) {
	  if (tvisstr(top-1) || tvisnumber(top-1)) top++;
	  lj_err_optype(L, top-1, LJ_ERR_OPCAT);
	  return NULL;  
	}
      }
      
      copyTV(L, top+2*LJ_FR2+2, top);  
      copyTV(L, top+2*LJ_FR2+1, top-1);
      copyTV(L, top+LJ_FR2, mo);
      setcont(top-1, lj_cont_cat);
      if (LJ_FR2) { setnilV(top); setnilV(top+2); top += 2; }
      return top+1;  
    } else {
      
      TValue *e, *o = top;
      uint64_t tlen = tvisstr(o) ? strV(o)->len :
		      tvisbuf(o) ? sbufxlen(bufV(o)) : STRFMT_MAXBUF_NUM;
      SBuf *sb;
      do {
	o--; tlen += tvisstr(o) ? strV(o)->len :
		     tvisbuf(o) ? sbufxlen(bufV(o)) : STRFMT_MAXBUF_NUM;
      } while (--left > 0 && (tvisstr(o-1) || tvisnumber(o-1)));
      if (tlen >= LJ_MAX_STR) lj_err_msg(L, LJ_ERR_STROV);
      sb = lj_buf_tmp_(L);
      lj_buf_more(sb, (MSize)tlen);
      for (e = top, top = o; o <= e; o++) {
	if (tvisstr(o)) {
	  GCstr *s = strV(o);
	  MSize len = s->len;
	  lj_buf_putmem(sb, strdata(s), len);
	} else if (tvisbuf(o)) {
	  SBufExt *sbx = bufV(o);
	  lj_buf_putmem(sb, sbx->r, sbufxlen(sbx));
	} else if (tvisint(o)) {
	  lj_strfmt_putint(sb, intV(o));
	} else {
	  lj_strfmt_putfnum(sb, STRFMT_G14, numV(o));
	}
      }
      setstrV(L, top, lj_buf_str(L, sb));
    }
  } while (left >= 1);
  if (LJ_UNLIKELY(G(L)->gc.total >= G(L)->gc.threshold)) {
    if (!fromc) L->top = curr_topL(L);
    lj_gc_step(L);
  }
  return NULL;
}


TValue * LJ_FASTCALL lj_meta_len(lua_State *L, cTValue *o)
{
  cTValue *mo = lj_meta_lookup(L, o, MM_len);
  if (tvisnil(mo)) {
    if (LJ_52 && tvistab(o))
      tabref(tabV(o)->metatable)->nomm |= (uint8_t)(1u<<MM_len);
    else
      lj_err_optype(L, o, LJ_ERR_OPLEN);
    return NULL;
  }
  return mmcall(L, lj_cont_ra, mo, o, LJ_52 ? o : niltv(L));
}


TValue *lj_meta_equal(lua_State *L, GCobj *o1, GCobj *o2, int ne)
{
  
  cTValue *mo = lj_meta_fast(L, tabref(o1->gch.metatable), MM_eq);
  if (mo) {
    TValue *top;
    uint32_t it;
    if (tabref(o1->gch.metatable) != tabref(o2->gch.metatable)) {
      cTValue *mo2 = lj_meta_fast(L, tabref(o2->gch.metatable), MM_eq);
      if (mo2 == NULL || !lj_obj_equal(mo, mo2))
	return (TValue *)(intptr_t)ne;
    }
    top = curr_top(L);
    setcont(top++, ne ? lj_cont_condf : lj_cont_condt);
    if (LJ_FR2) setnilV(top++);
    copyTV(L, top++, mo);
    if (LJ_FR2) setnilV(top++);
    it = ~(uint32_t)o1->gch.gct;
    setgcV(L, top, o1, it);
    setgcV(L, top+1, o2, it);
    return top;  
  }
  return (TValue *)(intptr_t)ne;
}

#if LJ_HASFFI
TValue * LJ_FASTCALL lj_meta_equal_cd(lua_State *L, BCIns ins)
{
  ASMFunction cont = (bc_op(ins) & 1) ? lj_cont_condf : lj_cont_condt;
  int op = (int)bc_op(ins) & ~1;
  TValue tv;
  cTValue *mo, *o2, *o1 = &L->base[bc_a(ins)];
  cTValue *o1mm = o1;
  if (op == BC_ISEQV) {
    o2 = &L->base[bc_d(ins)];
    if (!tviscdata(o1mm)) o1mm = o2;
  } else if (op == BC_ISEQS) {
    setstrV(L, &tv, gco2str(proto_kgc(curr_proto(L), ~(ptrdiff_t)bc_d(ins))));
    o2 = &tv;
  } else if (op == BC_ISEQN) {
    o2 = &mref(curr_proto(L)->k, cTValue)[bc_d(ins)];
  } else {
    lj_assertL(op == BC_ISEQP, "bad bytecode op %d", op);
    setpriV(&tv, ~bc_d(ins));
    o2 = &tv;
  }
  mo = lj_meta_lookup(L, o1mm, MM_eq);
  if (LJ_LIKELY(!tvisnil(mo)))
    return mmcall(L, cont, mo, o1, o2);
  else
    return (TValue *)(intptr_t)(bc_op(ins) & 1);
}
#endif


TValue *lj_meta_comp(lua_State *L, cTValue *o1, cTValue *o2, int op)
{
  if (LJ_HASFFI && (tviscdata(o1) || tviscdata(o2))) {
    ASMFunction cont = (op & 1) ? lj_cont_condf : lj_cont_condt;
    MMS mm = (op & 2) ? MM_le : MM_lt;
    cTValue *mo = lj_meta_lookup(L, tviscdata(o1) ? o1 : o2, mm);
    if (LJ_UNLIKELY(tvisnil(mo))) goto err;
    return mmcall(L, cont, mo, o1, o2);
  } else if (LJ_52 || itype(o1) == itype(o2)) {
    
    if (tvisstr(o1) && tvisstr(o2)) {
      int32_t res = lj_str_cmp(strV(o1), strV(o2));
      return (TValue *)(intptr_t)(((op&2) ? res <= 0 : res < 0) ^ (op&1));
    } else {
    trymt:
      while (1) {
	ASMFunction cont = (op & 1) ? lj_cont_condf : lj_cont_condt;
	MMS mm = (op & 2) ? MM_le : MM_lt;
	cTValue *mo = lj_meta_lookup(L, o1, mm);
#if LJ_52
	if (tvisnil(mo) && tvisnil((mo = lj_meta_lookup(L, o2, mm))))
#else
	cTValue *mo2 = lj_meta_lookup(L, o2, mm);
	if (tvisnil(mo) || !lj_obj_equal(mo, mo2))
#endif
	{
	  if (op & 2) {  
	    cTValue *ot = o1; o1 = o2; o2 = ot;  
	    op ^= 3;  
	    continue;
	  }
	  goto err;
	}
	return mmcall(L, cont, mo, o1, o2);
      }
    }
  } else if (tvisbool(o1) && tvisbool(o2)) {
    goto trymt;
  } else {
  err:
    lj_err_comp(L, o1, o2);
    return NULL;
  }
}


void lj_meta_istype(lua_State *L, BCReg ra, BCReg tp)
{
  L->top = curr_topL(L);
  ra++; tp--;
  lj_assertL(LJ_DUALNUM || tp != ~LJ_TNUMX, "bad type for ISTYPE");
  if (LJ_DUALNUM && tp == ~LJ_TNUMX) lj_lib_checkint(L, ra);
  else if (tp == ~LJ_TNUMX+1) lj_lib_checknum(L, ra);
  else if (tp == ~LJ_TSTR) lj_lib_checkstr(L, ra);
  else lj_err_argtype(L, ra, lj_obj_itypename[tp]);
}


void lj_meta_call(lua_State *L, TValue *func, TValue *top)
{
  cTValue *mo = lj_meta_lookup(L, func, MM_call);
  TValue *p;
  if (!tvisfunc(mo))
    lj_err_optype_call(L, func);
  for (p = top; p > func+2*LJ_FR2; p--) copyTV(L, p, p-1);
  if (LJ_FR2) copyTV(L, func+2, func);
  copyTV(L, func, mo);
}


void LJ_FASTCALL lj_meta_for(lua_State *L, TValue *o)
{
  if (!lj_strscan_numberobj(o)) lj_err_msg(L, LJ_ERR_FORINIT);
  if (!lj_strscan_numberobj(o+1)) lj_err_msg(L, LJ_ERR_FORLIM);
  if (!lj_strscan_numberobj(o+2)) lj_err_msg(L, LJ_ERR_FORSTEP);
  if (LJ_DUALNUM) {
    
    int32_t k[3];
    int nint = 0;
    ptrdiff_t i;
    for (i = 0; i <= 2; i++) {
      if (tvisint(o+i)) {
	k[i] = intV(o+i); nint++;
      } else {
	int64_t i64;
	if (lj_num2int_check(numV(o+i), i64, k[i])) nint++;
      }
    }
    if (nint == 3) {  
      setintV(o, k[0]);
      setintV(o+1, k[1]);
      setintV(o+2, k[2]);
    } else if (nint != 0) {  
      if (tvisint(o)) setnumV(o, (lua_Number)intV(o));
      if (tvisint(o+1)) setnumV(o+1, (lua_Number)intV(o+1));
      if (tvisint(o+2)) setnumV(o+2, (lua_Number)intV(o+2));
    }
  }
}

