

#define lj_tab_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_err.h"
#include "lj_tab.h"




static Node *hashkey(const GCtab *t, cTValue *key)
{
  lj_assertX(!tvisint(key), "attempt to hash integer");
  if (tvisstr(key))
    return hashstr(t, strV(key));
  else if (tvisnum(key))
    return hashnum(t, key);
  else if (tvisbool(key))
    return hashmask(t, boolV(key));
  else
    return hashgcref(t, key->gcr);
  
}




static LJ_AINLINE void newhpart(lua_State *L, GCtab *t, uint32_t hbits)
{
  uint32_t hsize;
  Node *node;
  lj_assertL(hbits != 0, "zero hash size");
  if (hbits > LJ_MAX_HBITS)
    lj_err_msg(L, LJ_ERR_TABOV);
  hsize = 1u << hbits;
  node = lj_mem_newvec(L, hsize, Node);
  setmref(t->node, node);
  setfreetop(t, node, &node[hsize]);
  t->hmask = hsize-1;
}




static LJ_AINLINE void clearhpart(GCtab *t)
{
  uint32_t i, hmask = t->hmask;
  Node *node = noderef(t->node);
  lj_assertX(t->hmask != 0, "empty hash part");
  for (i = 0; i <= hmask; i++) {
    Node *n = &node[i];
    setmref(n->next, NULL);
    setnilV(&n->key);
    setnilV(&n->val);
  }
}


static LJ_AINLINE void clearapart(GCtab *t)
{
  uint32_t i, asize = t->asize;
  TValue *array = tvref(t->array);
  for (i = 0; i < asize; i++)
    setnilV(&array[i]);
}


static GCtab *newtab(lua_State *L, uint32_t asize, uint32_t hbits)
{
  GCtab *t;
  
  if (LJ_MAX_COLOSIZE != 0 && asize > 0 && asize <= LJ_MAX_COLOSIZE) {
    Node *nilnode;
    lj_assertL((sizeof(GCtab) & 7) == 0, "bad GCtab size");
    t = (GCtab *)lj_mem_newgco(L, sizetabcolo(asize));
    t->gct = ~LJ_TTAB;
    t->nomm = (uint8_t)~0;
    t->colo = (int8_t)asize;
    setmref(t->array, (TValue *)((char *)t + sizeof(GCtab)));
    setgcrefnull(t->metatable);
    t->asize = asize;
    t->hmask = 0;
    nilnode = &G(L)->nilnode;
    setmref(t->node, nilnode);
#if LJ_GC64
    setmref(t->freetop, nilnode);
#endif
  } else {  
    Node *nilnode;
    t = lj_mem_newobj(L, GCtab);
    t->gct = ~LJ_TTAB;
    t->nomm = (uint8_t)~0;
    t->colo = 0;
    setmref(t->array, NULL);
    setgcrefnull(t->metatable);
    t->asize = 0;  
    t->hmask = 0;
    nilnode = &G(L)->nilnode;
    setmref(t->node, nilnode);
#if LJ_GC64
    setmref(t->freetop, nilnode);
#endif
    if (asize > 0) {
      if (asize > LJ_MAX_ASIZE)
	lj_err_msg(L, LJ_ERR_TABOV);
      setmref(t->array, lj_mem_newvec(L, asize, TValue));
      t->asize = asize;
    }
  }
  if (hbits)
    newhpart(L, t, hbits);
  return t;
}


GCtab *lj_tab_new(lua_State *L, uint32_t asize, uint32_t hbits)
{
  GCtab *t = newtab(L, asize, hbits);
  clearapart(t);
  if (t->hmask > 0) clearhpart(t);
  return t;
}


GCtab *lj_tab_new_ah(lua_State *L, uint32_t a, uint32_t h)
{
  return lj_tab_new(L, a ? a+1 : 0, hsize2hbits(h));
}

#if LJ_HASJIT
GCtab * LJ_FASTCALL lj_tab_new1(lua_State *L, uint32_t ahsize)
{
  GCtab *t = newtab(L, ahsize & 0xffffff, ahsize >> 24);
  clearapart(t);
  if (t->hmask > 0) clearhpart(t);
  return t;
}
#endif


GCtab * LJ_FASTCALL lj_tab_dup(lua_State *L, const GCtab *kt)
{
  GCtab *t;
  uint32_t asize, hmask;
  t = newtab(L, kt->asize, kt->hmask > 0 ? lj_fls(kt->hmask)+1 : 0);
  lj_assertL(kt->asize == t->asize && kt->hmask == t->hmask,
	     "mismatched size of table and template");
  t->nomm = 0;  
  asize = kt->asize;
  if (asize > 0) {
    TValue *array = tvref(t->array);
    TValue *karray = tvref(kt->array);
    if (asize < 64) {  
      uint32_t i;
      for (i = 0; i < asize; i++)
	copyTV(L, &array[i], &karray[i]);
    } else {
      memcpy(array, karray, asize*sizeof(TValue));
    }
  }
  hmask = kt->hmask;
  if (hmask > 0) {
    uint32_t i;
    Node *node = noderef(t->node);
    Node *knode = noderef(kt->node);
    ptrdiff_t d = (char *)node - (char *)knode;
    setfreetop(t, node, (Node *)((char *)getfreetop(kt, knode) + d));
    for (i = 0; i <= hmask; i++) {
      Node *kn = &knode[i];
      Node *n = &node[i];
      Node *next = nextnode(kn);
      
      n->val = kn->val; n->key = kn->key;
      if (tvistab(&n->val)) setnilV(&n->val); 
      setmref(n->next, next == NULL? next : (Node *)((char *)next + d));
    }
  }
  return t;
}


void LJ_FASTCALL lj_tab_clear(GCtab *t)
{
  clearapart(t);
  if (t->hmask > 0) {
    Node *node = noderef(t->node);
    setfreetop(t, node, &node[t->hmask+1]);
    clearhpart(t);
  }
}


void LJ_FASTCALL lj_tab_free(global_State *g, GCtab *t)
{
  if (t->hmask > 0)
    lj_mem_freevec(g, noderef(t->node), t->hmask+1, Node);
  if (t->asize > 0 && LJ_MAX_COLOSIZE != 0 && t->colo <= 0)
    lj_mem_freevec(g, tvref(t->array), t->asize, TValue);
  if (LJ_MAX_COLOSIZE != 0 && t->colo)
    lj_mem_free(g, t, sizetabcolo((uint32_t)t->colo & 0x7f));
  else
    lj_mem_freet(g, t);
}




void lj_tab_resize(lua_State *L, GCtab *t, uint32_t asize, uint32_t hbits)
{
  Node *oldnode = noderef(t->node);
  uint32_t oldasize = t->asize;
  uint32_t oldhmask = t->hmask;
  if (asize > oldasize) {  
    TValue *array;
    uint32_t i;
    if (asize > LJ_MAX_ASIZE)
      lj_err_msg(L, LJ_ERR_TABOV);
    if (LJ_MAX_COLOSIZE != 0 && t->colo > 0) {
      
      TValue *oarray = tvref(t->array);
      array = lj_mem_newvec(L, asize, TValue);
      t->colo = (int8_t)(t->colo | 0x80);  
      for (i = 0; i < oldasize; i++)
	copyTV(L, &array[i], &oarray[i]);
    } else {
      array = (TValue *)lj_mem_realloc(L, tvref(t->array),
			  oldasize*sizeof(TValue), asize*sizeof(TValue));
    }
    setmref(t->array, array);
    t->asize = asize;
    for (i = oldasize; i < asize; i++)  
      setnilV(&array[i]);
  }
  
  if (hbits) {
    newhpart(L, t, hbits);
    clearhpart(t);
  } else {
    global_State *g = G(L);
    setmref(t->node, &g->nilnode);
#if LJ_GC64
    setmref(t->freetop, &g->nilnode);
#endif
    t->hmask = 0;
  }
  if (asize < oldasize) {  
    TValue *array = tvref(t->array);
    uint32_t i;
    t->asize = asize;  
    for (i = asize; i < oldasize; i++)  
      if (!tvisnil(&array[i]))
	copyTV(L, lj_tab_setinth(L, t, (int32_t)i), &array[i]);
    
    if (LJ_MAX_COLOSIZE != 0 && t->colo <= 0)
      setmref(t->array, lj_mem_realloc(L, array,
	      oldasize*sizeof(TValue), asize*sizeof(TValue)));
  }
  if (oldhmask > 0) {  
    global_State *g;
    uint32_t i;
    for (i = 0; i <= oldhmask; i++) {
      Node *n = &oldnode[i];
      if (!tvisnil(&n->val))
	copyTV(L, lj_tab_set(L, t, &n->key), &n->val);
    }
    g = G(L);
    lj_mem_freevec(g, oldnode, oldhmask+1, Node);
  }
}

static uint32_t countint(cTValue *key, uint32_t *bins)
{
  lj_assertX(!tvisint(key), "bad integer key");
  if (tvisnum(key)) {
    int64_t i64;
    int32_t k;
    if (lj_num2int_cond(numV(key), i64, k, (uint32_t)i64 < LJ_MAX_ASIZE)) {
      bins[(k > 2 ? lj_fls((uint32_t)(k-1)) : 0)]++;
      return 1;
    }
  }
  return 0;
}

static uint32_t countarray(const GCtab *t, uint32_t *bins)
{
  uint32_t na, b, i;
  if (t->asize == 0) return 0;
  for (na = i = b = 0; b < LJ_MAX_ABITS; b++) {
    uint32_t n, top = 2u << b;
    TValue *array;
    if (top >= t->asize) {
      top = t->asize-1;
      if (i > top)
	break;
    }
    array = tvref(t->array);
    for (n = 0; i <= top; i++)
      if (!tvisnil(&array[i]))
	n++;
    bins[b] += n;
    na += n;
  }
  return na;
}

static uint32_t counthash(const GCtab *t, uint32_t *bins, uint32_t *narray)
{
  uint32_t total, na, i, hmask = t->hmask;
  Node *node = noderef(t->node);
  for (total = na = 0, i = 0; i <= hmask; i++) {
    Node *n = &node[i];
    if (!tvisnil(&n->val)) {
      na += countint(&n->key, bins);
      total++;
    }
  }
  *narray += na;
  return total;
}

static uint32_t bestasize(uint32_t bins[], uint32_t *narray)
{
  uint32_t b, sum, na = 0, sz = 0, nn = *narray;
  for (b = 0, sum = 0; 2*nn > (1u<<b) && sum != nn; b++)
    if (bins[b] > 0 && 2*(sum += bins[b]) > (1u<<b)) {
      sz = (2u<<b)+1;
      na = sum;
    }
  *narray = sz;
  return na;
}

static void rehashtab(lua_State *L, GCtab *t, cTValue *ek)
{
  uint32_t bins[LJ_MAX_ABITS];
  uint32_t total, asize, na, i;
  for (i = 0; i < LJ_MAX_ABITS; i++) bins[i] = 0;
  asize = countarray(t, bins);
  total = 1 + asize;
  total += counthash(t, bins, &asize);
  asize += countint(ek, bins);
  na = bestasize(bins, &asize);
  total -= na;
  lj_tab_resize(L, t, asize, hsize2hbits(total));
}

void lj_tab_reasize(lua_State *L, GCtab *t, uint32_t nasize)
{
  lj_tab_resize(L, t, nasize+1, t->hmask > 0 ? lj_fls(t->hmask)+1 : 0);
}



cTValue * LJ_FASTCALL lj_tab_getinth(GCtab *t, int32_t key)
{
  TValue k;
  Node *n;
  k.n = (lua_Number)key;
  n = hashnum(t, &k);
  do {
    if (tvisnum(&n->key) && n->key.n == k.n)
      return &n->val;
  } while ((n = nextnode(n)));
  return NULL;
}

cTValue *lj_tab_getstr(GCtab *t, const GCstr *key)
{
  Node *n = hashstr(t, key);
  do {
    if (tvisstr(&n->key) && strV(&n->key) == key)
      return &n->val;
  } while ((n = nextnode(n)));
  return NULL;
}

cTValue *lj_tab_get(lua_State *L, GCtab *t, cTValue *key)
{
  if (tvisstr(key)) {
    cTValue *tv = lj_tab_getstr(t, strV(key));
    if (tv)
      return tv;
  } else if (tvisint(key)) {
    cTValue *tv = lj_tab_getint(t, intV(key));
    if (tv)
      return tv;
  } else if (tvisnum(key)) {
    int64_t i64;
    int32_t k;
    if (lj_num2int_check(numV(key), i64, k)) {
      cTValue *tv = lj_tab_getint(t, k);
      if (tv)
	return tv;
    } else {
      goto genlookup;  
    }
  } else if (!tvisnil(key)) {
    Node *n;
  genlookup:
    n = hashkey(t, key);
    do {
      if (lj_obj_equal(&n->key, key))
	return &n->val;
    } while ((n = nextnode(n)));
  }
  return niltv(L);
}




TValue *lj_tab_newkey(lua_State *L, GCtab *t, cTValue *key)
{
  Node *n = hashkey(t, key);
  if (!tvisnil(&n->val) || t->hmask == 0) {
    Node *nodebase = noderef(t->node);
    Node *collide, *freenode = getfreetop(t, nodebase);
    lj_assertL(freenode >= nodebase && freenode <= nodebase+t->hmask+1,
	       "bad freenode");
    do {
      if (freenode == nodebase) {  
	rehashtab(L, t, key);  
	return lj_tab_set(L, t, key);  
      }
    } while (!tvisnil(&(--freenode)->key));
    setfreetop(t, nodebase, freenode);
    lj_assertL(freenode != &G(L)->nilnode, "store to fallback hash");
    collide = hashkey(t, &n->key);
    if (collide != n) {  
      while (noderef(collide->next) != n)  
	collide = nextnode(collide);
      setmref(collide->next, freenode);  
      
      freenode->val = n->val;
      freenode->key = n->key;
      freenode->next = n->next;
      setmref(n->next, NULL);
      setnilV(&n->val);
      
      while (nextnode(freenode)) {
	Node *nn = nextnode(freenode);
	if (!tvisnil(&nn->val) && hashkey(t, &nn->key) == n) {
	  freenode->next = nn->next;
	  nn->next = n->next;
	  setmref(n->next, nn);
	  
	  while ((nn = nextnode(freenode))) {
	    if (!tvisnil(&nn->val)) {
	      Node *mn = hashkey(t, &nn->key);
	      if (mn != freenode && mn != nn) {
		freenode->next = nn->next;
		nn->next = mn->next;
		setmref(mn->next, nn);
	      } else {
		freenode = nn;
	      }
	    } else {
	      freenode = nn;
	    }
	  }
	  break;
	} else {
	  freenode = nn;
	}
      }
    } else {  
      setmrefr(freenode->next, n->next);  
      setmref(n->next, freenode);
      n = freenode;
    }
  }
  n->key.u64 = key->u64;
  if (LJ_UNLIKELY(tvismzero(&n->key)))
    n->key.u64 = 0;
  lj_gc_anybarriert(L, t);
  lj_assertL(tvisnil(&n->val), "new hash slot is not empty");
  return &n->val;
}

TValue *lj_tab_setinth(lua_State *L, GCtab *t, int32_t key)
{
  TValue k;
  Node *n;
  k.n = (lua_Number)key;
  n = hashnum(t, &k);
  do {
    if (tvisnum(&n->key) && n->key.n == k.n)
      return &n->val;
  } while ((n = nextnode(n)));
  return lj_tab_newkey(L, t, &k);
}

TValue *lj_tab_setstr(lua_State *L, GCtab *t, const GCstr *key)
{
  TValue k;
  Node *n = hashstr(t, key);
  do {
    if (tvisstr(&n->key) && strV(&n->key) == key)
      return &n->val;
  } while ((n = nextnode(n)));
  setstrV(L, &k, key);
  return lj_tab_newkey(L, t, &k);
}

TValue *lj_tab_set(lua_State *L, GCtab *t, cTValue *key)
{
  Node *n;
  t->nomm = 0;  
  if (tvisstr(key)) {
    return lj_tab_setstr(L, t, strV(key));
  } else if (tvisint(key)) {
    return lj_tab_setint(L, t, intV(key));
  } else if (tvisnum(key)) {
    int64_t i64;
    int32_t k;
    if (lj_num2int_check(numV(key), i64, k))
      return lj_tab_setint(L, t, k);
    if (tvisnan(key))
      lj_err_msg(L, LJ_ERR_NANIDX);
    
  } else if (tvisnil(key)) {
    lj_err_msg(L, LJ_ERR_NILIDX);
  }
  n = hashkey(t, key);
  do {
    if (lj_obj_equal(&n->key, key))
      return &n->val;
  } while ((n = nextnode(n)));
  return lj_tab_newkey(L, t, key);
}






uint32_t LJ_FASTCALL lj_tab_keyindex(GCtab *t, cTValue *key)
{
  TValue tmp;
  if (tvisint(key)) {
    int32_t k = intV(key);
    if ((uint32_t)k < t->asize)
      return (uint32_t)k + 1;
    setnumV(&tmp, (lua_Number)k);
    key = &tmp;
  } else if (tvisnum(key)) {
    int64_t i64;
    int32_t k;
    if (lj_num2int_cond(numV(key), i64, k, (uint32_t)i64 < t->asize))
      return (uint32_t)k + 1;
  }
  if (!tvisnil(key)) {
    Node *n = hashkey(t, key);
    do {
      if (lj_obj_equal(&n->key, key))
	return t->asize + (uint32_t)((n+1) - noderef(t->node));
    } while ((n = nextnode(n)));
    if (key->u32.hi == LJ_KEYINDEX)  
      return key->u32.lo;
    return ~0u;  
  }
  return 0;  
}


int lj_tab_next(GCtab *t, cTValue *key, TValue *o)
{
  uint32_t idx = lj_tab_keyindex(t, key);  
  
  for (; idx < t->asize; idx++) {
    cTValue *a = arrayslot(t, idx);
    if (LJ_LIKELY(!tvisnil(a))) {
      setintV(o, idx);
      o[1] = *a;
      return 1;
    }
  }
  idx -= t->asize;
  
  for (; idx <= t->hmask; idx++) {
    Node *n = &noderef(t->node)[idx];
    if (!tvisnil(&n->val)) {
      o[0] = n->key;
      o[1] = n->val;
      return 1;
    }
  }
  return (int32_t)idx < 0 ? -1 : 0;  
}




LJ_NOINLINE static MSize tab_len_slow(GCtab *t, size_t hi)
{
  cTValue *tv;
  size_t lo = hi;
  hi++;
  
  while ((tv = lj_tab_getint(t, (int32_t)hi)) && !tvisnil(tv)) {
    lo = hi;
    hi += hi;
    if (hi > (size_t)(0x7fffffff - 2)) {  
      lo = 1;
      while ((tv = lj_tab_getint(t, (int32_t)lo)) && !tvisnil(tv)) lo++;
      return (MSize)(lo - 1);
    }
  }
  
  while (hi - lo > 1) {
    size_t mid = (lo+hi) >> 1;
    cTValue *tvb = lj_tab_getint(t, (int32_t)mid);
    if (tvb && !tvisnil(tvb)) lo = mid; else hi = mid;
  }
  return (MSize)lo;
}


MSize LJ_FASTCALL lj_tab_len(GCtab *t)
{
  size_t hi = (size_t)t->asize;
  if (hi) hi--;
  
  if (hi > 0 && LJ_LIKELY(tvisnil(arrayslot(t, hi)))) {
    
    size_t lo = 0;
    while (hi - lo > 1) {
      size_t mid = (lo+hi) >> 1;
      if (tvisnil(arrayslot(t, mid))) hi = mid; else lo = mid;
    }
    return (MSize)lo;
  }
  
  return t->hmask ? tab_len_slow(t, hi) : (MSize)hi;
}

#if LJ_HASJIT

MSize LJ_FASTCALL lj_tab_len_hint(GCtab *t, size_t hint)
{
  size_t asize = (size_t)t->asize;
  cTValue *tv = arrayslot(t, hint);
  if (LJ_LIKELY(hint+1 < asize)) {
    if (LJ_LIKELY(!tvisnil(tv) && tvisnil(tv+1))) return (MSize)hint;
  } else if (hint+1 <= asize && LJ_LIKELY(t->hmask == 0) && !tvisnil(tv)) {
    return (MSize)hint;
  }
  return lj_tab_len(t);
}
#endif

