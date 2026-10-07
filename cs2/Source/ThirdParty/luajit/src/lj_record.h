

#ifndef _LJ_RECORD_H
#define _LJ_RECORD_H

#include "lj_obj.h"
#include "lj_jit.h"

#if LJ_HASJIT

typedef struct RecordIndex {
  TValue tabv;		
  TValue keyv;		
  TValue valv;		
  TValue mobjv;		
  GCtab *mtv;		
  cTValue *oldv;	
  TRef tab;		
  TRef key;		
  TRef val;		
  TRef mt;		
  TRef mobj;		
  int idxchain;		
} RecordIndex;

LJ_FUNC int lj_record_objcmp(jit_State *J, TRef a, TRef b,
			     cTValue *av, cTValue *bv);
LJ_FUNC void lj_record_stop(jit_State *J, TraceLink linktype, TraceNo lnk);
LJ_FUNC TRef lj_record_constify(jit_State *J, cTValue *o);
LJ_FUNC TRef lj_record_vload(jit_State *J, TRef ref, MSize idx, IRType t);

LJ_FUNC void lj_record_call(jit_State *J, BCReg func, ptrdiff_t nargs);
LJ_FUNC void lj_record_tailcall(jit_State *J, BCReg func, ptrdiff_t nargs);
LJ_FUNC void lj_record_ret(jit_State *J, BCReg rbase, ptrdiff_t gotresults);

LJ_FUNC int lj_record_mm_lookup(jit_State *J, RecordIndex *ix, MMS mm);
LJ_FUNC TRef lj_record_idx(jit_State *J, RecordIndex *ix);
LJ_FUNC int lj_record_next(jit_State *J, RecordIndex *ix);

LJ_FUNC void lj_record_ins(jit_State *J);
LJ_FUNC void lj_record_setup(jit_State *J);
#endif

#endif
