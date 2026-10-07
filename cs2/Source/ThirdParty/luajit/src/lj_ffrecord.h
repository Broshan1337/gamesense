

#ifndef _LJ_FFRECORD_H
#define _LJ_FFRECORD_H

#include "lj_obj.h"
#include "lj_jit.h"

#if LJ_HASJIT

typedef struct RecordFFData {
  TValue *argv;		
  ptrdiff_t nres;	
  uint32_t data;	
} RecordFFData;

LJ_FUNC int32_t lj_ffrecord_select_mode(jit_State *J, TRef tr, TValue *tv);
LJ_FUNC void lj_ffrecord_func(jit_State *J);
#endif

#endif
