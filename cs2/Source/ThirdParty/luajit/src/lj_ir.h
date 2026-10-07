

#ifndef _LJ_IR_H
#define _LJ_IR_H

#include "lj_obj.h"




#define IRDEF(_) \
   \
   \
  _(LT,		N , ref, ref) \
  _(GE,		N , ref, ref) \
  _(LE,		N , ref, ref) \
  _(GT,		N , ref, ref) \
  \
  _(ULT,	N , ref, ref) \
  _(UGE,	N , ref, ref) \
  _(ULE,	N , ref, ref) \
  _(UGT,	N , ref, ref) \
  \
  _(EQ,		C , ref, ref) \
  _(NE,		C , ref, ref) \
  \
  _(ABC,	N , ref, ref) \
  _(RETF,	S , ref, ref) \
  \
   \
  _(NOP,	N , ___, ___) \
  _(BASE,	N , lit, lit) \
  _(PVAL,	N , lit, ___) \
  _(GCSTEP,	S , ___, ___) \
  _(HIOP,	S , ref, ref) \
  _(LOOP,	S , ___, ___) \
  _(USE,	S , ref, ___) \
  _(PHI,	S , ref, ref) \
  _(RENAME,	S , ref, lit) \
  _(PROF,	S , ___, ___) \
  \
   \
  _(KPRI,	N , ___, ___) \
  _(KINT,	N , cst, ___) \
  _(KGC,	N , cst, ___) \
  _(KPTR,	N , cst, ___) \
  _(KKPTR,	N , cst, ___) \
  _(KNULL,	N , cst, ___) \
  _(KNUM,	N , cst, ___) \
  _(KINT64,	N , cst, ___) \
  _(KSLOT,	N , ref, lit) \
  \
   \
  _(BNOT,	N , ref, ___) \
  _(BSWAP,	N , ref, ___) \
  _(BAND,	C , ref, ref) \
  _(BOR,	C , ref, ref) \
  _(BXOR,	C , ref, ref) \
  _(BSHL,	N , ref, ref) \
  _(BSHR,	N , ref, ref) \
  _(BSAR,	N , ref, ref) \
  _(BROL,	N , ref, ref) \
  _(BROR,	N , ref, ref) \
  \
   \
  _(ADD,	C , ref, ref) \
  _(SUB,	N , ref, ref) \
  _(MUL,	C , ref, ref) \
  _(DIV,	N , ref, ref) \
  _(MOD,	N , ref, ref) \
  _(POW,	N , ref, ref) \
  _(NEG,	N , ref, ref) \
  \
  _(ABS,	N , ref, ref) \
  _(LDEXP,	N , ref, ref) \
  _(MIN,	N , ref, ref) \
  _(MAX,	N , ref, ref) \
  _(FPMATH,	N , ref, lit) \
  \
   \
  _(ADDOV,	CW, ref, ref) \
  _(SUBOV,	NW, ref, ref) \
  _(MULOV,	CW, ref, ref) \
  \
   \
  \
   \
  _(AREF,	R , ref, ref) \
  _(HREFK,	R , ref, ref) \
  _(HREF,	L , ref, ref) \
  _(NEWREF,	S , ref, ref) \
  _(UREFO,	LW, ref, lit) \
  _(UREFC,	LW, ref, lit) \
  _(FREF,	R , ref, lit) \
  _(TMPREF,	S , ref, lit) \
  _(STRREF,	N , ref, ref) \
  _(LREF,	L , ___, ___) \
  \
   \
  _(ALOAD,	L , ref, ___) \
  _(HLOAD,	L , ref, ___) \
  _(ULOAD,	L , ref, ___) \
  _(FLOAD,	L , ref, lit) \
  _(XLOAD,	L , ref, lit) \
  _(SLOAD,	L , lit, lit) \
  _(VLOAD,	L , ref, lit) \
  _(ALEN,	L , ref, ref) \
  \
  _(ASTORE,	S , ref, ref) \
  _(HSTORE,	S , ref, ref) \
  _(USTORE,	S , ref, ref) \
  _(FSTORE,	S , ref, ref) \
  _(XSTORE,	S , ref, ref) \
  \
   \
  _(SNEW,	N , ref, ref)   \
  _(XSNEW,	A , ref, ref) \
  _(TNEW,	AW, lit, lit) \
  _(TDUP,	AW, ref, ___) \
  _(CNEW,	AW, ref, ref) \
  _(CNEWI,	NW, ref, ref)   \
  \
   \
  _(BUFHDR,	L , ref, lit) \
  _(BUFPUT,	LW, ref, ref) \
  _(BUFSTR,	AW, ref, ref) \
  \
   \
  _(TBAR,	S , ref, ___) \
  _(OBAR,	S , ref, ref) \
  _(XBAR,	S , ___, ___) \
  \
   \
  _(CONV,	N , ref, lit) \
  _(TOBIT,	N , ref, ref) \
  _(TOSTR,	N , ref, lit) \
  _(STRTO,	N , ref, ___) \
  \
   \
  _(CALLN,	NW, ref, lit) \
  _(CALLA,	AW, ref, lit) \
  _(CALLL,	LW, ref, lit) \
  _(CALLS,	S , ref, lit) \
  _(CALLXS,	S , ref, ref) \
  _(CARG,	N , ref, ref) \
  \
  


typedef enum {
#define IRENUM(name, m, m1, m2)	IR_##name,
IRDEF(IRENUM)
#undef IRENUM
  IR__MAX
} IROp;


typedef uint8_t IROp1;

LJ_STATIC_ASSERT(((int)IR_EQ^1) == (int)IR_NE);
LJ_STATIC_ASSERT(((int)IR_LT^1) == (int)IR_GE);
LJ_STATIC_ASSERT(((int)IR_LE^1) == (int)IR_GT);
LJ_STATIC_ASSERT(((int)IR_LT^3) == (int)IR_GT);
LJ_STATIC_ASSERT(((int)IR_LT^4) == (int)IR_ULT);


#define IRDELTA_L2S		((int)IR_ASTORE - (int)IR_ALOAD)

LJ_STATIC_ASSERT((int)IR_HLOAD + IRDELTA_L2S == (int)IR_HSTORE);
LJ_STATIC_ASSERT((int)IR_ULOAD + IRDELTA_L2S == (int)IR_USTORE);
LJ_STATIC_ASSERT((int)IR_FLOAD + IRDELTA_L2S == (int)IR_FSTORE);
LJ_STATIC_ASSERT((int)IR_XLOAD + IRDELTA_L2S == (int)IR_XSTORE);




#define IRFPMDEF(_) \
  _(FLOOR) _(CEIL) _(TRUNC)   \
  _(SQRT) _(LOG) _(LOG2) \
  _(OTHER)

typedef enum {
#define FPMENUM(name)		IRFPM_##name,
IRFPMDEF(FPMENUM)
#undef FPMENUM
  IRFPM__MAX
} IRFPMathOp;


#define IRFLDEF(_) \
  _(STR_LEN,	offsetof(GCstr, len)) \
  _(FUNC_ENV,	offsetof(GCfunc, l.env)) \
  _(FUNC_PC,	offsetof(GCfunc, l.pc)) \
  _(FUNC_FFID,	offsetof(GCfunc, l.ffid)) \
  _(THREAD_ENV,	offsetof(lua_State, env)) \
  _(TAB_META,	offsetof(GCtab, metatable)) \
  _(TAB_ARRAY,	offsetof(GCtab, array)) \
  _(TAB_NODE,	offsetof(GCtab, node)) \
  _(TAB_ASIZE,	offsetof(GCtab, asize)) \
  _(TAB_HMASK,	offsetof(GCtab, hmask)) \
  _(TAB_NOMM,	offsetof(GCtab, nomm)) \
  _(UDATA_META,	offsetof(GCudata, metatable)) \
  _(UDATA_UDTYPE, offsetof(GCudata, udtype)) \
  _(UDATA_FILE,	sizeof(GCudata)) \
  _(SBUF_W,	sizeof(GCudata) + offsetof(SBufExt, w)) \
  _(SBUF_E,	sizeof(GCudata) + offsetof(SBufExt, e)) \
  _(SBUF_B,	sizeof(GCudata) + offsetof(SBufExt, b)) \
  _(SBUF_L,	sizeof(GCudata) + offsetof(SBufExt, L)) \
  _(SBUF_REF,	sizeof(GCudata) + offsetof(SBufExt, cowref)) \
  _(SBUF_R,	sizeof(GCudata) + offsetof(SBufExt, r)) \
  _(CDATA_CTYPEID, offsetof(GCcdata, ctypeid)) \
  _(CDATA_PTR,	sizeof(GCcdata)) \
  _(CDATA_INT,	sizeof(GCcdata)) \
  _(CDATA_INT64, sizeof(GCcdata)) \
  _(CDATA_INT64_4, sizeof(GCcdata) + 4)

typedef enum {
#define FLENUM(name, ofs)	IRFL_##name,
IRFLDEF(FLENUM)
#undef FLENUM
  IRFL__MAX
} IRFieldID;


#define IRTMPREF_IN1		0x01	
#define IRTMPREF_OUT1		0x02	
#define IRTMPREF_OUT2		0x04	


#define IRSLOAD_PARENT		0x01	
#define IRSLOAD_FRAME		0x02	
#define IRSLOAD_TYPECHECK	0x04	
#define IRSLOAD_CONVERT		0x08	
#define IRSLOAD_READONLY	0x10	
#define IRSLOAD_INHERIT		0x20	
#define IRSLOAD_KEYINDEX	0x40	


#define IRXLOAD_READONLY	0x01	
#define IRXLOAD_VOLATILE	0x02	
#define IRXLOAD_UNALIGNED	0x04	


#define IRBUFHDR_RESET		0	
#define IRBUFHDR_APPEND		1	
#define IRBUFHDR_WRITE		2	


#define IRCONV_SRCMASK		0x001f	
#define IRCONV_DSTMASK		0x03e0	
#define IRCONV_DSH		5
#define IRCONV_NUM_INT		((IRT_NUM<<IRCONV_DSH)|IRT_INT)
#define IRCONV_INT_NUM		((IRT_INT<<IRCONV_DSH)|IRT_NUM)
#define IRCONV_SEXT		0x0800	
#define IRCONV_MODEMASK		0x0fff
#define IRCONV_CONVMASK		0xf000
#define IRCONV_CSH		12

#define IRCONV_TOBIT  (0<<IRCONV_CSH)	
#define IRCONV_ANY    (1<<IRCONV_CSH)	
#define IRCONV_INDEX  (2<<IRCONV_CSH)	
#define IRCONV_CHECK  (3<<IRCONV_CSH)	
#define IRCONV_NONE   IRCONV_ANY	


#define IRTOSTR_INT		0	
#define IRTOSTR_NUM		1	
#define IRTOSTR_CHAR		2	




typedef enum {
  IRMref,		
  IRMlit,		
  IRMcst,		
  IRMnone		
} IRMode;
#define IRM___		IRMnone


#define IRM_C			0x10

#define IRM_N			0x00
#define IRM_R			IRM_N
#define IRM_A			0x20
#define IRM_L			0x40
#define IRM_S			0x60

#define IRM_W			0x80

#define IRM_NW			(IRM_N|IRM_W)
#define IRM_CW			(IRM_C|IRM_W)
#define IRM_AW			(IRM_A|IRM_W)
#define IRM_LW			(IRM_L|IRM_W)

#define irm_op1(m)		((IRMode)((m)&3))
#define irm_op2(m)		((IRMode)(((m)>>2)&3))
#define irm_iscomm(m)		((m) & IRM_C)
#define irm_kind(m)		((m) & IRM_S)

#define IRMODE(name, m, m1, m2)	(((IRM##m1)|((IRM##m2)<<2)|(IRM_##m))^IRM_W),

LJ_DATA const uint8_t lj_ir_mode[IR__MAX+1];



#define IRTSIZE_PGC		(LJ_GC64 ? 8 : 4)


#define IRTDEF(_) \
  _(NIL, 4) _(FALSE, 4) _(TRUE, 4) _(LIGHTUD, LJ_64 ? 8 : 4) \
  _(STR, IRTSIZE_PGC) _(P32, 4) _(THREAD, IRTSIZE_PGC) _(PROTO, IRTSIZE_PGC) \
  _(FUNC, IRTSIZE_PGC) _(P64, 8) _(CDATA, IRTSIZE_PGC) _(TAB, IRTSIZE_PGC) \
  _(UDATA, IRTSIZE_PGC) \
  _(FLOAT, 4) _(NUM, 8) _(I8, 1) _(U8, 1) _(I16, 2) _(U16, 2) \
  _(INT, 4) _(U32, 4) _(I64, 8) _(U64, 8) \
  _(SOFTFP, 4)  


typedef enum {
#define IRTENUM(name, size)	IRT_##name,
IRTDEF(IRTENUM)
#undef IRTENUM
  IRT__MAX,

  
  IRT_PTR = LJ_64 ? IRT_P64 : IRT_P32,
  IRT_PGC = LJ_GC64 ? IRT_P64 : IRT_P32,
  IRT_IGC = LJ_GC64 ? IRT_I64 : IRT_INT,
  IRT_INTP = LJ_64 ? IRT_I64 : IRT_INT,
  IRT_UINTP = LJ_64 ? IRT_U64 : IRT_U32,

  
  IRT_MARK = 0x20,	
  IRT_ISPHI = 0x40,	
  IRT_GUARD = 0x80,	

  
  IRT_TYPE = 0x1f,
  IRT_T = 0xff
} IRType;

#define irtype_ispri(irt)	((uint32_t)(irt) <= IRT_TRUE)


typedef struct IRType1 { uint8_t irt; } IRType1;

#define IRT(o, t)		((uint32_t)(((o)<<8) | (t)))
#define IRTI(o)			(IRT((o), IRT_INT))
#define IRTN(o)			(IRT((o), IRT_NUM))
#define IRTG(o, t)		(IRT((o), IRT_GUARD|(t)))
#define IRTGI(o)		(IRT((o), IRT_GUARD|IRT_INT))

#define irt_t(t)		((IRType)(t).irt)
#define irt_type(t)		((IRType)((t).irt & IRT_TYPE))
#define irt_sametype(t1, t2)	((((t1).irt ^ (t2).irt) & IRT_TYPE) == 0)
#define irt_typerange(t, first, last) \
  ((uint32_t)((t).irt & IRT_TYPE) - (uint32_t)(first) <= (uint32_t)(last-first))

#define irt_isnil(t)		(irt_type(t) == IRT_NIL)
#define irt_ispri(t)		((uint32_t)irt_type(t) <= IRT_TRUE)
#define irt_islightud(t)	(irt_type(t) == IRT_LIGHTUD)
#define irt_isstr(t)		(irt_type(t) == IRT_STR)
#define irt_istab(t)		(irt_type(t) == IRT_TAB)
#define irt_iscdata(t)		(irt_type(t) == IRT_CDATA)
#define irt_isfloat(t)		(irt_type(t) == IRT_FLOAT)
#define irt_isnum(t)		(irt_type(t) == IRT_NUM)
#define irt_isint(t)		(irt_type(t) == IRT_INT)
#define irt_isi8(t)		(irt_type(t) == IRT_I8)
#define irt_isu8(t)		(irt_type(t) == IRT_U8)
#define irt_isi16(t)		(irt_type(t) == IRT_I16)
#define irt_isu16(t)		(irt_type(t) == IRT_U16)
#define irt_isu32(t)		(irt_type(t) == IRT_U32)
#define irt_isi64(t)		(irt_type(t) == IRT_I64)
#define irt_isu64(t)		(irt_type(t) == IRT_U64)
#define irt_isp32(t)		(irt_type(t) == IRT_P32)

#define irt_isfp(t)		(irt_isnum(t) || irt_isfloat(t))
#define irt_isinteger(t)	(irt_typerange((t), IRT_I8, IRT_INT))
#define irt_isgcv(t)		(irt_typerange((t), IRT_STR, IRT_UDATA))
#define irt_isaddr(t)		(irt_typerange((t), IRT_LIGHTUD, IRT_UDATA))
#define irt_isint64(t)		(irt_typerange((t), IRT_I64, IRT_U64))

#if LJ_GC64

#define IRT_IS64 \
  ((1u<<IRT_NUM)|(1u<<IRT_I64)|(1u<<IRT_U64)|(1u<<IRT_P64)|\
   (1u<<IRT_LIGHTUD)|(1u<<IRT_STR)|(1u<<IRT_THREAD)|(1u<<IRT_PROTO)|\
   (1u<<IRT_FUNC)|(1u<<IRT_CDATA)|(1u<<IRT_TAB)|(1u<<IRT_UDATA)|\
   (1u<<IRT_NIL))
#elif LJ_64
#define IRT_IS64 \
  ((1u<<IRT_NUM)|(1u<<IRT_I64)|(1u<<IRT_U64)|(1u<<IRT_P64)|(1u<<IRT_LIGHTUD))
#else
#define IRT_IS64 \
  ((1u<<IRT_NUM)|(1u<<IRT_I64)|(1u<<IRT_U64))
#endif

#define irt_is64(t)		((IRT_IS64 >> irt_type(t)) & 1)
#define irt_is64orfp(t)		(((IRT_IS64|(1u<<IRT_FLOAT))>>irt_type(t)) & 1)

#define irt_size(t)		(lj_ir_type_size[irt_t((t))])

LJ_DATA const uint8_t lj_ir_type_size[];

static LJ_AINLINE IRType itype2irt(const TValue *tv)
{
  if (tvisint(tv))
    return IRT_INT;
  else if (tvisnum(tv))
    return IRT_NUM;
#if LJ_64 && !LJ_GC64
  else if (tvislightud(tv))
    return IRT_LIGHTUD;
#endif
  else
    return (IRType)~itype(tv);
}

static LJ_AINLINE uint32_t irt_toitype_(IRType t)
{
  lj_assertX(!LJ_64 || LJ_GC64 || t != IRT_LIGHTUD,
	     "no plain type tag for lightuserdata");
  if (LJ_DUALNUM && t > IRT_NUM) {
    return LJ_TISNUM;
  } else {
    lj_assertX(t <= IRT_NUM, "no plain type tag for IR type %d", t);
    return ~(uint32_t)t;
  }
}

#define irt_toitype(t)		irt_toitype_(irt_type((t)))

#define irt_isguard(t)		((t).irt & IRT_GUARD)
#define irt_ismarked(t)		((t).irt & IRT_MARK)
#define irt_setmark(t)		((t).irt |= IRT_MARK)
#define irt_clearmark(t)	((t).irt &= ~IRT_MARK)
#define irt_isphi(t)		((t).irt & IRT_ISPHI)
#define irt_setphi(t)		((t).irt |= IRT_ISPHI)
#define irt_clearphi(t)		((t).irt &= ~IRT_ISPHI)


typedef uint16_t IROpT;




typedef uint16_t IRRef1;	
typedef uint32_t IRRef2;	
typedef uint32_t IRRef;		


enum {
  REF_BIAS =	0x8000,
  REF_TRUE =	REF_BIAS-3,
  REF_FALSE =	REF_BIAS-2,
  REF_NIL =	REF_BIAS-1,	
  REF_BASE =	REF_BIAS,	
  REF_FIRST =	REF_BIAS+1,
  REF_DROP =	0xffff
};



#define IRREF2(lo, hi)		((IRRef2)(lo) | ((IRRef2)(hi) << 16))

#define irref_isk(ref)		((ref) < REF_BIAS)


typedef uint32_t TRef;

#define TREF_REFMASK		0x0000ffff
#define TREF_FRAME		0x00010000
#define TREF_CONT		0x00020000
#define TREF_KEYINDEX		0x00100000

#define TREF(ref, t)		((TRef)((ref) + ((t)<<24)))

#define tref_ref(tr)		((IRRef1)(tr))
#define tref_t(tr)		((IRType)((tr)>>24))
#define tref_type(tr)		((IRType)(((tr)>>24) & IRT_TYPE))
#define tref_typerange(tr, first, last) \
  ((((tr)>>24) & IRT_TYPE) - (TRef)(first) <= (TRef)(last-first))

#define tref_istype(tr, t)	(((tr) & (IRT_TYPE<<24)) == ((t)<<24))
#define tref_isnil(tr)		(tref_istype((tr), IRT_NIL))
#define tref_isfalse(tr)	(tref_istype((tr), IRT_FALSE))
#define tref_istrue(tr)		(tref_istype((tr), IRT_TRUE))
#define tref_islightud(tr)	(tref_istype((tr), IRT_LIGHTUD))
#define tref_isstr(tr)		(tref_istype((tr), IRT_STR))
#define tref_isfunc(tr)		(tref_istype((tr), IRT_FUNC))
#define tref_iscdata(tr)	(tref_istype((tr), IRT_CDATA))
#define tref_istab(tr)		(tref_istype((tr), IRT_TAB))
#define tref_isudata(tr)	(tref_istype((tr), IRT_UDATA))
#define tref_isnum(tr)		(tref_istype((tr), IRT_NUM))
#define tref_isint(tr)		(tref_istype((tr), IRT_INT))

#define tref_isbool(tr)		(tref_typerange((tr), IRT_FALSE, IRT_TRUE))
#define tref_ispri(tr)		(tref_typerange((tr), IRT_NIL, IRT_TRUE))
#define tref_istruecond(tr)	(!tref_typerange((tr), IRT_NIL, IRT_FALSE))
#define tref_isinteger(tr)	(tref_typerange((tr), IRT_I8, IRT_INT))
#define tref_isnumber(tr)	(tref_typerange((tr), IRT_NUM, IRT_INT))
#define tref_isnumber_str(tr)	(tref_isnumber((tr)) || tref_isstr((tr)))
#define tref_isgcv(tr)		(tref_typerange((tr), IRT_STR, IRT_UDATA))

#define tref_isk(tr)		(irref_isk(tref_ref((tr))))
#define tref_isk2(tr1, tr2)	(irref_isk(tref_ref((tr1) | (tr2))))

#define TREF_PRI(t)		(TREF(REF_NIL-(t), (t)))
#define TREF_NIL		(TREF_PRI(IRT_NIL))
#define TREF_FALSE		(TREF_PRI(IRT_FALSE))
#define TREF_TRUE		(TREF_PRI(IRT_TRUE))





typedef union IRIns {
  struct {
    LJ_ENDIAN_LOHI(
      IRRef1 op1;	
    , IRRef1 op2;	
    )
    IROpT ot;		
    IRRef1 prev;	
  };
  struct {
    IRRef2 op12;	
    LJ_ENDIAN_LOHI(
      IRType1 t;	
    , IROp1 o;		
    )
    LJ_ENDIAN_LOHI(
      uint8_t r;	
    , uint8_t s;	
    )
  };
  int32_t i;		
  GCRef gcr;		
  MRef ptr;		
  TValue tv;		
} IRIns;

#define ir_isk64(ir) \
  ((ir)->o == IR_KNUM || (ir)->o == IR_KINT64 || \
   (LJ_GC64 && \
    ((ir)->o == IR_KGC || (ir)->o == IR_KPTR || (ir)->o == IR_KKPTR)))

#define ir_kgc(ir)	check_exp((ir)->o == IR_KGC, gcref((ir)[LJ_GC64].gcr))
#define ir_kstr(ir)	(gco2str(ir_kgc((ir))))
#define ir_ktab(ir)	(gco2tab(ir_kgc((ir))))
#define ir_kfunc(ir)	(gco2func(ir_kgc((ir))))
#define ir_kcdata(ir)	(gco2cd(ir_kgc((ir))))
#define ir_knum(ir)	check_exp((ir)->o == IR_KNUM, &(ir)[1].tv)
#define ir_kint64(ir)	check_exp((ir)->o == IR_KINT64, &(ir)[1].tv)
#define ir_k64(ir)	check_exp(ir_isk64(ir), &(ir)[1].tv)
#define ir_kptr(ir) \
  check_exp((ir)->o == IR_KPTR || (ir)->o == IR_KKPTR, \
    mref((ir)[LJ_GC64].ptr, void))


static LJ_AINLINE int ir_sideeff(IRIns *ir)
{
  return (((ir->t.irt | ~IRT_GUARD) & lj_ir_mode[ir->o]) >= IRM_S);
}

LJ_STATIC_ASSERT((int)IRT_GUARD == (int)IRM_W);


static LJ_AINLINE void lj_ir_nop(IRIns *ir)
{
  ir->ot = IRT(IR_NOP, IRT_NIL);
  ir->op1 = ir->op2 = 0;
  ir->prev = 0;
}

#endif
