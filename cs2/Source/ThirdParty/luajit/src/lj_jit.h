

#ifndef _LJ_JIT_H
#define _LJ_JIT_H

#include "lj_obj.h"
#if LJ_HASJIT
#include "lj_ir.h"




#define JIT_F_ON		0x00000001


#define JIT_F_CPU		0x00000010

#if LJ_TARGET_X86ORX64

#define JIT_F_SSE3		(JIT_F_CPU << 0)
#define JIT_F_SSE4_1		(JIT_F_CPU << 1)
#define JIT_F_BMI2		(JIT_F_CPU << 2)


#define JIT_F_CPUSTRING		"\4SSE3\6SSE4.1\4BMI2"

#elif LJ_TARGET_ARM

#define JIT_F_ARMV6_		(JIT_F_CPU << 0)
#define JIT_F_ARMV6T2_		(JIT_F_CPU << 1)
#define JIT_F_ARMV7		(JIT_F_CPU << 2)
#define JIT_F_ARMV8		(JIT_F_CPU << 3)
#define JIT_F_VFPV2		(JIT_F_CPU << 4)
#define JIT_F_VFPV3		(JIT_F_CPU << 5)

#define JIT_F_ARMV6		(JIT_F_ARMV6_|JIT_F_ARMV6T2_|JIT_F_ARMV7|JIT_F_ARMV8)
#define JIT_F_ARMV6T2		(JIT_F_ARMV6T2_|JIT_F_ARMV7|JIT_F_ARMV8)
#define JIT_F_VFP		(JIT_F_VFPV2|JIT_F_VFPV3)

#define JIT_F_CPUSTRING		"\5ARMv6\7ARMv6T2\5ARMv7\5ARMv8\5VFPv2\5VFPv3"

#elif LJ_TARGET_PPC

#define JIT_F_SQRT		(JIT_F_CPU << 0)
#define JIT_F_ROUND		(JIT_F_CPU << 1)

#define JIT_F_CPUSTRING		"\4SQRT\5ROUND"

#elif LJ_TARGET_MIPS

#define JIT_F_MIPSXXR2		(JIT_F_CPU << 0)

#if LJ_TARGET_MIPS32
#if LJ_TARGET_MIPSR6
#define JIT_F_CPUSTRING		"\010MIPS32R6"
#else
#define JIT_F_CPUSTRING		"\010MIPS32R2"
#endif
#else
#if LJ_TARGET_MIPSR6
#define JIT_F_CPUSTRING		"\010MIPS64R6"
#else
#define JIT_F_CPUSTRING		"\010MIPS64R2"
#endif
#endif

#else

#define JIT_F_CPUSTRING		""

#endif


#define JIT_F_OPT		0x00010000
#define JIT_F_OPT_MASK		0x0fff0000

#define JIT_F_OPT_FOLD		(JIT_F_OPT << 0)
#define JIT_F_OPT_CSE		(JIT_F_OPT << 1)
#define JIT_F_OPT_DCE		(JIT_F_OPT << 2)
#define JIT_F_OPT_FWD		(JIT_F_OPT << 3)
#define JIT_F_OPT_DSE		(JIT_F_OPT << 4)
#define JIT_F_OPT_NARROW	(JIT_F_OPT << 5)
#define JIT_F_OPT_LOOP		(JIT_F_OPT << 6)
#define JIT_F_OPT_ABC		(JIT_F_OPT << 7)
#define JIT_F_OPT_SINK		(JIT_F_OPT << 8)
#define JIT_F_OPT_FUSE		(JIT_F_OPT << 9)
#define JIT_F_OPT_FMA		(JIT_F_OPT << 10)


#define JIT_F_OPTSTRING	\
  "\4fold\3cse\3dce\3fwd\3dse\6narrow\4loop\3abc\4sink\4fuse\3fma"


#define JIT_F_OPT_0	0
#define JIT_F_OPT_1	(JIT_F_OPT_FOLD|JIT_F_OPT_CSE|JIT_F_OPT_DCE)
#define JIT_F_OPT_2	(JIT_F_OPT_1|JIT_F_OPT_NARROW|JIT_F_OPT_LOOP)
#define JIT_F_OPT_3	(JIT_F_OPT_2|\
  JIT_F_OPT_FWD|JIT_F_OPT_DSE|JIT_F_OPT_ABC|JIT_F_OPT_SINK|JIT_F_OPT_FUSE)
#define JIT_F_OPT_DEFAULT	JIT_F_OPT_3





#define JIT_PARAMDEF(_) \
  _(\010, maxtrace,	1000)	 \
  _(\011, maxrecord,	4000)	 \
  _(\012, maxirconst,	500)	 \
  _(\007, maxside,	100)	 \
  _(\007, maxsnap,	500)	 \
  _(\011, minstitch,	0)	 \
  \
  _(\007, hotloop,	56)	 \
  _(\007, hotexit,	10)	 \
  _(\007, tryside,	4)	 \
  \
  _(\012, instunroll,	4)	 \
  _(\012, loopunroll,	15)	 \
  _(\012, callunroll,	3)	 \
  _(\011, recunroll,	2)	 \
  \
   \
  _(\011, sizemcode,	64) \
   \
  _(\010, maxmcode,	2048) \
  

enum {
#define JIT_PARAMENUM(len, name, value)	JIT_P_##name,
JIT_PARAMDEF(JIT_PARAMENUM)
#undef JIT_PARAMENUM
  JIT_P__MAX
};

#define JIT_PARAMSTR(len, name, value)	#len #name
#define JIT_P_STRING	JIT_PARAMDEF(JIT_PARAMSTR)




typedef enum {
  LJ_TRACE_IDLE,	
  LJ_TRACE_ACTIVE = 0x10,
  LJ_TRACE_RECORD,	
  LJ_TRACE_RECORD_1ST,	
  LJ_TRACE_START,	
  LJ_TRACE_END,		
  LJ_TRACE_ASM,		
  LJ_TRACE_ERR		
} TraceState;


typedef enum {
  LJ_POST_NONE,		
  LJ_POST_FIXCOMP,	
  LJ_POST_FIXGUARD,	
  LJ_POST_FIXGUARDSNAP,	
  LJ_POST_FIXBOOL,	
  LJ_POST_FIXCONST,	
  LJ_POST_FFRETRY	
} PostProc;


#if LJ_TARGET_X86ORX64
typedef uint8_t MCode;
#else
typedef uint32_t MCode;
#endif


typedef struct MCLink {
  MCode *next;		
  size_t size;		
} MCLink;


typedef struct SnapShot {
  uint32_t mapofs;	
  IRRef1 ref;		
  uint16_t mcofs;	
  uint8_t nslots;	
  uint8_t topslot;	
  uint8_t nent;		
  uint8_t count;	
} SnapShot;

#define SNAPCOUNT_DONE	255	


typedef uint32_t SnapEntry;

#define SNAP_FRAME		0x010000	
#define SNAP_CONT		0x020000	
#define SNAP_NORESTORE		0x040000	
#define SNAP_SOFTFPNUM		0x080000	
#define SNAP_KEYINDEX		0x100000	
LJ_STATIC_ASSERT(SNAP_FRAME == TREF_FRAME);
LJ_STATIC_ASSERT(SNAP_CONT == TREF_CONT);
LJ_STATIC_ASSERT(SNAP_KEYINDEX == TREF_KEYINDEX);

#define SNAP(slot, flags, ref)	(((SnapEntry)(slot) << 24) + (flags) + (ref))
#define SNAP_TR(slot, tr) \
  (((SnapEntry)(slot) << 24) + \
   ((tr) & (TREF_KEYINDEX|TREF_CONT|TREF_FRAME|TREF_REFMASK)))
#if !LJ_FR2
#define SNAP_MKPC(pc)		((SnapEntry)u32ptr(pc))
#endif
#define SNAP_MKFTSZ(ftsz)	((SnapEntry)(ftsz))
#define snap_ref(sn)		((sn) & 0xffff)
#define snap_slot(sn)		((BCReg)((sn) >> 24))
#define snap_isframe(sn)	((sn) & SNAP_FRAME)
#define snap_setref(sn, ref)	(((sn) & (0xffff0000&~SNAP_NORESTORE)) | (ref))

static LJ_AINLINE const BCIns *snap_pc(SnapEntry *sn)
{
#if LJ_FR2
  uint64_t pcbase;
  memcpy(&pcbase, sn, sizeof(uint64_t));
  return (const BCIns *)(pcbase >> 8);
#else
  return (const BCIns *)(uintptr_t)*sn;
#endif
}


typedef uint32_t SnapNo;
typedef uint32_t ExitNo;


typedef uint32_t TraceNo;	
typedef uint16_t TraceNo1;	


typedef enum {
  LJ_TRLINK_NONE,		
  LJ_TRLINK_ROOT,		
  LJ_TRLINK_LOOP,		
  LJ_TRLINK_TAILREC,		
  LJ_TRLINK_UPREC,		
  LJ_TRLINK_DOWNREC,		
  LJ_TRLINK_INTERP,		
  LJ_TRLINK_RETURN,		
  LJ_TRLINK_STITCH		
} TraceLink;


typedef struct GCtrace {
  GCHeader;
  uint16_t nsnap;	
  IRRef nins;		
#if LJ_GC64
  uint32_t unused_gc64;
#endif
  GCRef gclist;
  IRIns *ir;		
  IRRef nk;		
  uint32_t nsnapmap;	
  SnapShot *snap;	
  SnapEntry *snapmap;	
  GCRef startpt;	
  MRef startpc;		
  BCIns startins;	
  MSize szmcode;	
  MCode *mcode;		
#if LJ_ABI_PAUTH
  ASMFunction mcauth;	
#endif
  MSize mcloop;		
  uint16_t nchild;	
  uint16_t spadjust;	
  TraceNo1 traceno;	
  TraceNo1 link;	
  TraceNo1 root;	
  TraceNo1 nextroot;	
  TraceNo1 nextside;	
  uint8_t sinktags;	
  uint8_t topslot;	
  uint8_t linktype;	
  uint8_t unused1;
#ifdef LUAJIT_USE_GDBJIT
  void *gdbjit_entry;	
#endif
} GCtrace;

#define gco2trace(o)	check_exp((o)->gch.gct == ~LJ_TTRACE, (GCtrace *)(o))
#define traceref(J, n) \
  check_exp((n)>0 && (MSize)(n)<J->sizetrace, (GCtrace *)gcref(J->trace[(n)]))

LJ_STATIC_ASSERT(offsetof(GChead, gclist) == offsetof(GCtrace, gclist));

static LJ_AINLINE MSize snap_nextofs(GCtrace *T, SnapShot *snap)
{
  if (snap+1 == &T->snap[T->nsnap])
    return T->nsnapmap;
  else
    return (snap+1)->mapofs;
}


typedef struct HotPenalty {
  MRef pc;		
  uint16_t val;		
  uint16_t reason;	
} HotPenalty;

#define PENALTY_SLOTS	64	
#define PENALTY_MIN	(36*2)	
#define PENALTY_MAX	60000	
#define PENALTY_RNDBITS	4	


typedef struct BPropEntry {
  IRRef1 key;		
  IRRef1 val;		
  IRRef mode;		
} BPropEntry;


#define BPROP_SLOTS	16


typedef struct ScEvEntry {
  MRef pc;		
  IRRef1 idx;		
  IRRef1 start;		
  IRRef1 stop;		
  IRRef1 step;		
  IRType1 t;		
  uint8_t dir;		
} ScEvEntry;


typedef struct RBCHashEntry {
  MRef pc;		
  GCRef pt;		
  IRRef ref;		
} RBCHashEntry;


#define RBCHASH_SLOTS	8


enum {
  LJ_KSIMD_ABS,
  LJ_KSIMD_NEG,
  LJ_KSIMD__MAX
};

enum {
#if LJ_TARGET_X64 || LJ_TARGET_MIPS64
  LJ_K64_M2P64,		
#endif
#if LJ_TARGET_X86ORX64
  LJ_K64_TOBIT,		
  LJ_K64_2P64,		
#endif
#if LJ_TARGET_MIPS64
  LJ_K64_2P63,		
#endif
#if LJ_TARGET_MIPS
  LJ_K64_2P31,		
#endif
#if LJ_TARGET_ARM64 || LJ_TARGET_MIPS64
  LJ_K64_VM_EXIT_HANDLER,
  LJ_K64_VM_EXIT_INTERP,
#endif
  LJ_K64__MAX,
};
#define LJ_K64__USED	(LJ_TARGET_X86ORX64 || LJ_TARGET_ARM64 || LJ_TARGET_MIPS)

enum {
#if LJ_TARGET_X86ORX64 || LJ_TARGET_MIPS64
  LJ_K32_M2P64,		
#endif
#if LJ_TARGET_MIPS64
  LJ_K32_2P63,		
#endif
#if LJ_TARGET_PPC
  LJ_K32_2P52_2P31,	
  LJ_K32_2P52,		
#endif
#if LJ_TARGET_PPC
  LJ_K32_2P31,		
#endif
#if LJ_TARGET_PPC || LJ_TARGET_MIPS32
  LJ_K32_VM_EXIT_HANDLER,
  LJ_K32_VM_EXIT_INTERP,
#endif
  LJ_K32__MAX
};
#define LJ_K32__USED	(LJ_TARGET_X86ORX64 || LJ_TARGET_PPC || LJ_TARGET_MIPS)


#define LJ_KSIMD(J, n) \
  ((TValue *)(((intptr_t)&J->ksimd[2*(n)] + 15) & ~(intptr_t)15))


#if LJ_SOFTFP32 || (LJ_32 && LJ_HASFFI)
#define lj_needsplit(J)		(J->needsplit = 1)
#define lj_resetsplit(J)	(J->needsplit = 0)
#else
#define lj_needsplit(J)		UNUSED(J)
#define lj_resetsplit(J)	UNUSED(J)
#endif


typedef struct FoldState {
  IRIns ins;		
  IRIns left[2];	
  IRIns right[2];	
} FoldState;


typedef struct jit_State {
  GCtrace cur;		
  GCtrace *curfinal;	

  lua_State *L;		
  const BCIns *pc;	
  GCfunc *fn;		
  GCproto *pt;		
  TRef *base;		

  uint32_t flags;	
  BCReg maxslot;	
  BCReg baseslot;	

  uint8_t mergesnap;	
  uint8_t needsnap;	
  IRType1 guardemit;	
  uint8_t bcskip;	

  FoldState fold;	

  const BCIns *bc_min;	
  MSize bc_extent;	

  TraceState state;	

  int32_t instunroll;	
  int32_t loopunroll;	
  int32_t tailcalled;	
  int32_t framedepth;	
  int32_t retdepth;	

#if LJ_K32__USED
  uint32_t k32[LJ_K32__MAX];  
#endif
  TValue ksimd[LJ_KSIMD__MAX*2+1];  
#if LJ_K64__USED
  TValue k64[LJ_K64__MAX];  
#endif

  IRIns *irbuf;		
  IRRef irtoplim;	
  IRRef irbotlim;	
  IRRef loopref;	

  MSize sizesnap;	
  SnapShot *snapbuf;	
  SnapEntry *snapmapbuf;  
  MSize sizesnapmap;	

  PostProc postproc;	
#if LJ_SOFTFP32 || (LJ_32 && LJ_HASFFI)
  uint8_t needsplit;	
#endif
  uint8_t retryrec;	

  GCRef *trace;		
  TraceNo freetrace;	
  MSize sizetrace;	
  IRRef1 ktrace;	

  IRRef1 chain[IR__MAX];  
  TRef slot[LJ_MAX_JSLOTS+LJ_STACK_EXTRA];  

  int32_t param[JIT_P__MAX];  

  MCode *exitstubgroup[LJ_MAX_EXITSTUBGR];  

  HotPenalty penalty[PENALTY_SLOTS];  
  uint32_t penaltyslot;	

#ifdef LUAJIT_ENABLE_TABLE_BUMP
  RBCHashEntry rbchash[RBCHASH_SLOTS];  
#endif

  BPropEntry bpropcache[BPROP_SLOTS];  
  uint32_t bpropslot;	

  ScEvEntry scev;	

  const BCIns *startpc;	
  TraceNo parent;	
  ExitNo exitno;	
  int exitcode;		

  BCIns *patchpc;	
  BCIns patchins;	

  int mcprot;		
  MCode *mcarea;	
  MCode *mctop;		
  MCode *mcbot;		
  size_t szmcarea;	
  size_t szallmcarea;	
  uintptr_t mcmin, mcmax;	

  TValue errinfo;	

#if LJ_HASPROFILE
  GCproto *prev_pt;	
  BCLine prev_line;	
  int prof_mode;	
#endif
} jit_State;

#ifdef LUA_USE_ASSERT
#define lj_assertJ(c, ...)	lj_assertG_(J2G(J), (c), __VA_ARGS__)
#else
#define lj_assertJ(c, ...)	((void)J)
#endif
#endif

#endif
