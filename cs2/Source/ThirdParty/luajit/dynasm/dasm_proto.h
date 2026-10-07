

#ifndef _DASM_PROTO_H
#define _DASM_PROTO_H

#include <stddef.h>
#include <stdarg.h>

#define DASM_IDENT	"DynASM 1.5.0"
#define DASM_VERSION	10500	

#ifndef Dst_DECL
#define Dst_DECL	dasm_State **Dst
#endif

#ifndef Dst_REF
#define Dst_REF		(*Dst)
#endif

#ifndef DASM_FDEF
#define DASM_FDEF	extern
#endif

#ifndef DASM_M_GROW
#define DASM_M_GROW(ctx, t, p, sz, need) \
  do { \
    size_t _sz = (sz), _need = (need); \
    if (_sz < _need) { \
      if (_sz < 16) _sz = 16; \
      while (_sz < _need) _sz += _sz; \
      (p) = (t *)realloc((p), _sz); \
      if ((p) == NULL) exit(1); \
      (sz) = _sz; \
    } \
  } while(0)
#endif

#ifndef DASM_M_FREE
#define DASM_M_FREE(ctx, p, sz)	free(p)
#endif


typedef struct dasm_State dasm_State;



DASM_FDEF void dasm_init(Dst_DECL, int maxsection);
DASM_FDEF void dasm_free(Dst_DECL);


DASM_FDEF void dasm_setupglobal(Dst_DECL, void **gl, unsigned int maxgl);


DASM_FDEF void dasm_growpc(Dst_DECL, unsigned int maxpc);


DASM_FDEF void dasm_setup(Dst_DECL, const void *actionlist);


DASM_FDEF void dasm_put(Dst_DECL, int start, ...);


DASM_FDEF int dasm_link(Dst_DECL, size_t *szp);


DASM_FDEF int dasm_encode(Dst_DECL, void *buffer);


DASM_FDEF int dasm_getpclabel(Dst_DECL, unsigned int pc);

#ifdef DASM_CHECKS

DASM_FDEF int dasm_checkstep(Dst_DECL, int secmatch);
#else
#define dasm_checkstep(a, b)	0
#endif


#endif 
