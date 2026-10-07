

#ifndef _LJ_CPARSE_H
#define _LJ_CPARSE_H

#include "lj_obj.h"
#include "lj_ctype.h"

#if LJ_HASFFI


#define CPARSE_MAX_BUF		32768	
#define CPARSE_MAX_DECLSTACK	100	
#define CPARSE_MAX_DECLDEPTH	20	
#define CPARSE_MAX_PACKSTACK	7	


#define CPARSE_MODE_MULTI	1	
#define CPARSE_MODE_ABSTRACT	2	
#define CPARSE_MODE_DIRECT	4	
#define CPARSE_MODE_FIELD	8	
#define CPARSE_MODE_NOIMPLICIT	16	
#define CPARSE_MODE_SKIP	32	

typedef int CPChar;	
typedef int CPToken;	


typedef struct CPValue {
  union {
    int32_t i32;	
    uint32_t u32;	
  };
  CTypeID id;		
} CPValue;


typedef struct CPState {
  CPChar c;		
  CPToken tok;		
  CPValue val;		
  GCstr *str;		
  CType *ct;		
  const char *p;	
  SBuf sb;		
  lua_State *L;		
  CTState *cts;		
  TValue *param;	
  const char *srcname;	
  BCLine linenumber;	
  int depth;		
  uint32_t tmask;	
  uint32_t mode;	
  uint8_t packstack[CPARSE_MAX_PACKSTACK];  
  uint8_t curpack;	
} CPState;

LJ_FUNC int lj_cparse(CPState *cp);

LJ_FUNC int lj_cparse_case(GCstr *str, const char *match);

#endif

#endif
