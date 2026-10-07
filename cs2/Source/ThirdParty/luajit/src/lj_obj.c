

#define lj_obj_c
#define LUA_CORE

#include "lj_obj.h"


LJ_DATADEF const char *const lj_obj_typename[] = {  
  "no value", "nil", "boolean", "userdata", "number", "string",
  "table", "function", "userdata", "thread", "proto", "cdata"
};

LJ_DATADEF const char *const lj_obj_itypename[] = {  
  "nil", "boolean", "boolean", "userdata", "string", "upval", "thread",
  "proto", "function", "trace", "cdata", "table", "userdata", "number"
};


int LJ_FASTCALL lj_obj_equal(cTValue *o1, cTValue *o2)
{
  if (itype(o1) == itype(o2)) {
    if (tvispri(o1))
      return 1;
    if (!tvisnum(o1))
      return gcrefeq(o1->gcr, o2->gcr);
  } else if (!tvisnumber(o1) || !tvisnumber(o2)) {
    return 0;
  }
  return numberVnum(o1) == numberVnum(o2);
}


const void * LJ_FASTCALL lj_obj_ptr(global_State *g, cTValue *o)
{
  UNUSED(g);
  if (tvisudata(o))
    return uddata(udataV(o));
  else if (tvislightud(o))
    return lightudV(g, o);
  else if (LJ_HASFFI && tviscdata(o))
    return cdataptr(cdataV(o));
  else if (tvisgcv(o))
    return gcV(o);
  else
    return NULL;
}

