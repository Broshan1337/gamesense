

#include "buildvm.h"
#include "lj_obj.h"
#include "lj_gc.h"
#include "lj_bc.h"
#if LJ_HASJIT
#include "lj_ir.h"
#include "lj_ircall.h"
#endif
#include "lj_frame.h"
#include "lj_dispatch.h"
#if LJ_HASFFI
#include "lj_ctype.h"
#include "lj_ccall.h"
#endif
#include "luajit.h"

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif




#define Dst		ctx
#define Dst_DECL	BuildCtx *ctx
#define Dst_REF		(ctx->D)
#define DASM_CHECKS	1

#include "../dynasm/dasm_proto.h"


static int collect_reloc(BuildCtx *ctx, uint8_t *addr, int idx, int type);

#define DASM_EXTERN(ctx, addr, idx, type) \
  collect_reloc(ctx, addr, idx, type)




#define DASM_ALIGNED_WRITES	1


#if LJ_TARGET_X86ORX64
#include "../dynasm/dasm_x86.h"
#elif LJ_TARGET_ARM
#include "../dynasm/dasm_arm.h"
#elif LJ_TARGET_ARM64
#include "../dynasm/dasm_arm64.h"
#elif LJ_TARGET_PPC
#include "../dynasm/dasm_ppc.h"
#elif LJ_TARGET_MIPS
#include "../dynasm/dasm_mips.h"
#else
#error "No support for this architecture (yet)"
#endif


#include "buildvm_arch.h"



void owrite(BuildCtx *ctx, const void *ptr, size_t sz)
{
  if (fwrite(ptr, 1, sz, ctx->fp) != sz) {
    fprintf(stderr, "Error: cannot write to output file: %s\n",
	    strerror(errno));
    exit(1);
  }
}




static void emit_raw(BuildCtx *ctx)
{
  owrite(ctx, ctx->code, ctx->codesz);
}



static const char *sym_decorate(BuildCtx *ctx,
				const char *prefix, const char *suffix)
{
  char name[256];
  char *p;
#if LJ_64
  const char *symprefix = ctx->mode == BUILD_machasm ? "_" : "";
#elif LJ_TARGET_XBOX360
  const char *symprefix = "";
#else
  const char *symprefix = ctx->mode != BUILD_elfasm ? "_" : "";
#endif
  sprintf(name, "%s%s%s", symprefix, prefix, suffix);
  p = strchr(name, '@');
  if (p) {
#if LJ_TARGET_X86ORX64
    if (!LJ_64 && (ctx->mode == BUILD_coffasm || ctx->mode == BUILD_peobj))
      name[0] = name[1] == 'R' ? '_' : '@';  
    else
      *p = '\0';
#elif LJ_TARGET_PPC && !LJ_TARGET_CONSOLE
    
#else
    *p = '\0';
#endif
  }
  p = (char *)malloc(strlen(name)+1);  
  strcpy(p, name);
  return p;
}

#define NRELOCSYM	(sizeof(extnames)/sizeof(extnames[0])-1)

static int relocmap[NRELOCSYM];


static int collect_reloc(BuildCtx *ctx, uint8_t *addr, int idx, int type)
{
  if (ctx->nreloc >= BUILD_MAX_RELOC) {
    fprintf(stderr, "Error: too many relocations, increase BUILD_MAX_RELOC.\n");
    exit(1);
  }
  if (relocmap[idx] < 0) {
    relocmap[idx] = ctx->nrelocsym;
    ctx->relocsym[ctx->nrelocsym] = sym_decorate(ctx, "", extnames[idx]);
    ctx->nrelocsym++;
  }
  ctx->reloc[ctx->nreloc].ofs = (int32_t)(addr - ctx->code);
  ctx->reloc[ctx->nreloc].sym = relocmap[idx];
  ctx->reloc[ctx->nreloc].type = type;
  ctx->nreloc++;
#if LJ_TARGET_XBOX360
  return (int)(ctx->code - addr) + 4;  
#else
  return 0;  
#endif
}


static void sym_insert(BuildCtx *ctx, int32_t ofs,
		       const char *prefix, const char *suffix)
{
  ptrdiff_t i = ctx->nsym++;
  while (i > 0) {
    if (ctx->sym[i-1].ofs <= ofs)
      break;
    ctx->sym[i] = ctx->sym[i-1];
    i--;
  }
  ctx->sym[i].ofs = ofs;
  ctx->sym[i].name = sym_decorate(ctx, prefix, suffix);
}


static int build_code(BuildCtx *ctx)
{
  int status;
  int i;

  
  ctx->nglob = GLOB__MAX;
  ctx->glob = (void **)malloc(ctx->nglob*sizeof(void *));
  memset(ctx->glob, 0, ctx->nglob*sizeof(void *));
  ctx->nreloc = 0;

  ctx->globnames = globnames;
  ctx->extnames = extnames;
  ctx->relocsym = (const char **)malloc(NRELOCSYM*sizeof(const char *));
  ctx->nrelocsym = 0;
  for (i = 0; i < (int)NRELOCSYM; i++) relocmap[i] = -1;

  ctx->dasm_ident = DASM_IDENT;
  ctx->dasm_arch = DASM_ARCH;

  dasm_init(Dst, DASM_MAXSECTION);
  dasm_setupglobal(Dst, ctx->glob, ctx->nglob);
  dasm_setup(Dst, build_actionlist);

  
  ctx->npc = build_backend(ctx);

  
  (void)dasm_checkstep(Dst, -1);
  if ((status = dasm_link(Dst, &ctx->codesz))) return status;
  ctx->code = (uint8_t *)malloc(ctx->codesz);
  if ((status = dasm_encode(Dst, (void *)ctx->code))) return status;

  
  ctx->beginsym = sym_decorate(ctx, "", LABEL_PREFIX "vm_asm_begin");
  ctx->sym = (BuildSym *)malloc((ctx->npc+ctx->nglob+1)*sizeof(BuildSym));
  ctx->nsym = 0;
  ctx->bc_ofs = (int32_t *)malloc(ctx->npc*sizeof(int32_t));

  
  for (i = 0; i < ctx->npc; i++) {
    int32_t ofs = dasm_getpclabel(Dst, i);
    if (ofs < 0) return 0x22000000|i;
    ctx->bc_ofs[i] = ofs;
    if ((LJ_HASJIT ||
	 !(i == BC_JFORI || i == BC_JFORL || i == BC_JITERL || i == BC_JLOOP ||
	   i == BC_IFORL || i == BC_IITERL || i == BC_ILOOP)) &&
	(LJ_HASFFI || i != BC_KCDATA))
      sym_insert(ctx, ofs, LABEL_PREFIX_BC, bc_names[i]);
  }

  
  for (i = 0; i < ctx->nglob; i++) {
    const char *gl = globnames[i];
    int len = (int)strlen(gl);
    if (!ctx->glob[i]) {
      fprintf(stderr, "Error: undefined global %s\n", gl);
      exit(2);
    }
    
    if (!(len >= 2 && gl[len-2] == '_' && gl[len-1] == 'Z'))
      sym_insert(ctx, (int32_t)((uint8_t *)(ctx->glob[i]) - ctx->code),
		 LABEL_PREFIX, globnames[i]);
  }

  
  sym_insert(ctx, (int32_t)ctx->codesz, "", "");
  ctx->nsym--;

  dasm_free(Dst);

  return 0;
}



const char *const bc_names[] = {
#define BCNAME(name, ma, mb, mc, mt)       #name,
BCDEF(BCNAME)
#undef BCNAME
  NULL
};

#if LJ_HASJIT
const char *const ir_names[] = {
#define IRNAME(name, m, m1, m2)	#name,
IRDEF(IRNAME)
#undef IRNAME
  NULL
};

const char *const irt_names[] = {
#define IRTNAME(name, size)	#name,
IRTDEF(IRTNAME)
#undef IRTNAME
  NULL
};

const char *const irfpm_names[] = {
#define FPMNAME(name)		#name,
IRFPMDEF(FPMNAME)
#undef FPMNAME
  NULL
};

const char *const irfield_names[] = {
#define FLNAME(name, ofs)	#name,
IRFLDEF(FLNAME)
#undef FLNAME
  NULL
};

const char *const ircall_names[] = {
#define IRCALLNAME(cond, name, nargs, kind, type, flags)	#name,
IRCALLDEF(IRCALLNAME)
#undef IRCALLNAME
  NULL
};

static const char *const trace_errors[] = {
#define TREDEF(name, msg)	msg,
#include "lj_traceerr.h"
  NULL
};
#endif

#if LJ_HASJIT
static const char *lower(char *buf, const char *s)
{
  char *p = buf;
  while (*s) {
    *p++ = (*s >= 'A' && *s <= 'Z') ? *s+0x20 : *s;
    s++;
  }
  *p = '\0';
  return buf;
}
#endif


static void emit_bcdef(BuildCtx *ctx)
{
  int i;
  fprintf(ctx->fp, "/* This is a generated file. DO NOT EDIT! */\n\n");
  fprintf(ctx->fp, "LJ_DATADEF const uint16_t lj_bc_ofs[] = {\n");
  for (i = 0; i < ctx->npc; i++) {
    if (i != 0)
      fprintf(ctx->fp, ",\n");
    fprintf(ctx->fp, "%d", ctx->bc_ofs[i]);
  }
}


static void emit_vmdef(BuildCtx *ctx)
{
#if LJ_HASJIT
  char buf[80];
#endif
  int i;
  fprintf(ctx->fp, "-- This is a generated file. DO NOT EDIT!\n\n");
  fprintf(ctx->fp, "assert(require(\"jit\").version == \"%s\", \"LuaJIT core/library version mismatch\")\n\n", LUAJIT_VERSION);
  fprintf(ctx->fp, "return {\n\n");

  fprintf(ctx->fp, "bcnames = \"");
  for (i = 0; bc_names[i]; i++) fprintf(ctx->fp, "%-6s", bc_names[i]);
  fprintf(ctx->fp, "\",\n\n");

#if LJ_HASJIT
  fprintf(ctx->fp, "irnames = \"");
  for (i = 0; ir_names[i]; i++) fprintf(ctx->fp, "%-6s", ir_names[i]);
  fprintf(ctx->fp, "\",\n\n");

  fprintf(ctx->fp, "irfpm = { [0]=");
  for (i = 0; irfpm_names[i]; i++)
    fprintf(ctx->fp, "\"%s\", ", lower(buf, irfpm_names[i]));
  fprintf(ctx->fp, "},\n\n");

  fprintf(ctx->fp, "irfield = { [0]=");
  for (i = 0; irfield_names[i]; i++) {
    char *p;
    lower(buf, irfield_names[i]);
    p = strchr(buf, '_');
    if (p) *p = '.';
    fprintf(ctx->fp, "\"%s\", ", buf);
  }
  fprintf(ctx->fp, "},\n\n");

  fprintf(ctx->fp, "ircall = {\n[0]=");
  for (i = 0; ircall_names[i]; i++)
    fprintf(ctx->fp, "\"%s\",\n", ircall_names[i]);
  fprintf(ctx->fp, "},\n\n");

  fprintf(ctx->fp, "traceerr = {\n[0]=");
  for (i = 0; trace_errors[i]; i++)
    fprintf(ctx->fp, "\"%s\",\n", trace_errors[i]);
  fprintf(ctx->fp, "},\n\n");
#endif
}




static const char *const modenames[] = {
#define BUILDNAME(name)		#name,
BUILDDEF(BUILDNAME)
#undef BUILDNAME
  NULL
};


static void usage(void)
{
  int i;
  fprintf(stderr, LUAJIT_VERSION " VM builder.\n");
  fprintf(stderr, LUAJIT_COPYRIGHT ", " LUAJIT_URL "\n");
  fprintf(stderr, "Target architecture: " LJ_ARCH_NAME "\n\n");
  fprintf(stderr, "Usage: buildvm -m mode [-o outfile] [infiles...]\n\n");
  fprintf(stderr, "Available modes:\n");
  for (i = 0; i < BUILD__MAX; i++)
    fprintf(stderr, "  %s\n", modenames[i]);
  exit(1);
}


static BuildMode parsemode(const char *mode)
{
  int i;
  for (i = 0; modenames[i]; i++)
    if (!strcmp(mode, modenames[i]))
      return (BuildMode)i;
  usage();
  return (BuildMode)-1;
}


static void parseargs(BuildCtx *ctx, char **argv)
{
  const char *a;
  int i;
  ctx->mode = (BuildMode)-1;
  ctx->outname = "-";
  for (i = 1; (a = argv[i]) != NULL; i++) {
    if (a[0] != '-')
      break;
    switch (a[1]) {
    case '-':
      if (a[2]) goto err;
      i++;
      goto ok;
    case '\0':
      goto ok;
    case 'm':
      i++;
      if (a[2] || argv[i] == NULL) goto err;
      ctx->mode = parsemode(argv[i]);
      break;
    case 'o':
      i++;
      if (a[2] || argv[i] == NULL) goto err;
      ctx->outname = argv[i];
      break;
    default: err:
      usage();
      break;
    }
  }
ok:
  ctx->args = argv+i;
  if (ctx->mode == (BuildMode)-1) goto err;
}

int main(int argc, char **argv)
{
  BuildCtx ctx_;
  BuildCtx *ctx = &ctx_;
  int status, binmode;

  if (sizeof(void *) != 4*LJ_32+8*LJ_64) {
    fprintf(stderr,"Error: pointer size mismatch in cross-build.\n");
    fprintf(stderr,"Try: make HOST_CC=\"gcc -m32\" CROSS=...\n\n");
    return 1;
  }

  UNUSED(argc);
  parseargs(ctx, argv);

  if ((status = build_code(ctx))) {
    fprintf(stderr,"Error: DASM error %08x\n", status);
    return 1;
  }

  switch (ctx->mode) {
  case BUILD_peobj:
  case BUILD_raw:
    binmode = 1;
    break;
  default:
    binmode = 0;
    break;
  }

  if (ctx->outname[0] == '-' && ctx->outname[1] == '\0') {
    ctx->fp = stdout;
#if defined(_WIN32)
    if (binmode)
      _setmode(_fileno(stdout), _O_BINARY);  
#endif
  } else if (!(ctx->fp = fopen(ctx->outname, binmode ? "wb" : "w"))) {
    fprintf(stderr, "Error: cannot open output file '%s': %s\n",
	    ctx->outname, strerror(errno));
    exit(1);
  }

  switch (ctx->mode) {
  case BUILD_elfasm:
  case BUILD_coffasm:
  case BUILD_machasm:
    emit_asm(ctx);
    emit_asm_debug(ctx);
    break;
  case BUILD_peobj:
    emit_peobj(ctx);
    break;
  case BUILD_raw:
    emit_raw(ctx);
    break;
  case BUILD_bcdef:
    emit_bcdef(ctx);
    emit_lib(ctx);
    break;
  case BUILD_vmdef:
    emit_vmdef(ctx);
    emit_lib(ctx);
    fprintf(ctx->fp, "}\n\n");
    break;
  case BUILD_ffdef:
  case BUILD_libdef:
  case BUILD_recdef:
    emit_lib(ctx);
    break;
  case BUILD_folddef:
    emit_fold(ctx);
    break;
  default:
    break;
  }

  fflush(ctx->fp);
  if (ferror(ctx->fp)) {
    fprintf(stderr, "Error: cannot write to output file: %s\n",
	    strerror(errno));
    exit(1);
  }
  fclose(ctx->fp);

  return 0;
}

