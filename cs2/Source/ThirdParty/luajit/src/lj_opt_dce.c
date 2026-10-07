

#define lj_opt_dce_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASJIT

#include "lj_ir.h"
#include "lj_jit.h"
#include "lj_iropt.h"


#define IR(ref)		(&J->cur.ir[(ref)])


static void dce_marksnap(jit_State *J)
{
  SnapNo i, nsnap = J->cur.nsnap;
  for (i = 0; i < nsnap; i++) {
    SnapShot *snap = &J->cur.snap[i];
    SnapEntry *map = &J->cur.snapmap[snap->mapofs];
    MSize n, nent = snap->nent;
    for (n = 0; n < nent; n++) {
      IRRef ref = snap_ref(map[n]);
      if (ref >= REF_FIRST)
	irt_setmark(IR(ref)->t);
    }
  }
}


static void dce_propagate(jit_State *J)
{
  IRRef1 *pchain[IR__MAX];
  IRRef ins;
  uint32_t i;
  for (i = 0; i < IR__MAX; i++) pchain[i] = &J->chain[i];
  for (ins = J->cur.nins-1; ins >= REF_FIRST; ins--) {
    IRIns *ir = IR(ins);
    if (irt_ismarked(ir->t)) {
      irt_clearmark(ir->t);
    } else if (!ir_sideeff(ir)) {
      *pchain[ir->o] = ir->prev;  
      lj_ir_nop(ir);
      continue;
    }
    pchain[ir->o] = &ir->prev;
    if (ir->op1 >= REF_FIRST) irt_setmark(IR(ir->op1)->t);
    if (ir->op2 >= REF_FIRST) irt_setmark(IR(ir->op2)->t);
  }
}


void lj_opt_dce(jit_State *J)
{
  if ((J->flags & JIT_F_OPT_DCE)) {
    dce_marksnap(J);
    dce_propagate(J);
    memset(J->bpropcache, 0, sizeof(J->bpropcache));  
  }
}

#undef IR

#endif
