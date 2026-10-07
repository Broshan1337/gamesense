

#ifndef _LJ_FF_H
#define _LJ_FF_H


typedef enum {
  FF_LUA_ = FF_LUA,	
  FF_C_ = FF_C,		
#define FFDEF(name)	FF_##name,
#include "lj_ffdef.h"
  FF__MAX
} FastFunc;

#endif
