

#include "lj_obj.h"

#if LJ_HASFFI

#include "lj_gc.h"
#include "lj_err.h"
#include "lj_tab.h"
#include "lj_ctype.h"
#include "lj_cconv.h"
#include "lj_cdata.h"




GCcdata *lj_cdata_newref(CTState *cts, const void *p, CTypeID id)
{
  CTypeID refid = lj_ctype_intern(cts, CTINFO_REF(id), CTSIZE_PTR);
  GCcdata *cd = lj_cdata_new(cts, refid, CTSIZE_PTR);
  *(const void **)cdataptr(cd) = p;
  return cd;
}


GCcdata *lj_cdata_newv(lua_State *L, CTypeID id, CTSize sz, CTSize align)
{
  global_State *g;
  MSize extra = sizeof(GCcdataVar) + sizeof(GCcdata) +
		(align > CT_MEMALIGN ? (1u<<align) - (1u<<CT_MEMALIGN) : 0);
  char *p = lj_mem_newt(L, extra + sz, char);
  uintptr_t adata = (uintptr_t)p + sizeof(GCcdataVar) + sizeof(GCcdata);
  uintptr_t almask = (1u << align) - 1u;
  GCcdata *cd = (GCcdata *)(((adata + almask) & ~almask) - sizeof(GCcdata));
  lj_assertL((char *)cd - p < 65536, "excessive cdata alignment");
  cdatav(cd)->offset = (uint16_t)((char *)cd - p);
  cdatav(cd)->extra = extra;
  cdatav(cd)->len = sz;
  g = G(L);
  setgcrefr(cd->nextgc, g->gc.root);
  setgcref(g->gc.root, obj2gco(cd));
  newwhite(g, obj2gco(cd));
  cd->marked |= 0x80;
  cd->gct = ~LJ_TCDATA;
  cd->ctypeid = id;
  return cd;
}


GCcdata *lj_cdata_newx(CTState *cts, CTypeID id, CTSize sz, CTInfo info)
{
  if (!(info & CTF_VLA) && ctype_align(info) <= CT_MEMALIGN)
    return lj_cdata_new(cts, id, sz);
  else
    return lj_cdata_newv(cts->L, id, sz, ctype_align(info));
}


void LJ_FASTCALL lj_cdata_free(global_State *g, GCcdata *cd)
{
  if (LJ_UNLIKELY(cd->marked & LJ_GC_CDATA_FIN)) {
    GCobj *root;
    makewhite(g, obj2gco(cd));
    markfinalized(obj2gco(cd));
    if ((root = gcref(g->gc.mmudata)) != NULL) {
      setgcrefr(cd->nextgc, root->gch.nextgc);
      setgcref(root->gch.nextgc, obj2gco(cd));
      setgcref(g->gc.mmudata, obj2gco(cd));
    } else {
      setgcref(cd->nextgc, obj2gco(cd));
      setgcref(g->gc.mmudata, obj2gco(cd));
    }
  } else if (LJ_LIKELY(!cdataisv(cd))) {
    CType *ct = ctype_raw(ctype_ctsG(g), cd->ctypeid);
    CTSize sz = ctype_hassize(ct->info) ? ct->size : CTSIZE_PTR;
    lj_assertG(ctype_hassize(ct->info) || ctype_isfunc(ct->info) ||
	       ctype_isextern(ct->info), "free of ctype without a size");
    lj_mem_free(g, cd, sizeof(GCcdata) + sz);
  } else {
    lj_mem_free(g, memcdatav(cd), sizecdatav(cd));
  }
}

void lj_cdata_setfin(lua_State *L, GCcdata *cd, GCobj *obj, uint32_t it)
{
  GCtab *t = tabref(G(L)->gcroot[GCROOT_FFI_FIN]);
  if (gcref(t->metatable)) {
    
    TValue *tv, tmp;
    setcdataV(L, &tmp, cd);
    lj_gc_anybarriert(L, t);
    tv = lj_tab_set(L, t, &tmp);
    if (it == LJ_TNIL) {
      setnilV(tv);
      cd->marked &= ~LJ_GC_CDATA_FIN;
    } else {
      setgcV(L, tv, obj, it);
      cd->marked |= LJ_GC_CDATA_FIN;
    }
  }
}




CType *lj_cdata_index(CTState *cts, GCcdata *cd, cTValue *key, uint8_t **pp,
		      CTInfo *qual)
{
  uint8_t *p = (uint8_t *)cdataptr(cd);
  CType *ct = ctype_get(cts, cd->ctypeid);
  ptrdiff_t idx;

  
  if (ctype_isref(ct->info)) {
    lj_assertCTS(ct->size == CTSIZE_PTR, "ref is not pointer-sized");
    p = *(uint8_t **)p;
    ct = ctype_child(cts, ct);
  }

collect_attrib:
  
  while (ctype_isattrib(ct->info)) {
    if (ctype_attrib(ct->info) == CTA_QUAL) *qual |= ct->size;
    ct = ctype_child(cts, ct);
  }
  
  lj_assertCTS(!ctype_isref(ct->info), "bad ref of ref");

  if (tvisint(key)) {
    idx = (ptrdiff_t)intV(key);
    goto integer_key;
  } else if (tvisnum(key)) {  
    idx = lj_num2int_type(numV(key), ptrdiff_t);
  integer_key:
    if (ctype_ispointer(ct->info)) {
      CTSize sz = lj_ctype_size(cts, ctype_cid(ct->info));  
      if (sz == CTSIZE_INVALID)
	lj_err_caller(cts->L, LJ_ERR_FFI_INVSIZE);
      if (ctype_isptr(ct->info)) {
	p = (uint8_t *)cdata_getptr(p, ct->size);
      } else if ((ct->info & (CTF_VECTOR|CTF_COMPLEX))) {
	if ((ct->info & CTF_COMPLEX)) idx &= 1;
	*qual |= CTF_CONST;  
      }
      *pp = p + idx*(int32_t)sz;
      return ct;
    }
  } else if (tviscdata(key)) {  
    GCcdata *cdk = cdataV(key);
    CType *ctk = ctype_raw(cts, cdk->ctypeid);
    if (ctype_isenum(ctk->info)) ctk = ctype_child(cts, ctk);
    if (ctype_isinteger(ctk->info)) {
      lj_cconv_ct_ct(cts, ctype_get(cts, CTID_INT_PSZ), ctk,
		     (uint8_t *)&idx, cdataptr(cdk), 0);
      goto integer_key;
    }
  } else if (tvisstr(key)) {  
    GCstr *name = strV(key);
    if (ctype_isstruct(ct->info)) {
      CTSize ofs;
      CType *fct = lj_ctype_getfieldq(cts, ct, name, &ofs, qual);
      if (fct) {
	*pp = p + ofs;
	return fct;
      }
    } else if (ctype_iscomplex(ct->info)) {
      if (name->len == 2) {
	*qual |= CTF_CONST;  
	if (strdata(name)[0] == 'r' && strdata(name)[1] == 'e') {
	  *pp = p;
	  return ct;
	} else if (strdata(name)[0] == 'i' && strdata(name)[1] == 'm') {
	  *pp = p + (ct->size >> 1);
	  return ct;
	}
      }
    } else if (cd->ctypeid == CTID_CTYPEID) {
      
      CType *sct = ctype_raw(cts, *(CTypeID *)p);
      if (ctype_isptr(sct->info))
	sct = ctype_rawchild(cts, sct);
      if (ctype_isstruct(sct->info)) {
	CTSize ofs;
	CType *fct = lj_ctype_getfield(cts, sct, name, &ofs);
	if (fct && ctype_isconstval(fct->info))
	  return fct;
      }
      ct = sct;  
    }
  }
  if (ctype_isptr(ct->info)) {  
    if (ctype_isstruct(ctype_rawchild(cts, ct)->info)) {
      p = (uint8_t *)cdata_getptr(p, ct->size);
      ct = ctype_child(cts, ct);
      goto collect_attrib;
    }
  }
  *qual |= 1;  
  return ct;  
}




static void cdata_getconst(CTState *cts, TValue *o, CType *ct)
{
  CType *ctt = ctype_child(cts, ct);
  lj_assertCTS(ctype_isinteger(ctt->info) && ctt->size <= 4,
	       "only 32 bit const supported");  
  
  if ((ctt->info & CTF_UNSIGNED) && (int32_t)ct->size < 0)
    setnumV(o, (lua_Number)(uint32_t)ct->size);
  else
    setintV(o, (int32_t)ct->size);
}


int lj_cdata_get(CTState *cts, CType *s, TValue *o, uint8_t *sp)
{
  CTypeID sid;

  if (ctype_isconstval(s->info)) {
    cdata_getconst(cts, o, s);
    return 0;  
  } else if (ctype_isbitfield(s->info)) {
    return lj_cconv_tv_bf(cts, s, o, sp);
  }

  
  lj_assertCTS(ctype_ispointer(s->info) || ctype_isfield(s->info),
	       "pointer or field expected");
  sid = ctype_cid(s->info);
  s = ctype_get(cts, sid);

  
  if (ctype_isref(s->info)) {
    lj_assertCTS(s->size == CTSIZE_PTR, "ref is not pointer-sized");
    sp = *(uint8_t **)sp;
    sid = ctype_cid(s->info);
    s = ctype_get(cts, sid);
  }

  
  while (ctype_isattrib(s->info))
    s = ctype_child(cts, s);

  return lj_cconv_tv_ct(cts, s, sid, o, sp);
}




void lj_cdata_set(CTState *cts, CType *d, uint8_t *dp, TValue *o, CTInfo qual)
{
  if (ctype_isconstval(d->info)) {
    goto err_const;
  } else if (ctype_isbitfield(d->info)) {
    if (((d->info|qual) & CTF_CONST)) goto err_const;
    lj_cconv_bf_tv(cts, d, dp, o);
    return;
  }

  
  lj_assertCTS(ctype_ispointer(d->info) || ctype_isfield(d->info),
	       "pointer or field expected");
  d = ctype_child(cts, d);

  
  if (ctype_isref(d->info)) {
    lj_assertCTS(d->size == CTSIZE_PTR, "ref is not pointer-sized");
    dp = *(uint8_t **)dp;
    d = ctype_child(cts, d);
  }

  
  for (;;) {
    if (ctype_isattrib(d->info)) {
      if (ctype_attrib(d->info) == CTA_QUAL) qual |= d->size;
    } else {
      break;
    }
    d = ctype_child(cts, d);
  }

  lj_assertCTS(ctype_hassize(d->info), "store to ctype without size");
  lj_assertCTS(!ctype_isvoid(d->info), "store to void type");

  if (((d->info|qual) & CTF_CONST)) {
  err_const:
    lj_err_caller(cts->L, LJ_ERR_FFI_WRCONST);
  }

  lj_cconv_ct_tv(cts, d, dp, o, 0);
}

#endif
