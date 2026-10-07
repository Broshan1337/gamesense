

#include "lj_obj.h"

#if LJ_HASFFI

#include "lj_gc.h"
#include "lj_err.h"
#include "lj_tab.h"
#include "lj_ctype.h"
#include "lj_cconv.h"
#include "lj_cdata.h"
#include "lj_ccall.h"
#include "lj_trace.h"


#if LJ_TARGET_X86


#define CCALL_PUSH(arg) \
  *(GPRArg *)((uint8_t *)cc->stack + nsp) = (GPRArg)(arg), nsp += CTSIZE_PTR

#if LJ_ABI_WIN

#define CCALL_HANDLE_STRUCTRET \
   \
  cc->retref = (sz > 8); \
  if (cc->retref) CCALL_PUSH(dp);

#define CCALL_HANDLE_COMPLEXRET CCALL_HANDLE_STRUCTRET

#else

#if LJ_TARGET_OSX

#define CCALL_HANDLE_STRUCTRET \
   \
  cc->retref = !(sz == 1 || sz == 2 || sz == 4 || sz == 8); \
  if (cc->retref) { \
    if (ngpr < maxgpr) \
      cc->gpr[ngpr++] = (GPRArg)dp; \
    else \
      CCALL_PUSH(dp); \
  } else {   \
    cc->resx87 = ccall_classify_struct(cts, ctr); \
  }

#define CCALL_HANDLE_STRUCTRET2 \
  if (cc->resx87) sp = (uint8_t *)&cc->fpr[0]; \
  memcpy(dp, sp, ctr->size);

#else

#define CCALL_HANDLE_STRUCTRET \
  cc->retref = 1;   \
  if (ngpr < maxgpr) \
    cc->gpr[ngpr++] = (GPRArg)dp; \
  else \
    CCALL_PUSH(dp);

#endif

#define CCALL_HANDLE_COMPLEXRET \
   \
  cc->retref = (sz > 8); \
  if (cc->retref) { \
    if (ngpr < maxgpr) \
      cc->gpr[ngpr++] = (GPRArg)dp; \
    else \
      CCALL_PUSH(dp); \
  }

#endif

#define CCALL_HANDLE_COMPLEXRET2 \
  if (!cc->retref) \
    *(int64_t *)dp = *(int64_t *)sp;  

#define CCALL_HANDLE_STRUCTARG \
  ngpr = maxgpr;  

#define CCALL_HANDLE_COMPLEXARG \
  isfp = 1;  

#define CCALL_HANDLE_REGARG \
  if (!isfp) {   \
    if (n > 1) {   \
      if (!LJ_ABI_WIN) ngpr = maxgpr;   \
    } else if (ngpr + 1 <= maxgpr) { \
      dp = &cc->gpr[ngpr]; \
      ngpr += n; \
      goto done; \
    } \
  }

#elif LJ_TARGET_X64 && LJ_ABI_WIN


#define CCALL_HANDLE_STRUCTRET \
   \
  cc->retref = !(sz == 1 || sz == 2 || sz == 4 || sz == 8); \
  if (cc->retref) cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET CCALL_HANDLE_STRUCTRET

#define CCALL_HANDLE_COMPLEXRET2 \
  if (!cc->retref) \
    *(int64_t *)dp = *(int64_t *)sp;  

#define CCALL_HANDLE_STRUCTARG \
   \
  if (!(sz == 1 || sz == 2 || sz == 4 || sz == 8)) { \
    rp = cdataptr(lj_cdata_new(cts, did, sz)); \
    sz = CTSIZE_PTR;   \
  }

#define CCALL_HANDLE_COMPLEXARG \
   \
  if (sz != 2*sizeof(float)) { \
    rp = cdataptr(lj_cdata_new(cts, did, sz)); \
    sz = CTSIZE_PTR; \
  }


#define CCALL_HANDLE_REGARG \
  if (isfp) { \
    if (ngpr < maxgpr) { dp = &cc->fpr[ngpr++]; nfpr = ngpr; goto done; } \
  } else { \
    if (ngpr < maxgpr) { dp = &cc->gpr[ngpr++]; goto done; } \
  }

#elif LJ_TARGET_X64


#define CCALL_HANDLE_STRUCTRET \
  int rcl[2]; rcl[0] = rcl[1] = 0; \
  if (ccall_classify_struct(cts, ctr, rcl, 0)) { \
    cc->retref = 1;   \
    cc->gpr[ngpr++] = (GPRArg)dp; \
  } else { \
    cc->retref = 0;   \
  }

#define CCALL_HANDLE_STRUCTRET2 \
  int rcl[2]; rcl[0] = rcl[1] = 0; \
  ccall_classify_struct(cts, ctr, rcl, 0); \
  ccall_struct_ret(cc, rcl, dp, ctr->size);

#define CCALL_HANDLE_COMPLEXRET \
   \
  cc->retref = 0;

#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {   \
    *(int64_t *)dp = cc->fpr[0].l[0]; \
  } else {   \
    ((int64_t *)dp)[0] = cc->fpr[0].l[0]; \
    ((int64_t *)dp)[1] = cc->fpr[1].l[0]; \
  }

#define CCALL_HANDLE_STRUCTARG \
  int rcl[2]; rcl[0] = rcl[1] = 0; \
  if (!ccall_classify_struct(cts, d, rcl, 0)) { \
    cc->nsp = nsp; cc->ngpr = ngpr; cc->nfpr = nfpr; \
    if (ccall_struct_arg(cc, cts, d, rcl, o, narg)) goto err_nyi; \
    nsp = cc->nsp; ngpr = cc->ngpr; nfpr = cc->nfpr; \
    continue; \
  } else {   \
    onstack = 1; \
  }

#define CCALL_HANDLE_COMPLEXARG \
  isfp = 2;  

#define CCALL_HANDLE_REGARG \
  if (isfp) {   \
    int n2 = ctype_isvector(d->info) ? 1 : n; \
    if (nfpr + n2 <= CCALL_NARG_FPR) { \
      dp = &cc->fpr[nfpr]; \
      nfpr += n2; \
      goto done; \
    } \
  } else {   \
     \
    if (!onstack && n <= 2 && ngpr + n <= maxgpr) { \
      dp = &cc->gpr[ngpr]; \
      ngpr += n; \
      goto done; \
    } \
  }

#elif LJ_TARGET_ARM


#if LJ_ABI_SOFTFP

#define CCALL_HANDLE_STRUCTRET \
   \
  cc->retref = !(sz <= 4); \
  if (cc->retref) cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET \
  cc->retref = 1;   \
  cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET2 \
  UNUSED(dp); 

#define CCALL_HANDLE_STRUCTARG \
  

#define CCALL_HANDLE_COMPLEXARG \
  

#define CCALL_HANDLE_REGARG_FP1
#define CCALL_HANDLE_REGARG_FP2

#else

#define CCALL_HANDLE_STRUCTRET \
  cc->retref = !ccall_classify_struct(cts, ctr, ct); \
  if (cc->retref) cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_STRUCTRET2 \
  if (ccall_classify_struct(cts, ctr, ct) > 1) sp = (uint8_t *)&cc->fpr[0]; \
  memcpy(dp, sp, ctr->size);

#define CCALL_HANDLE_COMPLEXRET \
  if (!(ct->info & CTF_VARARG)) cc->retref = 0;  

#define CCALL_HANDLE_COMPLEXRET2 \
  if (!(ct->info & CTF_VARARG)) memcpy(dp, &cc->fpr[0], ctr->size);

#define CCALL_HANDLE_STRUCTARG \
  isfp = (ccall_classify_struct(cts, d, ct) > 1);
  

#define CCALL_HANDLE_COMPLEXARG \
  isfp = 1;  

#define CCALL_HANDLE_REGARG_FP1 \
  if (isfp && !(ct->info & CTF_VARARG)) { \
    if ((d->info & CTF_ALIGN) > CTALIGN_PTR) { \
      if (nfpr + (n >> 1) <= CCALL_NARG_FPR) { \
	dp = &cc->fpr[nfpr]; \
	nfpr += (n >> 1); \
	goto done; \
      } \
    } else { \
      if (sz > 1 && fprodd != nfpr) fprodd = 0; \
      if (fprodd) { \
	if (2*nfpr+n <= 2*CCALL_NARG_FPR+1) { \
	  dp = (void *)&cc->fpr[fprodd-1].f[1]; \
	  nfpr += (n >> 1); \
	  if ((n & 1)) fprodd = 0; else fprodd = nfpr-1; \
	  goto done; \
	} \
      } else { \
	if (2*nfpr+n <= 2*CCALL_NARG_FPR) { \
	  dp = (void *)&cc->fpr[nfpr]; \
	  nfpr += (n >> 1); \
	  if ((n & 1)) fprodd = ++nfpr; else fprodd = 0; \
	  goto done; \
	} \
      } \
    } \
    fprodd = 0;   \
  } else {

#define CCALL_HANDLE_REGARG_FP2	}

#endif

#define CCALL_HANDLE_REGARG \
  CCALL_HANDLE_REGARG_FP1 \
  if ((d->info & CTF_ALIGN) > CTALIGN_PTR) { \
    if (ngpr < maxgpr) \
      ngpr = (ngpr + 1u) & ~1u;   \
  } \
  if (ngpr < maxgpr) { \
    dp = &cc->gpr[ngpr]; \
    if (ngpr + n > maxgpr) { \
      nsp += (ngpr + n - maxgpr) * CTSIZE_PTR;   \
      if (nsp > CCALL_SIZE_STACK) goto err_nyi;   \
      ngpr = maxgpr; \
    } else { \
      ngpr += n; \
    } \
    goto done; \
  } CCALL_HANDLE_REGARG_FP2

#define CCALL_HANDLE_RET \
  if ((ct->info & CTF_VARARG)) sp = (uint8_t *)&cc->gpr[0];

#elif LJ_TARGET_ARM64


#define CCALL_HANDLE_STRUCTRET \
  cc->retref = !ccall_classify_struct(cts, ctr); \
  if (cc->retref) cc->retp = dp;

#define CCALL_HANDLE_STRUCTRET2 \
  unsigned int cl = ccall_classify_struct(cts, ctr); \
  if ((cl & 4)) {  \
    CTSize i = (cl >> 8) - 1; \
    do { ((uint32_t *)dp)[i] = cc->fpr[i].lo; } while (i--); \
  } else { \
    if (cl > 1) sp = (uint8_t *)&cc->fpr[0]; \
    memcpy(dp, sp, ctr->size); \
  }

#define CCALL_HANDLE_COMPLEXRET \
   \
  cc->retref = 0;

#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {   \
    ((float *)dp)[0] = cc->fpr[0].f; \
    ((float *)dp)[1] = cc->fpr[1].f; \
  } else {   \
    ((double *)dp)[0] = cc->fpr[0].d; \
    ((double *)dp)[1] = cc->fpr[1].d; \
  }

#define CCALL_HANDLE_STRUCTARG \
  unsigned int cl = ccall_classify_struct(cts, d); \
  if (cl == 0) {   \
    rp = cdataptr(lj_cdata_new(cts, did, sz)); \
    sz = CTSIZE_PTR; \
  } else if (cl > 1) {   \
    isfp = (cl & 4) ? 2 : 1; \
  }  

#define CCALL_HANDLE_COMPLEXARG \
   \
  isfp = sz == 2*sizeof(float) ? 2 : 1;

#define CCALL_HANDLE_REGARG \
  if (LJ_TARGET_OSX && isva) { \
     \
  } else if (isfp) {   \
    int n2 = ctype_isvector(d->info) ? 1 : \
	     isfp == 1 ? n : (d->size >> (4-isfp)); \
    if (nfpr + n2 <= CCALL_NARG_FPR) { \
      dp = &cc->fpr[nfpr]; \
      nfpr += n2; \
      goto done; \
    } else { \
      nfpr = CCALL_NARG_FPR;   \
    } \
  } else {   \
    if (!LJ_TARGET_OSX && !rp && ccall_struct_align(cts, d) > CTALIGN_PTR) \
      ngpr = (ngpr + 1u) & ~1u;   \
    if (ngpr + n <= maxgpr) { \
      dp = &cc->gpr[ngpr]; \
      ngpr += n; \
      goto done; \
    } else { \
      ngpr = maxgpr;   \
    } \
  }

#if LJ_BE
#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    sp = (uint8_t *)&cc->fpr[0].f;
#endif


#elif LJ_TARGET_PPC


#define CCALL_HANDLE_STRUCTRET \
  cc->retref = 1;   \
  cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET \
   \
  cc->retref = 0;

#define CCALL_HANDLE_COMPLEXRET2 \
  memcpy(dp, sp, ctr->size);  

#define CCALL_HANDLE_STRUCTARG \
  rp = cdataptr(lj_cdata_new(cts, did, sz)); \
  sz = CTSIZE_PTR;  

#define CCALL_HANDLE_COMPLEXARG \
  

#define CCALL_HANDLE_GPR \
   \
  if (n > 1) { \
     \
    lj_assertL(n == 2 || n == 4, "bad GPR size %d", n); \
    if (ctype_isinteger(d->info) || ctype_isfp(d->info)) \
      ngpr = (ngpr + 1u) & ~1u;   \
    else if (ngpr + n > maxgpr) \
      ngpr = maxgpr;   \
  } \
  if (ngpr + n <= maxgpr) { \
    dp = &cc->gpr[ngpr]; \
    ngpr += n; \
    goto done; \
  } \

#if LJ_ABI_SOFTFP
#define CCALL_HANDLE_REGARG  CCALL_HANDLE_GPR
#else
#define CCALL_HANDLE_REGARG \
  if (isfp) {   \
    if (nfpr + 1 <= CCALL_NARG_FPR) { \
      dp = &cc->fpr[nfpr]; \
      nfpr += 1; \
      d = ctype_get(cts, CTID_DOUBLE);   \
      goto done; \
    } \
  } else { \
    CCALL_HANDLE_GPR \
  }
#endif

#if !LJ_ABI_SOFTFP
#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    ctr = ctype_get(cts, CTID_DOUBLE);  
#endif

#elif LJ_TARGET_MIPS32


#define CCALL_HANDLE_STRUCTRET \
  cc->retref = 1;   \
  cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_COMPLEXRET \
   \
  cc->retref = 0;

#if LJ_ABI_SOFTFP
#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {   \
    ((intptr_t *)dp)[0] = cc->gpr[0]; \
    ((intptr_t *)dp)[1] = cc->gpr[1]; \
  } else {   \
    ((intptr_t *)dp)[0] = cc->gpr[0]; \
    ((intptr_t *)dp)[1] = cc->gpr[1]; \
    ((intptr_t *)dp)[2] = cc->gpr[2]; \
    ((intptr_t *)dp)[3] = cc->gpr[3]; \
  }
#else
#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {   \
    ((float *)dp)[0] = cc->fpr[0].f; \
    ((float *)dp)[1] = cc->fpr[1].f; \
  } else {   \
    ((double *)dp)[0] = cc->fpr[0].d; \
    ((double *)dp)[1] = cc->fpr[1].d; \
  }
#endif

#define CCALL_HANDLE_STRUCTARG \
  

#define CCALL_HANDLE_COMPLEXARG \
  

#define CCALL_HANDLE_GPR \
  if ((d->info & CTF_ALIGN) > CTALIGN_PTR) \
    ngpr = (ngpr + 1u) & ~1u;   \
  if (ngpr < maxgpr) { \
    dp = &cc->gpr[ngpr]; \
    if (ngpr + n > maxgpr) { \
     nsp += (ngpr + n - maxgpr) * CTSIZE_PTR;   \
     if (nsp > CCALL_SIZE_STACK) goto err_nyi;   \
     ngpr = maxgpr; \
    } else { \
     ngpr += n; \
    } \
    goto done; \
  }

#if !LJ_ABI_SOFTFP	
#define CCALL_HANDLE_REGARG \
  if (isfp && nfpr < CCALL_NARG_FPR && !(ct->info & CTF_VARARG)) { \
     \
    dp = n == 1 ? (void *)&cc->fpr[nfpr].f : (void *)&cc->fpr[nfpr].d; \
    nfpr++; ngpr += n; \
    goto done; \
  } else {   \
    nfpr = CCALL_NARG_FPR; \
    CCALL_HANDLE_GPR \
  }
#else			
#define CCALL_HANDLE_REGARG CCALL_HANDLE_GPR
#endif

#if !LJ_ABI_SOFTFP

#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    sp = (uint8_t *)&cc->fpr[0].f;
#endif

#elif LJ_TARGET_MIPS64


#define CCALL_HANDLE_STRUCTRET \
  cc->retref = !(sz <= 16); \
  if (cc->retref) cc->gpr[ngpr++] = (GPRArg)dp;

#define CCALL_HANDLE_STRUCTRET2 \
  ccall_copy_struct(cc, ctr, dp, sp, ccall_classify_struct(cts, ctr, ct));

#define CCALL_HANDLE_COMPLEXRET \
   \
  cc->retref = 0;

#if LJ_ABI_SOFTFP	

#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {   \
    ((intptr_t *)dp)[0] = cc->gpr[0]; \
  } else {   \
    ((intptr_t *)dp)[0] = cc->gpr[0]; \
    ((intptr_t *)dp)[1] = cc->gpr[1]; \
  }

#define CCALL_HANDLE_COMPLEXARG \
  


#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    sp = (uint8_t *)cc->gpr + LJ_ENDIAN_SELECT(0, 4);

#else			

#define CCALL_HANDLE_COMPLEXRET2 \
  if (ctr->size == 2*sizeof(float)) {   \
    ((float *)dp)[0] = cc->fpr[0].f; \
    ((float *)dp)[1] = cc->fpr[1].f; \
  } else {   \
    ((double *)dp)[0] = cc->fpr[0].d; \
    ((double *)dp)[1] = cc->fpr[1].d; \
  }

#define CCALL_HANDLE_COMPLEXARG \
  if (sz == 2*sizeof(float)) { \
    isfp = 2; \
    if (ngpr < maxgpr) \
      sz *= 2; \
  }

#define CCALL_HANDLE_RET \
  if (ctype_isfp(ctr->info) && ctr->size == sizeof(float)) \
    sp = (uint8_t *)&cc->fpr[0].f;

#endif

#define CCALL_HANDLE_STRUCTARG \
  

#define CCALL_HANDLE_REGARG \
  if (ngpr < maxgpr) { \
    dp = &cc->gpr[ngpr]; \
    if (ngpr + n > maxgpr) { \
      nsp += (ngpr + n - maxgpr) * CTSIZE_PTR;   \
      if (nsp > CCALL_SIZE_STACK) goto err_nyi;   \
      ngpr = maxgpr; \
    } else { \
      ngpr += n; \
    } \
    goto done; \
  }

#else
#error "Missing calling convention definitions for this architecture"
#endif

#ifndef CCALL_HANDLE_STRUCTRET2
#define CCALL_HANDLE_STRUCTRET2 \
  memcpy(dp, sp, ctr->size);  
#endif



#if LJ_TARGET_X86 && LJ_TARGET_OSX


static int ccall_classify_struct(CTState *cts, CType *ct)
{
  CTSize sz = ct->size;
  if (!(sz == sizeof(float) || sz == sizeof(double))) return 0;
  if ((ct->info & CTF_UNION)) return 0;
  while (ct->sib) {
    ct = ctype_get(cts, ct->sib);
    if (ctype_isfield(ct->info)) {
      CType *sct = ctype_rawchild(cts, ct);
      if (ctype_isfp(sct->info)) {
	if (sct->size == sz)
	  return (sz >> 2);  
      } else if (ctype_isstruct(sct->info)) {
	if (sct->size)
	  return ccall_classify_struct(cts, sct);
      } else {
	break;
      }
    } else if (ctype_isbitfield(ct->info)) {
      break;
    } else if (ctype_isxattrib(ct->info, CTA_SUBTYPE)) {
      CType *sct = ctype_rawchild(cts, ct);
      if (sct->size)
	return ccall_classify_struct(cts, sct);
    }
  }
  return 0;
}

#endif



#if LJ_TARGET_X64 && !LJ_ABI_WIN


#define CCALL_RCL_INT	1
#define CCALL_RCL_SSE	2
#define CCALL_RCL_MEM	4


static int ccall_classify_struct(CTState *cts, CType *ct, int *rcl, CTSize ofs);


static void ccall_classify_ct(CTState *cts, CType *ct, int *rcl, CTSize ofs)
{
  if (ctype_isarray(ct->info)) {
    CType *cct = ctype_rawchild(cts, ct);
    CTSize eofs, esz = cct->size, asz = ct->size;
    for (eofs = 0; eofs < asz; eofs += esz)
      ccall_classify_ct(cts, cct, rcl, ofs+eofs);
  } else if (ctype_isstruct(ct->info)) {
    ccall_classify_struct(cts, ct, rcl, ofs);
  } else {
    int cl = ctype_isfp(ct->info) ? CCALL_RCL_SSE : CCALL_RCL_INT;
    lj_assertCTS(ctype_hassize(ct->info),
		 "classify ctype %08x without size", ct->info);
    if ((ofs & (ct->size-1))) cl = CCALL_RCL_MEM;  
    rcl[(ofs >= 8)] |= cl;
  }
}


static int ccall_classify_struct(CTState *cts, CType *ct, int *rcl, CTSize ofs)
{
  if (ct->size > 16) return CCALL_RCL_MEM;  
  while (ct->sib) {
    CTSize fofs;
    ct = ctype_get(cts, ct->sib);
    fofs = ofs+ct->size;
    if (ctype_isfield(ct->info))
      ccall_classify_ct(cts, ctype_rawchild(cts, ct), rcl, fofs);
    else if (ctype_isbitfield(ct->info) && ctype_bitbsz(ct->info))
      rcl[(fofs >= 8)] |= CCALL_RCL_INT;  
    else if (ctype_isxattrib(ct->info, CTA_SUBTYPE))
      ccall_classify_struct(cts, ctype_rawchild(cts, ct), rcl, fofs);
  }
  return ((rcl[0]|rcl[1]) & CCALL_RCL_MEM);  
}


static int ccall_struct_reg(CCallState *cc, CTState *cts, GPRArg *dp, int *rcl)
{
  MSize ngpr = cc->ngpr, nfpr = cc->nfpr;
  uint32_t i;
  UNUSED(cts);
  for (i = 0; i < 2; i++) {
    lj_assertCTS(!(rcl[i] & CCALL_RCL_MEM), "pass mem struct in reg");
    if ((rcl[i] & CCALL_RCL_INT)) {  
      if (ngpr >= CCALL_NARG_GPR) return 1;  
      cc->gpr[ngpr++] = dp[i];
    } else if ((rcl[i] & CCALL_RCL_SSE)) {
      if (nfpr >= CCALL_NARG_FPR) return 1;  
      cc->fpr[nfpr++].l[0] = dp[i];
    }
  }
  cc->ngpr = ngpr; cc->nfpr = nfpr;
  return 0;  
}


static int ccall_struct_arg(CCallState *cc, CTState *cts, CType *d, int *rcl,
			    TValue *o, int narg)
{
  GPRArg dp[2];
  dp[0] = dp[1] = 0;
  
  lj_cconv_ct_tv(cts, d, (uint8_t *)dp, o, CCF_ARG(narg));
  if (ccall_struct_reg(cc, cts, dp, rcl)) {
    
    MSize nsp = cc->nsp, sz = rcl[1] ? 2*CTSIZE_PTR : CTSIZE_PTR;
    MSize align = (1u << ctype_align(d->info)) - 1;
    if (nsp + sz > CCALL_SIZE_STACK)
      return 1;  
    if (CCALL_ALIGN_STACKARG && align > CTSIZE_PTR-1)
      nsp = (nsp + align) & ~align;  
    cc->nsp = nsp + sz;
    memcpy((uint8_t *)cc->stack + nsp, dp, sz);
  }
  return 0;  
}


static void ccall_struct_ret(CCallState *cc, int *rcl, uint8_t *dp, CTSize sz)
{
  GPRArg sp[2];
  MSize ngpr = 0, nfpr = 0;
  uint32_t i;
  for (i = 0; i < 2; i++) {
    if ((rcl[i] & CCALL_RCL_INT)) {  
      sp[i] = cc->gpr[ngpr++];
    } else if ((rcl[i] & CCALL_RCL_SSE)) {
      sp[i] = cc->fpr[nfpr++].l[0];
    }
  }
  memcpy(dp, sp, sz);
}
#endif



#if LJ_TARGET_ARM && !LJ_ABI_SOFTFP


static unsigned int ccall_classify_struct(CTState *cts, CType *ct, CType *ctf)
{
  CTSize sz = ct->size;
  unsigned int r = 0, n = 0, isu = (ct->info & CTF_UNION);
  if ((ctf->info & CTF_VARARG)) goto noth;
  while (ct->sib) {
    CType *sct;
    ct = ctype_get(cts, ct->sib);
    if (ctype_isfield(ct->info)) {
      sct = ctype_rawchild(cts, ct);
      if (ctype_isfp(sct->info)) {
	r |= sct->size;
	if (!isu) n++; else if (n == 0) n = 1;
      } else if (ctype_iscomplex(sct->info)) {
	r |= (sct->size >> 1);
	if (!isu) n += 2; else if (n < 2) n = 2;
      } else if (ctype_isstruct(sct->info)) {
	goto substruct;
      } else {
	goto noth;
      }
    } else if (ctype_isbitfield(ct->info)) {
      goto noth;
    } else if (ctype_isxattrib(ct->info, CTA_SUBTYPE)) {
      sct = ctype_rawchild(cts, ct);
    substruct:
      if (sct->size > 0) {
	unsigned int s = ccall_classify_struct(cts, sct, ctf);
	if (s <= 1) goto noth;
	r |= (s & 255);
	if (!isu) n += (s >> 8); else if (n < (s >>8)) n = (s >> 8);
      }
    }
  }
  if ((r == 4 || r == 8) && n <= 4)
    return r + (n << 8);
noth:  
  return (sz <= 4);  
}

#endif



#if LJ_TARGET_ARM64

#if !LJ_TARGET_OSX

static CTInfo ccall_struct_align_arm64(CTState *cts, CType *ct)
{
  CTSize sz;
  if (ct->sib) {
    while (ct->sib) {
      ct = ctype_get(cts, ct->sib);
      if (ctype_isfield(ct->info)) {
	if ((ct->info & CTF_ALIGN) > CTALIGN_PTR) return CTALIGN(4);
      } else if (ctype_isxattrib(ct->info, CTA_SUBTYPE)) {
	CType *sct = ctype_rawchild(cts, ct);
	CTInfo info = lj_ctype_info(cts, ctype_typeid(cts, sct), &sz);
	if ((info & CTF_ALIGN) > CTALIGN_PTR) return CTALIGN(4);
      }
    }
  } else  {
    CTInfo info = lj_ctype_info(cts, ctype_typeid(cts, ct), &sz);
    if ((info & CTF_ALIGN) > CTALIGN_PTR) return CTALIGN(4);
  }
  return CTALIGN_PTR;
}
#define ccall_struct_align(cts, ct)	ccall_struct_align_arm64((cts), (ct))
#endif


static unsigned int ccall_classify_struct(CTState *cts, CType *ct)
{
  CTSize sz = ct->size;
  unsigned int r = 0, n = 0, isu = (ct->info & CTF_UNION);
  while (ct->sib && n <= 4) {
    unsigned int m = 1;
    CType *sct;
    ct = ctype_get(cts, ct->sib);
    if (ctype_isfield(ct->info)) {
      sct = ctype_rawchild(cts, ct);
      if (ctype_isarray(sct->info) && !sct->size) goto noth;
      while (ctype_isarray(sct->info)) {
	CType *cct = ctype_rawchild(cts, sct);
	m *= sct->size / cct->size;
	sct = cct;
      }
      if (ctype_isfp(sct->info)) {
	r |= sct->size;
	if (!isu) n += m; else if (n < m) n = m;
      } else if (ctype_iscomplex(sct->info)) {
	r |= (sct->size >> 1);
	if (!isu) n += 2*m; else if (n < 2*m) n = 2*m;
      } else if (ctype_isstruct(sct->info)) {
	goto substruct;
      } else {
	goto noth;
      }
    } else if (ctype_isbitfield(ct->info) && ctype_bitbsz(ct->info)) {
      goto noth;
    } else if (ctype_isxattrib(ct->info, CTA_SUBTYPE)) {
      sct = ctype_rawchild(cts, ct);
    substruct:
      if (sct->size > 0) {
	unsigned int s = ccall_classify_struct(cts, sct), sn;
	if (s <= 1) goto noth;
	r |= (s & 255);
	sn = (s >> 8) * m;
	if (!isu) n += sn; else if (n < sn) n = sn;
      }
    }
  }
  if ((r == 4 || r == 8) && n <= 4)
    return r + (n << 8);
noth:  
  return (sz <= 16);  
}

#endif



#if LJ_TARGET_MIPS64

#define FTYPE_FLOAT	1
#define FTYPE_DOUBLE	2


static unsigned int ccall_classify_struct(CTState *cts, CType *ct, CType *ctf)
{
  int n = 0, ft = 0;
  if ((ctf->info & CTF_VARARG) || (ct->info & CTF_UNION))
    goto noth;
  while (ct->sib) {
    CType *sct;
    ct = ctype_get(cts, ct->sib);
    if (n == 2) {
      goto noth;
    } else if (ctype_isfield(ct->info)) {
      sct = ctype_rawchild(cts, ct);
      if (ctype_isfp(sct->info)) {
	ft |= (sct->size == 4 ? FTYPE_FLOAT : FTYPE_DOUBLE) << 2*n;
	n++;
      } else {
	goto noth;
      }
    } else if (ctype_isbitfield(ct->info) ||
	       ctype_isxattrib(ct->info, CTA_SUBTYPE)) {
      goto noth;
    }
  }
  if (n <= 2)
    return ft;
noth:  
  return 0;  
}

static void ccall_copy_struct(CCallState *cc, CType *ctr, void *dp, void *sp,
			      int ft)
{
  if (LJ_ABI_SOFTFP ? ft :
      ((ft & 3) == FTYPE_FLOAT || (ft >> 2) == FTYPE_FLOAT)) {
    int i, ofs = 0;
    for (i = 0; ft != 0; i++, ft >>= 2) {
      if ((ft & 3) == FTYPE_FLOAT) {
#if LJ_ABI_SOFTFP
	
	memcpy((uint8_t *)dp + ofs,
	       (uint8_t *)&cc->gpr[2*i] + LJ_ENDIAN_SELECT(0, 4), 4);
#else
	*(float *)((uint8_t *)dp + ofs) = cc->fpr[i].f;
#endif
	ofs += 4;
      } else {
	ofs = (ofs + 7) & ~7;  
#if LJ_ABI_SOFTFP
	*(intptr_t *)((uint8_t *)dp + ofs) = cc->gpr[2*i];
#else
	*(double *)((uint8_t *)dp + ofs) = cc->fpr[i].d;
#endif
	ofs += 8;
      }
    }
  } else {
#if !LJ_ABI_SOFTFP
    if (ft) sp = (uint8_t *)&cc->fpr[0];
#endif
    memcpy(dp, sp, ctr->size);
  }
}

#endif

#ifndef ccall_struct_align

#define ccall_struct_align(cts, ct)	((ct)->info & CTF_ALIGN)
#endif




CTypeID lj_ccall_ctid_vararg(CTState *cts, cTValue *o)
{
  if (tvisnumber(o)) {
    return CTID_DOUBLE;
  } else if (tviscdata(o)) {
    CTypeID id = cdataV(o)->ctypeid;
    CType *s = ctype_get(cts, id);
    if (ctype_isrefarray(s->info)) {
      return lj_ctype_intern(cts,
	       CTINFO(CT_PTR, CTALIGN_PTR|ctype_cid(s->info)), CTSIZE_PTR);
    } else if (ctype_isstruct(s->info) || ctype_isfunc(s->info)) {
      
      return lj_ctype_intern(cts, CTINFO(CT_PTR, CTALIGN_PTR|id), CTSIZE_PTR);
    } else if (ctype_isfp(s->info) && s->size == sizeof(float)) {
      return CTID_DOUBLE;
    } else {
      return id;
    }
  } else if (tvisstr(o)) {
    return CTID_P_CCHAR;
  } else if (tvisbool(o)) {
    return CTID_BOOL;
  } else {
    return CTID_P_VOID;
  }
}


static int ccall_set_args(lua_State *L, CTState *cts, CType *ct,
			  CCallState *cc)
{
  int gcsteps = 0;
  TValue *o, *top = L->top;
  CTypeID fid;
  CTInfo info = ct->info;  
  CType *ctr;
  MSize maxgpr, ngpr = 0, nsp = 0, narg;
#if CCALL_NARG_FPR
  MSize nfpr = 0;
#if LJ_TARGET_ARM
  MSize fprodd = 0;
#endif
#endif

  
  memset(cc->gpr, 0, sizeof(cc->gpr));
#if CCALL_NUM_FPR
  memset(cc->fpr, 0, sizeof(cc->fpr));
#endif

#if LJ_TARGET_X86
  
  cc->resx87 = 0;
  switch (ctype_cconv(info)) {
  case CTCC_FASTCALL: maxgpr = 2; break;
  case CTCC_THISCALL: maxgpr = 1; break;
  default: maxgpr = 0; break;
  }
#else
  maxgpr = CCALL_NARG_GPR;
#endif

  
  ctr = ctype_rawchild(cts, ct);
  if (ctype_isvector(ctr->info)) {
    if (!(CCALL_VECTOR_REG && (ctr->size == 8 || ctr->size == 16)))
      goto err_nyi;
  } else if (ctype_iscomplex(ctr->info) || ctype_isstruct(ctr->info)) {
    
    CTSize sz = ctr->size;
    GCcdata *cd = lj_cdata_new(cts, ctype_cid(info), sz);
    void *dp = cdataptr(cd);
    setcdataV(L, L->top++, cd);
    if (ctype_isstruct(ctr->info)) {
      CCALL_HANDLE_STRUCTRET
    } else {
      CCALL_HANDLE_COMPLEXRET
    }
#if LJ_TARGET_X86
  } else if (ctype_isfp(ctr->info)) {
    cc->resx87 = ctr->size == sizeof(float) ? 1 : 2;
#endif
  }

  
  fid = ct->sib;
  while (fid) {
    CType *ctf = ctype_get(cts, fid);
    if (!ctype_isattrib(ctf->info)) break;
    fid = ctf->sib;
  }

#if LJ_TARGET_ARM64 && LJ_ABI_WIN
  if ((info & CTF_VARARG)) {
    nsp -= maxgpr * CTSIZE_PTR;  
    ngpr = maxgpr;
    nfpr = CCALL_NARG_FPR;
  }
#endif

  
  for (o = L->base+1, narg = 1; o < top; o++, narg++) {
    CTypeID did;
    CType *d;
    CTSize sz;
    MSize n, isfp = 0, isva = 0;
    void *dp, *rp = NULL;
#if LJ_TARGET_X64 && !LJ_ABI_WIN
    int onstack = 0;
#endif

    if (fid) {  
      CType *ctf = ctype_get(cts, fid);
      fid = ctf->sib;
      lj_assertL(ctype_isfield(ctf->info), "field expected");
      did = ctype_cid(ctf->info);
    } else {
      if (!(info & CTF_VARARG))
	lj_err_caller(L, LJ_ERR_FFI_NUMARG);  
      did = lj_ccall_ctid_vararg(cts, o);  
      isva = 1;
    }
    d = ctype_raw(cts, did);
    sz = d->size;

    
    if (ctype_isnum(d->info)) {
      if (sz > 8) goto err_nyi;
      if ((d->info & CTF_FP))
	isfp = 1;
    } else if (ctype_isvector(d->info)) {
      if (CCALL_VECTOR_REG && (sz == 8 || sz == 16))
	isfp = 1;
      else
	goto err_nyi;
    } else if (ctype_isstruct(d->info)) {
      CCALL_HANDLE_STRUCTARG
    } else if (ctype_iscomplex(d->info)) {
      CCALL_HANDLE_COMPLEXARG
    } else if (!(CCALL_PACK_STACKARG && ctype_isenum(d->info))) {
      sz = CTSIZE_PTR;
    }
    n = (sz + CTSIZE_PTR-1) / CTSIZE_PTR;  

    CCALL_HANDLE_REGARG  

    
    if (CCALL_ALIGN_STACKARG) {  
      MSize align = (1u << ctype_align(ccall_struct_align(cts, d))) - 1;
#if LJ_TARGET_ARM64 && LJ_TARGET_OSX
      isva |= ctype_isstruct(d->info);
#endif
      if (rp || (CCALL_PACK_STACKARG && isva && align < CTSIZE_PTR-1))
	align = CTSIZE_PTR-1;
      nsp = (nsp + align) & ~align;
    }
#if LJ_TARGET_ARM64 && LJ_ABI_WIN
    
    dp = ((uint8_t *)cc->stack) + (int32_t)nsp;
#else
    dp = ((uint8_t *)cc->stack) + nsp;
#endif
    nsp += CCALL_PACK_STACKARG ? sz : n * CTSIZE_PTR;
    if ((int32_t)nsp > CCALL_SIZE_STACK) {  
    err_nyi:
      lj_err_caller(L, LJ_ERR_FFI_NYICALL);
    }
    isva = 0;

  done:
    if (rp) {  
      gcsteps++;
      *(void **)dp = rp;
      dp = rp;
    }
    lj_cconv_ct_tv(cts, d, (uint8_t *)dp, o, CCF_ARG(narg));
    
    if (ctype_isinteger_or_bool(d->info) && d->size < 4 &&
	(!CCALL_PACK_STACKARG || !((uintptr_t)dp & 3))) {  
      if (d->info & CTF_UNSIGNED)
	*(uint32_t *)dp = d->size == 1 ? (uint32_t)*(uint8_t *)dp :
					 (uint32_t)*(uint16_t *)dp;
      else
	*(int32_t *)dp = d->size == 1 ? (int32_t)*(int8_t *)dp :
					(int32_t)*(int16_t *)dp;
    }
#if LJ_TARGET_ARM64 && LJ_BE
    if (isfp && d->size == sizeof(float))
      ((float *)dp)[1] = ((float *)dp)[0];  
#endif
#if LJ_TARGET_MIPS64 || (LJ_TARGET_ARM64 && LJ_BE)
    if ((ctype_isinteger_or_bool(d->info) || ctype_isenum(d->info)
#if LJ_TARGET_MIPS64
	 || (isfp && nsp == 0)
#endif
	 ) && d->size <= 4) {
      *(int64_t *)dp = (int64_t)*(int32_t *)dp;  
    }
#endif
#if LJ_TARGET_X64 && LJ_ABI_WIN
    if (isva) {  
      if (nfpr == ngpr)
	cc->gpr[ngpr-1] = cc->fpr[ngpr-1].l[0];
      else
	cc->fpr[ngpr-1].l[0] = cc->gpr[ngpr-1];
    }
#else
    UNUSED(isva);
#endif
#if LJ_TARGET_X64 && !LJ_ABI_WIN
    if (isfp == 2 && n == 2 && (uint8_t *)dp == (uint8_t *)&cc->fpr[nfpr-2]) {
      cc->fpr[nfpr-1].d[0] = cc->fpr[nfpr-2].d[1];  
      cc->fpr[nfpr-2].d[1] = 0;
    }
#elif LJ_TARGET_ARM64 || (LJ_TARGET_MIPS64 && !LJ_ABI_SOFTFP)
    if (isfp == 2 && (uint8_t *)dp < (uint8_t *)cc->stack) {
      
      CTSize i = (sz >> 2) - 1;
      do { ((uint64_t *)dp)[i] = ((uint32_t *)dp)[i]; } while (i--);
    }
#else
    UNUSED(isfp);
#endif
  }
  if (fid) lj_err_caller(L, LJ_ERR_FFI_NUMARG);  
#if LJ_TARGET_ARM64 && LJ_ABI_WIN
  if ((int32_t)nsp < 0) nsp = 0;
#endif

#if LJ_TARGET_X64 || (LJ_TARGET_PPC && !LJ_ABI_SOFTFP)
  cc->nfpr = nfpr;  
#endif
  cc->nsp = (nsp + CTSIZE_PTR-1) & ~(CTSIZE_PTR-1);
  cc->spadj = (CCALL_SPS_FREE + CCALL_SPS_EXTRA) * CTSIZE_PTR;
  if (cc->nsp > CCALL_SPS_FREE * CTSIZE_PTR)
    cc->spadj += (((cc->nsp - CCALL_SPS_FREE * CTSIZE_PTR) + 15u) & ~15u);
  return gcsteps;
}


static int ccall_get_results(lua_State *L, CTState *cts, CType *ct,
			     CCallState *cc, int *ret)
{
  CType *ctr = ctype_rawchild(cts, ct);
  uint8_t *sp = (uint8_t *)&cc->gpr[0];
  if (ctype_isvoid(ctr->info)) {
    *ret = 0;  
    return 0;  
  }
  *ret = 1;  
  if (ctype_isstruct(ctr->info)) {
    
    if (!cc->retref) {
      void *dp = cdataptr(cdataV(L->top-1));  
      CCALL_HANDLE_STRUCTRET2
    }
    return 1;  
  }
  if (ctype_iscomplex(ctr->info)) {
    
    void *dp = cdataptr(cdataV(L->top-1));  
    CCALL_HANDLE_COMPLEXRET2
    return 1;  
  }
  if (LJ_BE && ctr->size < CTSIZE_PTR &&
      (ctype_isinteger_or_bool(ctr->info) || ctype_isenum(ctr->info)))
    sp += (CTSIZE_PTR - ctr->size);
#if CCALL_NUM_FPR
  if (ctype_isfp(ctr->info) || ctype_isvector(ctr->info))
    sp = (uint8_t *)&cc->fpr[0];
#endif
#ifdef CCALL_HANDLE_RET
  CCALL_HANDLE_RET
#endif
  
  lj_assertL(!(ctype_isrefarray(ctr->info) || ctype_isstruct(ctr->info)),
	     "unexpected reference ctype");
  return lj_cconv_tv_ct(cts, ctr, 0, L->top-1, sp);
}


int lj_ccall_func(lua_State *L, GCcdata *cd)
{
  CTState *cts = ctype_cts(L);
  CType *ct = ctype_raw(cts, cd->ctypeid);
  CTSize sz = CTSIZE_PTR;
  if (ctype_isptr(ct->info)) {
    sz = ct->size;
    ct = ctype_rawchild(cts, ct);
  }
  if (ctype_isfunc(ct->info)) {
    CTypeID id = ctype_typeid(cts, ct);
    CCallState cc;
    int gcsteps, ret;
    cc.func = (void (*)(void))cdata_getptr(cdataptr(cd), sz);
    gcsteps = ccall_set_args(L, cts, ct, &cc);
    cts->cb.slot = ~0u;
    lj_vm_ffi_call(&cc);
    if (cts->cb.slot != ~0u) {  
      TValue tv;
      tv.u64 = ((uintptr_t)(void *)cc.func >> 2) | U64x(800000000, 00000000);
      setboolV(lj_tab_set(L, cts->miscmap, &tv), 1);
    }
    ct = ctype_get(cts, id);  
    gcsteps += ccall_get_results(L, cts, ct, &cc, &ret);
#if LJ_TARGET_X86 && LJ_ABI_WIN
    
    if (cc.spadj && ctype_cconv(ct->info) == CTCC_CDECL) {
      CTF_INSERT(ct->info, CCONV, CTCC_STDCALL);
      lj_trace_abort(G(L));
    }
#endif
    while (gcsteps-- > 0)
      lj_gc_check(L);
    return ret;
  }
  return -1;  
}

#endif
