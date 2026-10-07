

#ifndef _LJ_CTYPE_H
#define _LJ_CTYPE_H

#include "lj_obj.h"
#include "lj_gc.h"

#if LJ_HASFFI




enum {
  
  CT_NUM,		
  CT_STRUCT,		
  CT_PTR,		
  CT_ARRAY,		
  CT_MAYCONVERT = CT_ARRAY,
  CT_VOID,		
  CT_ENUM,		
  CT_HASSIZE = CT_ENUM,  
  CT_FUNC,		
  CT_TYPEDEF,		
  CT_ATTRIB,		
  
  CT_FIELD,		
  CT_BITFIELD,		
  CT_CONSTVAL,		
  CT_EXTERN,		
  CT_KW			
};

LJ_STATIC_ASSERT(((int)CT_PTR & (int)CT_ARRAY) == CT_PTR);
LJ_STATIC_ASSERT(((int)CT_STRUCT & (int)CT_ARRAY) == CT_STRUCT);




#define CTF_BOOL	0x08000000u	
#define CTF_FP		0x04000000u	
#define CTF_CONST	0x02000000u	
#define CTF_VOLATILE	0x01000000u	
#define CTF_UNSIGNED	0x00800000u	
#define CTF_LONG	0x00400000u	
#define CTF_VLA		0x00100000u	
#define CTF_REF		0x00800000u	
#define CTF_VECTOR	0x08000000u	
#define CTF_COMPLEX	0x04000000u	
#define CTF_UNION	0x00800000u	
#define CTF_VARARG	0x00800000u	
#define CTF_SSEREGPARM	0x00400000u	

#define CTF_QUAL	(CTF_CONST|CTF_VOLATILE)
#define CTF_ALIGN	(CTMASK_ALIGN<<CTSHIFT_ALIGN)
#define CTF_UCHAR	((char)-1 > 0 ? CTF_UNSIGNED : 0)


#define CTFP_ALIGNED	0x00000001u	
#define CTFP_PACKED	0x00000002u	

#define CTFP_CCONV	0x00000001u	


#define CTMASK_CID	0x0000ffffu	
#define CTMASK_NUM	0xf0000000u	
#define CTSHIFT_NUM	28
#define CTMASK_ALIGN	15		
#define CTSHIFT_ALIGN	16
#define CTMASK_ATTRIB	255		
#define CTSHIFT_ATTRIB	16
#define CTMASK_CCONV	3		
#define CTSHIFT_CCONV	16
#define CTMASK_REGPARM	3		
#define CTSHIFT_REGPARM	18

#define CTMASK_VSIZEP	15		
#define CTSHIFT_VSIZEP	4
#define CTMASK_MSIZEP	255		
#define CTSHIFT_MSIZEP	8


#define CTBSZ_MAX	32		
#define CTBSZ_FIELD	127		
#define CTMASK_BITPOS	127
#define CTMASK_BITBSZ	127
#define CTMASK_BITCSZ	127
#define CTSHIFT_BITPOS	0
#define CTSHIFT_BITBSZ	8
#define CTSHIFT_BITCSZ	16

#define CTF_INSERT(info, field, val) \
  info = (info & ~(CTMASK_##field<<CTSHIFT_##field)) | \
	  (((CTSize)(val) & CTMASK_##field) << CTSHIFT_##field)


enum { CTCC_CDECL, CTCC_THISCALL, CTCC_FASTCALL, CTCC_STDCALL };


enum {
  CTA_NONE,		
  CTA_QUAL,		
  CTA_ALIGN,		
  CTA_SUBTYPE,		
  CTA_REDIR,		
  CTA_BAD,		
  CTA__MAX
};


#define CTSIZE_INVALID	0xffffffffu

typedef uint32_t CTInfo;	
typedef uint32_t CTSize;	
typedef uint32_t CTypeID;	
typedef uint16_t CTypeID1;	


typedef struct CType {
  CTInfo info;		
  CTSize size;		
  CTypeID1 sib;		
  CTypeID1 next;	
  GCRef name;		
} CType;

#define CTHASH_SIZE	128	
#define CTHASH_MASK	(CTHASH_SIZE-1)


#define CCALL_MAX_GPR		8
#define CCALL_MAX_FPR		8

typedef LJ_ALIGN(8) union FPRCBArg { double d; float f[2]; } FPRCBArg;



typedef LJ_ALIGN(8) struct CCallback {
  FPRCBArg fpr[CCALL_MAX_FPR];	
  intptr_t gpr[CCALL_MAX_GPR];	
  intptr_t *stack;		
  void *mcode;			
  CTypeID1 *cbid;		
  MSize sizeid;			
  MSize topid;			
  MSize slot;			
} CCallback;


typedef struct CTState {
  CType *tab;		
  CTypeID top;		
  MSize sizetab;	
  lua_State *L;		
  global_State *g;	
  GCtab *miscmap;	
  CCallback cb;		
  CTypeID1 hash[CTHASH_SIZE];  
} CTState;

#define CTINFO(ct, flags)	(((CTInfo)(ct) << CTSHIFT_NUM) + (flags))
#define CTALIGN(al)		((CTSize)(al) << CTSHIFT_ALIGN)
#define CTATTRIB(at)		((CTInfo)(at) << CTSHIFT_ATTRIB)

#define ctype_type(info)	((info) >> CTSHIFT_NUM)
#define ctype_cid(info)		((CTypeID)((info) & CTMASK_CID))
#define ctype_align(info)	(((info) >> CTSHIFT_ALIGN) & CTMASK_ALIGN)
#define ctype_attrib(info)	(((info) >> CTSHIFT_ATTRIB) & CTMASK_ATTRIB)
#define ctype_bitpos(info)	(((info) >> CTSHIFT_BITPOS) & CTMASK_BITPOS)
#define ctype_bitbsz(info)	(((info) >> CTSHIFT_BITBSZ) & CTMASK_BITBSZ)
#define ctype_bitcsz(info)	(((info) >> CTSHIFT_BITCSZ) & CTMASK_BITCSZ)
#define ctype_vsizeP(info)	(((info) >> CTSHIFT_VSIZEP) & CTMASK_VSIZEP)
#define ctype_msizeP(info)	(((info) >> CTSHIFT_MSIZEP) & CTMASK_MSIZEP)
#define ctype_cconv(info)	(((info) >> CTSHIFT_CCONV) & CTMASK_CCONV)


#define ctype_isnum(info)	(ctype_type((info)) == CT_NUM)
#define ctype_isvoid(info)	(ctype_type((info)) == CT_VOID)
#define ctype_isptr(info)	(ctype_type((info)) == CT_PTR)
#define ctype_isarray(info)	(ctype_type((info)) == CT_ARRAY)
#define ctype_isstruct(info)	(ctype_type((info)) == CT_STRUCT)
#define ctype_isfunc(info)	(ctype_type((info)) == CT_FUNC)
#define ctype_isenum(info)	(ctype_type((info)) == CT_ENUM)
#define ctype_istypedef(info)	(ctype_type((info)) == CT_TYPEDEF)
#define ctype_isattrib(info)	(ctype_type((info)) == CT_ATTRIB)
#define ctype_isfield(info)	(ctype_type((info)) == CT_FIELD)
#define ctype_isbitfield(info)	(ctype_type((info)) == CT_BITFIELD)
#define ctype_isconstval(info)	(ctype_type((info)) == CT_CONSTVAL)
#define ctype_isextern(info)	(ctype_type((info)) == CT_EXTERN)
#define ctype_hassize(info)	(ctype_type((info)) <= CT_HASSIZE)


#define ctype_isinteger(info) \
  (((info) & (CTMASK_NUM|CTF_BOOL|CTF_FP)) == CTINFO(CT_NUM, 0))
#define ctype_isinteger_or_bool(info) \
  (((info) & (CTMASK_NUM|CTF_FP)) == CTINFO(CT_NUM, 0))
#define ctype_isbool(info) \
  (((info) & (CTMASK_NUM|CTF_BOOL)) == CTINFO(CT_NUM, CTF_BOOL))
#define ctype_isfp(info) \
  (((info) & (CTMASK_NUM|CTF_FP)) == CTINFO(CT_NUM, CTF_FP))

#define ctype_ispointer(info) \
  ((ctype_type(info) >> 1) == (CT_PTR >> 1))  
#define ctype_isref(info) \
  (((info) & (CTMASK_NUM|CTF_REF)) == CTINFO(CT_PTR, CTF_REF))

#define ctype_isrefarray(info) \
  (((info) & (CTMASK_NUM|CTF_VECTOR|CTF_COMPLEX)) == CTINFO(CT_ARRAY, 0))
#define ctype_isvector(info) \
  (((info) & (CTMASK_NUM|CTF_VECTOR)) == CTINFO(CT_ARRAY, CTF_VECTOR))
#define ctype_iscomplex(info) \
  (((info) & (CTMASK_NUM|CTF_COMPLEX)) == CTINFO(CT_ARRAY, CTF_COMPLEX))

#define ctype_isvltype(info) \
  (((info) & ((CTMASK_NUM|CTF_VLA) - (2u<<CTSHIFT_NUM))) == \
   CTINFO(CT_STRUCT, CTF_VLA))  
#define ctype_isvlarray(info) \
  (((info) & (CTMASK_NUM|CTF_VLA)) == CTINFO(CT_ARRAY, CTF_VLA))

#define ctype_isxattrib(info, at) \
  (((info) & (CTMASK_NUM|CTATTRIB(CTMASK_ATTRIB))) == \
   CTINFO(CT_ATTRIB, CTATTRIB(at)))


#if LJ_64
#define CTSIZE_PTR	8
#define CTALIGN_PTR	CTALIGN(3)
#else
#define CTSIZE_PTR	4
#define CTALIGN_PTR	CTALIGN(2)
#endif

#define CTINFO_REF(ref) \
  CTINFO(CT_PTR, (CTF_CONST|CTF_REF|CTALIGN_PTR) + (ref))

#define CT_MEMALIGN	3	

#ifdef LUA_USE_ASSERT
#define lj_assertCTS(c, ...)	(lj_assertG_(cts->g, (c), __VA_ARGS__))
#else
#define lj_assertCTS(c, ...)	((void)cts)
#endif




#if LJ_TARGET_PPC
#define CTTYDEFP(_) \
  _(LINT32,		4,	CT_NUM, CTF_LONG|CTALIGN(2))
#else
#define CTTYDEFP(_)
#endif

#define CTF_LONG_IF8		(CTF_LONG * (sizeof(long) == 8))


#define CTTYDEF(_) \
  _(NONE,		0,	CT_ATTRIB, CTATTRIB(CTA_BAD)) \
  _(VOID,		-1,	CT_VOID, CTALIGN(0)) \
  _(CVOID,		-1,	CT_VOID, CTF_CONST|CTALIGN(0)) \
  _(BOOL,		1,	CT_NUM, CTF_BOOL|CTF_UNSIGNED|CTALIGN(0)) \
  _(CCHAR,		1,	CT_NUM, CTF_CONST|CTF_UCHAR|CTALIGN(0)) \
  _(INT8,		1,	CT_NUM, CTALIGN(0)) \
  _(UINT8,		1,	CT_NUM, CTF_UNSIGNED|CTALIGN(0)) \
  _(INT16,		2,	CT_NUM, CTALIGN(1)) \
  _(UINT16,		2,	CT_NUM, CTF_UNSIGNED|CTALIGN(1)) \
  _(INT32,		4,	CT_NUM, CTALIGN(2)) \
  _(UINT32,		4,	CT_NUM, CTF_UNSIGNED|CTALIGN(2)) \
  _(INT64,		8,	CT_NUM, CTF_LONG_IF8|CTALIGN(3)) \
  _(UINT64,		8,	CT_NUM, CTF_UNSIGNED|CTF_LONG_IF8|CTALIGN(3)) \
  _(INT128,		16,	CT_NUM, CTALIGN(4)) \
  _(UINT128,		16,	CT_NUM, CTF_UNSIGNED|CTALIGN(4)) \
  _(FLOAT,		4,	CT_NUM, CTF_FP|CTALIGN(2)) \
  _(DOUBLE,		8,	CT_NUM, CTF_FP|CTALIGN(3)) \
  _(COMPLEX_FLOAT,	8,	CT_ARRAY, CTF_COMPLEX|CTALIGN(2)|CTID_FLOAT) \
  _(COMPLEX_DOUBLE,	16,	CT_ARRAY, CTF_COMPLEX|CTALIGN(3)|CTID_DOUBLE) \
  _(P_VOID,	CTSIZE_PTR,	CT_PTR, CTALIGN_PTR|CTID_VOID) \
  _(P_CVOID,	CTSIZE_PTR,	CT_PTR, CTALIGN_PTR|CTID_CVOID) \
  _(P_CCHAR,	CTSIZE_PTR,	CT_PTR, CTALIGN_PTR|CTID_CCHAR) \
  _(P_UINT8,	CTSIZE_PTR,	CT_PTR, CTALIGN_PTR|CTID_UINT8) \
  _(A_CCHAR,		-1,	CT_ARRAY, CTF_CONST|CTALIGN(0)|CTID_CCHAR) \
  _(CTYPEID,		4,	CT_ENUM, CTALIGN(2)|CTID_INT32) \
  CTTYDEFP(_) \
  


enum {
#define CTTYIDDEF(id, sz, ct, info)	CTID_##id,
CTTYDEF(CTTYIDDEF)
#undef CTTYIDDEF
  
  CTID_MAX = 65536
};


#if LJ_64
#define CTID_INT_PSZ	CTID_INT64
#define CTID_UINT_PSZ	CTID_UINT64
#else
#define CTID_INT_PSZ	CTID_INT32
#define CTID_UINT_PSZ	CTID_UINT32
#endif

#if LJ_ABI_WIN
#define CTID_WCHAR	CTID_UINT16
#elif LJ_TARGET_PPC
#define CTID_WCHAR	CTID_LINT32
#else
#define CTID_WCHAR	CTID_INT32
#endif




#define CTOKDEF(_) \
  _(IDENT, "<identifier>") _(STRING, "<string>") \
  _(INTEGER, "<integer>") _(EOF, "<eof>") \
  _(OROR, "||") _(ANDAND, "&&") _(EQ, "==") _(NE, "!=") \
  _(LE, "<=") _(GE, ">=") _(SHL, "<<") _(SHR, ">>") _(DEREF, "->")


#define CDSDEF(_) \
  _(VOID) _(BOOL) _(CHAR) _(INT) _(FP) \
  _(LONG) _(LONGLONG) _(SHORT) _(COMPLEX) _(SIGNED) _(UNSIGNED) \
  _(CONST) _(VOLATILE) _(RESTRICT) _(INLINE) \
  _(TYPEDEF) _(EXTERN) _(STATIC) _(AUTO) _(REGISTER)


#define CKWDEF(_) \
  CDSDEF(_) _(EXTENSION) _(ASM) _(ATTRIBUTE) \
  _(DECLSPEC) _(CCDECL) _(PTRSZ) \
  _(STRUCT) _(UNION) _(ENUM) \
  _(SIZEOF) _(ALIGNOF)


enum {
  CTOK_OFS = 255,
#define CTOKNUM(name, sym)	CTOK_##name,
#define CKWNUM(name)		CTOK_##name,
CTOKDEF(CTOKNUM)
CKWDEF(CKWNUM)
#undef CTOKNUM
#undef CKWNUM
  CTOK_FIRSTDECL = CTOK_VOID,
  CTOK_FIRSTSCL = CTOK_TYPEDEF,
  CTOK_LASTDECLFLAG = CTOK_REGISTER,
  CTOK_LASTDECL = CTOK_ENUM
};


enum {
#define CDSFLAG(name)	CDF_##name = (1u << (CTOK_##name - CTOK_FIRSTDECL)),
CDSDEF(CDSFLAG)
#undef CDSFLAG
  CDF__END
};

#define CDF_SCL  (CDF_TYPEDEF|CDF_EXTERN|CDF_STATIC|CDF_AUTO|CDF_REGISTER)



#define ctype_ctsG(g)		(mref((g)->ctype_state, CTState))


static LJ_AINLINE CTState *ctype_cts(lua_State *L)
{
  CTState *cts = ctype_ctsG(G(L));
  cts->L = L;  
  return cts;
}


#define ctype_loadffi(L) \
  do { \
    if (!ctype_ctsG(G(L))) { \
      ptrdiff_t oldtop = (char *)L->top - mref(L->stack, char); \
      luaopen_ffi(L); \
      L->top = (TValue *)(mref(L->stack, char) + oldtop); \
    } \
  } while (0)


#define LJ_CTYPE_SAVE(cts)	CTState savects_ = *(cts)
#define LJ_CTYPE_RESTORE(cts) \
  ((cts)->top = savects_.top, \
   memcpy((cts)->hash, savects_.hash, sizeof(savects_.hash)))


static LJ_AINLINE CTypeID ctype_check(CTState *cts, CTypeID id)
{
  UNUSED(cts);
  lj_assertCTS(id > 0 && id < cts->top, "bad CTID %d", id);
  return id;
}


static LJ_AINLINE CType *ctype_get(CTState *cts, CTypeID id)
{
  return &cts->tab[ctype_check(cts, id)];
}


#define ctype_typeid(cts, ct)	((CTypeID)((ct) - (cts)->tab))


static LJ_AINLINE CType *ctype_child(CTState *cts, CType *ct)
{
  lj_assertCTS(!(ctype_isvoid(ct->info) || ctype_isstruct(ct->info) ||
	       ctype_isbitfield(ct->info)),
	       "ctype %08x has no children", ct->info);
  return ctype_get(cts, ctype_cid(ct->info));
}


static LJ_AINLINE CType *ctype_raw(CTState *cts, CTypeID id)
{
  CType *ct = ctype_get(cts, id);
  while (ctype_isattrib(ct->info)) ct = ctype_child(cts, ct);
  return ct;
}


static LJ_AINLINE CType *ctype_rawchild(CTState *cts, CType *ct)
{
  do { ct = ctype_child(cts, ct); } while (ctype_isattrib(ct->info));
  return ct;
}


static LJ_AINLINE void ctype_setname(CType *ct, GCstr *s)
{
  
  fixstring(s);
  setgcref(ct->name, obj2gco(s));
}

LJ_FUNC CTypeID lj_ctype_new(CTState *cts, CType **ctp);
LJ_FUNC CTypeID lj_ctype_intern(CTState *cts, CTInfo info, CTSize size);
LJ_FUNC void lj_ctype_addname(CTState *cts, CType *ct, CTypeID id);
LJ_FUNC CTypeID lj_ctype_getname(CTState *cts, CType **ctp, GCstr *name,
				 uint32_t tmask);
LJ_FUNC CType *lj_ctype_getfieldq(CTState *cts, CType *ct, GCstr *name,
				  CTSize *ofs, CTInfo *qual);
#define lj_ctype_getfield(cts, ct, name, ofs) \
  lj_ctype_getfieldq((cts), (ct), (name), (ofs), NULL)
LJ_FUNC CType *lj_ctype_rawref(CTState *cts, CTypeID id);
LJ_FUNC CTSize lj_ctype_size(CTState *cts, CTypeID id);
LJ_FUNC CTSize lj_ctype_vlsize(CTState *cts, CType *ct, CTSize nelem);
LJ_FUNC CTInfo lj_ctype_info(CTState *cts, CTypeID id, CTSize *szp);
LJ_FUNC CTInfo lj_ctype_info_raw(CTState *cts, CTypeID id, CTSize *szp);
LJ_FUNC cTValue *lj_ctype_meta(CTState *cts, CTypeID id, MMS mm);
LJ_FUNC GCstr *lj_ctype_repr(lua_State *L, CTypeID id, GCstr *name);
LJ_FUNC GCstr *lj_ctype_repr_int64(lua_State *L, uint64_t n, int isunsigned);
LJ_FUNC GCstr *lj_ctype_repr_complex(lua_State *L, void *sp, CTSize size);
LJ_FUNC CTState *lj_ctype_init(lua_State *L);
LJ_FUNC void lj_ctype_initfin(lua_State *L);
LJ_FUNC void lj_ctype_freestate(global_State *g);

#endif

#endif
