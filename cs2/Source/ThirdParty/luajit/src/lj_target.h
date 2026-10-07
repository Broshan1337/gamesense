

#ifndef _LJ_TARGET_H
#define _LJ_TARGET_H

#include "lj_def.h"
#include "lj_arch.h"




typedef uint32_t Reg;


#define RID_NONE		0x80
#define RID_MASK		0x7f
#define RID_INIT		(RID_NONE|RID_MASK)
#define RID_SINK		(RID_INIT-1)
#define RID_SUNK		(RID_INIT-2)

#define ra_noreg(r)		((r) & RID_NONE)
#define ra_hasreg(r)		(!((r) & RID_NONE))


#define ra_hashint(r)		((r) < RID_SUNK)
#define ra_gethint(r)		((Reg)((r) & RID_MASK))
#define ra_sethint(rr, r)	rr = (uint8_t)((r)|RID_NONE)
#define ra_samehint(r1, r2)	(ra_gethint((r1)^(r2)) == 0)


#define SPS_NONE		0

#define ra_hasspill(s)		((s) != SPS_NONE)


typedef uint32_t RegSP;

#define REGSP(r, s)		((r) + ((s) << 8))
#define REGSP_HINT(r)		((r)|RID_NONE)
#define REGSP_INIT		REGSP(RID_INIT, 0)

#define regsp_reg(rs)		((rs) & 255)
#define regsp_spill(rs)		((rs) >> 8)
#define regsp_used(rs) \
  (((rs) & ~REGSP(RID_MASK, 0)) != REGSP(RID_NONE, 0))




#if LJ_TARGET_PPC || LJ_TARGET_MIPS || LJ_TARGET_ARM64
typedef uint64_t RegSet;
#define RSET_BITS		6
#define rset_picktop_(rs)	((Reg)lj_fls64(rs))
#define rset_pickbot_(rs)	((Reg)lj_ffs64(rs))
#else
typedef uint32_t RegSet;
#define RSET_BITS		5
#define rset_picktop_(rs)	((Reg)lj_fls(rs))
#define rset_pickbot_(rs)	((Reg)lj_ffs(rs))
#endif

#define RID2RSET(r)		(((RegSet)1) << (r))
#define RSET_EMPTY		((RegSet)0)
#define RSET_RANGE(lo, hi)	((RID2RSET((hi)-(lo))-1) << (lo))

#define rset_test(rs, r)	((int)((rs) >> (r)) & 1)
#define rset_set(rs, r)		(rs |= RID2RSET(r))
#define rset_clear(rs, r)	(rs &= ~RID2RSET(r))
#define rset_exclude(rs, r)	(rs & ~RID2RSET(r))




#define REGCOST_PHI_WEIGHT	64


typedef uint32_t RegCost;


#define REGCOST(cost, ref)	((RegCost)(ref) + ((RegCost)(cost) << 16))
#define regcost_ref(rc)		((IRRef1)(rc))

#define REGCOST_T(t) \
  ((RegCost)((t)&IRT_ISPHI) * (((RegCost)(REGCOST_PHI_WEIGHT)<<16)/IRT_ISPHI))
#define REGCOST_REF_T(ref, t)	(REGCOST((ref), (ref)) + REGCOST_T((t)))



#if LJ_TARGET_X86ORX64
#include "lj_target_x86.h"
#elif LJ_TARGET_ARM
#include "lj_target_arm.h"
#elif LJ_TARGET_ARM64
#include "lj_target_arm64.h"
#elif LJ_TARGET_PPC
#include "lj_target_ppc.h"
#elif LJ_TARGET_MIPS
#include "lj_target_mips.h"
#else
#error "Missing include for target CPU"
#endif

#ifdef EXITSTUBS_PER_GROUP

static LJ_AINLINE char *exitstub_addr_(char **group, uint32_t exitno)
{
  lj_assertX(group[exitno / EXITSTUBS_PER_GROUP] != NULL,
	     "exit stub group for exit %d uninitialized", exitno);
  return (char *)group[exitno / EXITSTUBS_PER_GROUP] +
	 EXITSTUB_SPACING*(exitno % EXITSTUBS_PER_GROUP);
}

#define exitstub_addr(J, exitno) \
  ((MCode *)exitstub_addr_((char **)((J)->exitstubgroup), (exitno)))
#endif

#endif
