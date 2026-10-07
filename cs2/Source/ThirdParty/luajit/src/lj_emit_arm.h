



static uint8_t emit_invai[16] = {
   (ARMI_AND^ARMI_BIC) >> 21,
   0,
   (ARMI_SUB^ARMI_ADD) >> 21,
   0,
   (ARMI_ADD^ARMI_SUB) >> 21,
   (ARMI_ADC^ARMI_SBC) >> 21,
   (ARMI_SBC^ARMI_ADC) >> 21,
   0,
   0,
   0,
   (ARMI_CMP^ARMI_CMN) >> 21,
   (ARMI_CMN^ARMI_CMP) >> 21,
   0,
   (ARMI_MOV^ARMI_MVN) >> 21,
   (ARMI_BIC^ARMI_AND) >> 21,
   (ARMI_MVN^ARMI_MOV) >> 21
};


static uint32_t emit_isk12(ARMIns ai, int32_t n)
{
  uint32_t invai, i, m = (uint32_t)n;
  
  for (i = 0; i < 4096; i += 256, m = lj_rol(m, 2))
    if (m <= 255) return ARMI_K12|m|i;
  
  invai = emit_invai[((ai >> 21) & 15)];
  if (!invai) return 0;  
  m = ~(uint32_t)n;
  if (invai == ((ARMI_SUB^ARMI_ADD) >> 21) ||
      invai == (ARMI_CMP^ARMI_CMN) >> 21) m++;
  for (i = 0; i < 4096; i += 256, m = lj_rol(m, 2))
    if (m <= 255) return ARMI_K12|(invai<<21)|m|i;
  return 0;  
}



static void emit_dnm(ASMState *as, ARMIns ai, Reg rd, Reg rn, Reg rm)
{
  *--as->mcp = ai | ARMF_D(rd) | ARMF_N(rn) | ARMF_M(rm);
}

static void emit_dm(ASMState *as, ARMIns ai, Reg rd, Reg rm)
{
  *--as->mcp = ai | ARMF_D(rd) | ARMF_M(rm);
}

static void emit_dn(ASMState *as, ARMIns ai, Reg rd, Reg rn)
{
  *--as->mcp = ai | ARMF_D(rd) | ARMF_N(rn);
}

static void emit_nm(ASMState *as, ARMIns ai, Reg rn, Reg rm)
{
  *--as->mcp = ai | ARMF_N(rn) | ARMF_M(rm);
}

static void emit_d(ASMState *as, ARMIns ai, Reg rd)
{
  *--as->mcp = ai | ARMF_D(rd);
}

static void emit_n(ASMState *as, ARMIns ai, Reg rn)
{
  *--as->mcp = ai | ARMF_N(rn);
}

static void emit_m(ASMState *as, ARMIns ai, Reg rm)
{
  *--as->mcp = ai | ARMF_M(rm);
}

static void emit_lsox(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lj_assertA(ofs >= -255 && ofs <= 255,
	     "load/store offset %d out of range", ofs);
  if (ofs < 0) ofs = -ofs; else ai |= ARMI_LS_U;
  *--as->mcp = ai | ARMI_LS_P | ARMI_LSX_I | ARMF_D(rd) | ARMF_N(rn) |
	       ((ofs & 0xf0) << 4) | (ofs & 0x0f);
}

static void emit_lso(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lj_assertA(ofs >= -4095 && ofs <= 4095,
	     "load/store offset %d out of range", ofs);
  
  if (*as->mcp == (ai|ARMI_LS_P|ARMI_LS_U|ARMF_D(rd^1)|ARMF_N(rn)|(ofs^4)) &&
      (ai & ~(ARMI_LDR^ARMI_STR)) == ARMI_STR && rd != rn &&
      (uint32_t)ofs <= 252 && !(ofs & 3) && !((rd ^ (ofs >>2)) & 1) &&
      as->mcp != as->mcloop) {
    as->mcp++;
    emit_lsox(as, ai == ARMI_LDR ? ARMI_LDRD : ARMI_STRD, rd&~1, rn, ofs&~4);
    return;
  }
  if (ofs < 0) ofs = -ofs; else ai |= ARMI_LS_U;
  *--as->mcp = ai | ARMI_LS_P | ARMF_D(rd) | ARMF_N(rn) | ofs;
}

#if !LJ_SOFTFP
static void emit_vlso(ASMState *as, ARMIns ai, Reg rd, Reg rn, int32_t ofs)
{
  lj_assertA(ofs >= -1020 && ofs <= 1020 && (ofs&3) == 0,
	     "load/store offset %d out of range", ofs);
  if (ofs < 0) ofs = -ofs; else ai |= ARMI_LS_U;
  *--as->mcp = ai | ARMI_LS_P | ARMF_D(rd & 15) | ARMF_N(rn) | (ofs >> 2);
}
#endif




#define emit_canremat(ref)	((ref) < ASMREF_L)


static int emit_kdelta1(ASMState *as, Reg d, int32_t i)
{
  RegSet work = ~as->freeset & RSET_GPR;
  while (work) {
    Reg r = rset_picktop(work);
    IRRef ref = regcost_ref(as->cost[r]);
    lj_assertA(r != d, "dest reg not free");
    if (emit_canremat(ref)) {
      int32_t delta = i - (ra_iskref(ref) ? ra_krefk(as, ref) : IR(ref)->i);
      uint32_t k = emit_isk12(ARMI_ADD, delta);
      if (k) {
	if (k == ARMI_K12)
	  emit_dm(as, ARMI_MOV, d, r);
	else
	  emit_dn(as, ARMI_ADD^k, d, r);
	return 1;
      }
    }
    rset_clear(work, r);
  }
  return 0;  
}


static int emit_kdelta2(ASMState *as, Reg rd, int32_t i)
{
  RegSet work = ~as->freeset & RSET_GPR;
  while (work) {
    Reg r = rset_picktop(work);
    IRRef ref = regcost_ref(as->cost[r]);
    lj_assertA(r != rd, "dest reg %d not free", rd);
    if (emit_canremat(ref)) {
      int32_t other = ra_iskref(ref) ? ra_krefk(as, ref) : IR(ref)->i;
      if (other) {
	int32_t delta = i - other;
	uint32_t sh, inv = 0, k2, k;
	if (delta < 0) { delta = (int32_t)(~(uint32_t)delta+1u); inv = ARMI_ADD^ARMI_SUB; }
	sh = lj_ffs(delta) & ~1;
	k2 = emit_isk12(0, delta & (255 << sh));
	k = emit_isk12(0, delta & ~(255 << sh));
	if (k) {
	  emit_dn(as, ARMI_ADD^k2^inv, rd, rd);
	  emit_dn(as, ARMI_ADD^k^inv, rd, r);
	  return 1;
	}
      }
    }
    rset_clear(work, r);
  }
  return 0;  
}

#define emit_movw_k(k) \
  (ARMI_MOVW | ((k) & 0x0fffu) | (((k) & 0xf000u) << 4))
#define emit_movt_k(k) \
  (ARMI_MOVT | (((k) >> 16) & 0x0fffu) | ((((k) >> 16) & 0xf000u) << 4))


static void emit_loadi(ASMState *as, Reg rd, int32_t i)
{
  uint32_t k = emit_isk12(ARMI_MOV, i);
  lj_assertA(rset_test(as->freeset, rd) || rd == RID_TMP,
	     "dest reg %d not free", rd);
  if (k) {
    
    emit_d(as, ARMI_MOV^k, rd);
  } else if ((as->flags & JIT_F_ARMV6T2) && (uint32_t)i < 0x00010000u) {
    
    emit_d(as, emit_movw_k(i), rd);
  } else if (emit_kdelta1(as, rd, i)) {
    
  } else if ((as->flags & JIT_F_ARMV6T2)) {
    
    emit_d(as, emit_movt_k(i), rd);
    emit_d(as, emit_movw_k(i), rd);
  } else if (emit_kdelta2(as, rd, i)) {
    
  } else {
    
    
    for (;;) {
      uint32_t sh = lj_ffs(i) & ~1;
      int32_t m = i & (255 << sh);
      i &= ~(255 << sh);
      if (i == 0) {
	emit_d(as, ARMI_MOV ^ emit_isk12(0, m), rd);
	break;
      }
      emit_dn(as, ARMI_ORR ^ emit_isk12(0, m), rd, rd);
    }
  }
}

#define emit_loada(as, rd, addr)	emit_loadi(as, (rd), i32ptr((addr)))

static Reg ra_allock(ASMState *as, intptr_t k, RegSet allow);


static void emit_lsptr(ASMState *as, ARMIns ai, Reg r, void *p)
{
  int32_t i = i32ptr(p);
  emit_lso(as, ai, r, ra_allock(as, (i & ~4095), rset_exclude(RSET_GPR, r)),
	   (i & 4095));
}

#if !LJ_SOFTFP

static void emit_loadk64(ASMState *as, Reg r, IRIns *ir)
{
  cTValue *tv = ir_knum(ir);
  int32_t i;
  if ((as->flags & JIT_F_VFPV3) && !tv->u32.lo) {
    uint32_t hi = tv->u32.hi;
    uint32_t b = ((hi >> 22) & 0x1ff);
    if (!(hi & 0xffff) && (b == 0x100 || b == 0x0ff)) {
      *--as->mcp = ARMI_VMOVI_D | ARMF_D(r & 15) |
		   ((tv->u32.hi >> 12) & 0x00080000) |
		   ((tv->u32.hi >> 4) & 0x00070000) |
		   ((tv->u32.hi >> 16) & 0x0000000f);
      return;
    }
  }
  i = i32ptr(tv);
  emit_vlso(as, ARMI_VLDR_D, r,
	    ra_allock(as, (i & ~1020), RSET_GPR), (i & 1020));
}
#endif


#define emit_getgl(as, r, field) \
  emit_lsptr(as, ARMI_LDR, (r), (void *)&J2G(as->J)->field)
#define emit_setgl(as, r, field) \
  emit_lsptr(as, ARMI_STR, (r), (void *)&J2G(as->J)->field)


#define emit_setvmstate(as, i)		UNUSED(i)




typedef MCode *MCLabel;


#define emit_label(as)		((as)->mcp)

static void emit_branch(ASMState *as, ARMIns ai, MCode *target)
{
  MCode *p = as->mcp;
  ptrdiff_t delta = (target - p) - 1;
  lj_assertA(((delta + 0x00800000) >> 24) == 0, "branch target out of range");
  *--p = ai | ((uint32_t)delta & 0x00ffffffu);
  as->mcp = p;
}

#define emit_jmp(as, target) emit_branch(as, ARMI_B, (target))

static void emit_call(ASMState *as, void *target)
{
  MCode *p = --as->mcp;
  ptrdiff_t delta = ((char *)target - (char *)p) - 8;
  if ((((delta>>2) + 0x00800000) >> 24) == 0) {
    if ((delta & 1))
      *p = ARMI_BLX | ((uint32_t)(delta>>2) & 0x00ffffffu) | ((delta&2) << 23);
    else
      *p = ARMI_BL | ((uint32_t)(delta>>2) & 0x00ffffffu);
  } else {  
    Reg r = ra_allock(as, i32ptr(target), RSET_RANGE(RID_R4, RID_R12+1));
    *p = ARMI_BLXr | ARMF_M(r);
  }
}




static void emit_movrr(ASMState *as, IRIns *ir, Reg dst, Reg src)
{
#if LJ_SOFTFP
  lj_assertA(!irt_isnum(ir->t), "unexpected FP op"); UNUSED(ir);
#else
  if (dst >= RID_MAX_GPR) {
    emit_dm(as, irt_isnum(ir->t) ? ARMI_VMOV_D : ARMI_VMOV_S,
	    (dst & 15), (src & 15));
    return;
  }
#endif
  if (as->mcp != as->mcloop) {  
    MCode ins = *as->mcp, swp = (src^dst);
    if ((ins & 0x0c000000) == 0x04000000 && (ins & 0x02000010) != 0x02000010) {
      if (!((ins ^ (dst << 16)) & 0x000f0000))
	*as->mcp = ins ^ (swp << 16);  
      if (!(ins & 0x00100000) && !((ins ^ (dst << 12)) & 0x0000f000))
	*as->mcp = ins ^ (swp << 12);  
    }
  }
  emit_dm(as, ARMI_MOV, dst, src);
}


static void emit_loadofs(ASMState *as, IRIns *ir, Reg r, Reg base, int32_t ofs)
{
#if LJ_SOFTFP
  lj_assertA(!irt_isnum(ir->t), "unexpected FP op"); UNUSED(ir);
#else
  if (r >= RID_MAX_GPR)
    emit_vlso(as, irt_isnum(ir->t) ? ARMI_VLDR_D : ARMI_VLDR_S, r, base, ofs);
  else
#endif
    emit_lso(as, ARMI_LDR, r, base, ofs);
}


static void emit_storeofs(ASMState *as, IRIns *ir, Reg r, Reg base, int32_t ofs)
{
#if LJ_SOFTFP
  lj_assertA(!irt_isnum(ir->t), "unexpected FP op"); UNUSED(ir);
#else
  if (r >= RID_MAX_GPR)
    emit_vlso(as, irt_isnum(ir->t) ? ARMI_VSTR_D : ARMI_VSTR_S, r, base, ofs);
  else
#endif
    emit_lso(as, ARMI_STR, r, base, ofs);
}


static void emit_opk(ASMState *as, ARMIns ai, Reg dest, Reg src,
		     int32_t i, RegSet allow)
{
  uint32_t k = emit_isk12(ai, i);
  if (k)
    emit_dn(as, ai^k, dest, src);
  else
    emit_dnm(as, ai, dest, src, ra_allock(as, i, allow));
}


static void emit_addptr(ASMState *as, Reg r, int32_t ofs)
{
  if (ofs)
    emit_opk(as, ARMI_ADD, r, r, ofs, rset_exclude(RSET_GPR, r));
}

#define emit_spsub(as, ofs)	emit_addptr(as, RID_SP, -(ofs))

