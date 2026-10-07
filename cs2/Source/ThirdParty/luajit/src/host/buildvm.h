

#ifndef _BUILDVM_H
#define _BUILDVM_H

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "lj_def.h"
#include "lj_arch.h"


#define BUILD_MAX_RELOC		200	
#define BUILD_MAX_FOLD		4096	


#define LIBDEF_PREFIX		"LJLIB_"


#define FOLDDEF_PREFIX		"LJFOLD"


#define LABEL_PREFIX		"lj_"
#define LABEL_PREFIX_BC		LABEL_PREFIX "BC_"
#define LABEL_PREFIX_FF		LABEL_PREFIX "ff_"
#define LABEL_PREFIX_CF		LABEL_PREFIX "cf_"
#define LABEL_PREFIX_FFH	LABEL_PREFIX "ffh_"
#define LABEL_PREFIX_LIBCF	LABEL_PREFIX "lib_cf_"
#define LABEL_PREFIX_LIBINIT	LABEL_PREFIX "lib_init_"


struct dasm_State;


#define BUILDDEF(_) \
  _(elfasm) _(coffasm) _(machasm) _(peobj) _(raw) \
  _(bcdef) _(ffdef) _(libdef) _(recdef) _(vmdef) \
  _(folddef)

typedef enum {
#define BUILDENUM(name)		BUILD_##name,
BUILDDEF(BUILDENUM)
#undef BUILDENUM
  BUILD__MAX
} BuildMode;


typedef struct BuildReloc {
  int32_t ofs;
  int sym;
  int type;
} BuildReloc;

typedef struct BuildSym {
  const char *name;
  int32_t ofs;
} BuildSym;


typedef struct BuildCtx {
  
  struct dasm_State *D;
  
  BuildMode mode;
  FILE *fp;
  const char *outname;
  char **args;
  
  uint8_t *code;
  size_t codesz;
  int npc, nglob, nsym, nreloc, nrelocsym;
  void **glob;
  BuildSym *sym;
  const char **relocsym;
  int32_t *bc_ofs;
  const char *beginsym;
  
  const char *const *globnames;
  const char *const *extnames;
  const char *dasm_ident;
  const char *dasm_arch;
  
  BuildReloc reloc[BUILD_MAX_RELOC];
} BuildCtx;

extern void owrite(BuildCtx *ctx, const void *ptr, size_t sz);
extern void emit_asm(BuildCtx *ctx);
extern void emit_peobj(BuildCtx *ctx);
extern void emit_lib(BuildCtx *ctx);
extern void emit_fold(BuildCtx *ctx);

extern const char *const bc_names[];
extern const char *const ir_names[];
extern const char *const irt_names[];
extern const char *const irfpm_names[];
extern const char *const irfield_names[];
extern const char *const ircall_names[];

#endif
