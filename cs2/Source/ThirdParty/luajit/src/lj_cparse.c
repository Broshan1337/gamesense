

#include "lj_obj.h"

#if LJ_HASFFI

#include "lj_gc.h"
#include "lj_err.h"
#include "lj_buf.h"
#include "lj_ctype.h"
#include "lj_cparse.h"
#include "lj_frame.h"
#include "lj_vm.h"
#include "lj_char.h"
#include "lj_strscan.h"
#include "lj_strfmt.h"



#ifdef LUA_USE_ASSERT
#define lj_assertCP(c, ...)	(lj_assertG_(G(cp->L), (c), __VA_ARGS__))
#else
#define lj_assertCP(c, ...)	((void)cp)
#endif




#define cp_str_is(str, k) \
  ((str)->len == sizeof(k)-1 && !memcmp(strdata(str), k, sizeof(k)-1))


int lj_cparse_case(GCstr *str, const char *match)
{
  MSize len;
  int n;
  for  (n = 0; (len = (MSize)*match++); n++, match += len) {
    if (str->len == len && !memcmp(match, strdata(str), len))
      return n;
  }
  return -1;
}




static const char *const ctoknames[] = {
#define CTOKSTR(name, str)	str,
CTOKDEF(CTOKSTR)
#undef CTOKSTR
  NULL
};


LJ_NORET static void cp_err(CPState *cp, ErrMsg em);

static const char *cp_tok2str(CPState *cp, CPToken tok)
{
  lj_assertCP(tok < CTOK_FIRSTDECL, "bad CPToken %d", tok);
  if (tok > CTOK_OFS)
    return ctoknames[tok-CTOK_OFS-1];
  else if (!lj_char_iscntrl(tok))
    return lj_strfmt_pushf(cp->L, "%c", tok);
  else
    return lj_strfmt_pushf(cp->L, "char(%d)", tok);
}


static LJ_AINLINE int cp_iseol(CPChar c)
{
  return (c == '\n' || c == '\r');
}


static LJ_AINLINE CPChar cp_rawpeek(CPState *cp)
{
  return (CPChar)(uint8_t)(*cp->p);
}

static LJ_NOINLINE CPChar cp_get_bs(CPState *cp);


static LJ_AINLINE CPChar cp_get(CPState *cp)
{
  cp->c = (CPChar)(uint8_t)(*cp->p++);
  if (LJ_LIKELY(cp->c != '\\')) return cp->c;
  return cp_get_bs(cp);
}


static LJ_NOINLINE CPChar cp_get_bs(CPState *cp)
{
  CPChar c2, c = cp_rawpeek(cp);
  if (!cp_iseol(c)) return cp->c;
  cp->p++;
  c2 = cp_rawpeek(cp);
  if (cp_iseol(c2) && c2 != c) cp->p++;
  cp->linenumber++;
  return cp_get(cp);
}


static LJ_AINLINE void cp_save(CPState *cp, CPChar c)
{
  lj_buf_putb(&cp->sb, c);
}


static void cp_newline(CPState *cp)
{
  CPChar c = cp_rawpeek(cp);
  if (cp_iseol(c) && c != cp->c) cp->p++;
  cp->linenumber++;
}

LJ_NORET static void cp_errmsg(CPState *cp, CPToken tok, ErrMsg em, ...)
{
  const char *msg, *tokstr;
  lua_State *L;
  va_list argp;
  if (tok == 0) {
    tokstr = NULL;
  } else if (tok == CTOK_IDENT || tok == CTOK_INTEGER || tok == CTOK_STRING ||
	     tok >= CTOK_FIRSTDECL) {
    if (cp->sb.w == cp->sb.b) cp_save(cp, '$');
    cp_save(cp, '\0');
    tokstr = cp->sb.b;
  } else {
    tokstr = cp_tok2str(cp, tok);
  }
  L = cp->L;
  va_start(argp, em);
  msg = lj_strfmt_pushvf(L, err2msg(em), argp);
  va_end(argp);
  if (tokstr)
    msg = lj_strfmt_pushf(L, err2msg(LJ_ERR_XNEAR), msg, tokstr);
  if (cp->linenumber > 1)
    msg = lj_strfmt_pushf(L, "%s at line %d", msg, cp->linenumber);
  lj_err_callermsg(L, msg);
}

LJ_NORET LJ_NOINLINE static void cp_err_token(CPState *cp, CPToken tok)
{
  cp_errmsg(cp, cp->tok, LJ_ERR_XTOKEN, cp_tok2str(cp, tok));
}

LJ_NORET LJ_NOINLINE static void cp_err_badidx(CPState *cp, CType *ct)
{
  GCstr *s = lj_ctype_repr(cp->cts->L, ctype_typeid(cp->cts, ct), NULL);
  cp_errmsg(cp, 0, LJ_ERR_FFI_BADIDX, strdata(s));
}

LJ_NORET LJ_NOINLINE static void cp_err(CPState *cp, ErrMsg em)
{
  cp_errmsg(cp, 0, em);
}




static CPToken cp_number(CPState *cp)
{
  StrScanFmt fmt;
  TValue o;
  do { cp_save(cp, cp->c); } while (lj_char_isident(cp_get(cp)));
  cp_save(cp, '\0');
  fmt = lj_strscan_scan((const uint8_t *)(cp->sb.b), sbuflen(&cp->sb)-1,
			&o, STRSCAN_OPT_C);
  if (fmt == STRSCAN_INT) cp->val.id = CTID_INT32;
  else if (fmt == STRSCAN_U32) cp->val.id = CTID_UINT32;
  else if (!(cp->mode & CPARSE_MODE_SKIP))
    cp_errmsg(cp, CTOK_INTEGER, LJ_ERR_XNUMBER);
  cp->val.u32 = (uint32_t)o.i;
  return CTOK_INTEGER;
}


static CPToken cp_ident(CPState *cp)
{
  do { cp_save(cp, cp->c); } while (lj_char_isident(cp_get(cp)));
  cp->str = lj_buf_str(cp->L, &cp->sb);
  cp->val.id = lj_ctype_getname(cp->cts, &cp->ct, cp->str, cp->tmask);
  if (ctype_type(cp->ct->info) == CT_KW)
    return ctype_cid(cp->ct->info);
  return CTOK_IDENT;
}


static CPToken cp_param(CPState *cp)
{
  CPChar c = cp_get(cp);
  TValue *o = cp->param;
  if (lj_char_isident(c) || c == '$')  
    cp_errmsg(cp, c, LJ_ERR_XSYNTAX);
  if (!o || o >= cp->L->top)
    cp_err(cp, LJ_ERR_FFI_NUMPARAM);
  cp->param = o+1;
  if (tvisstr(o)) {
    cp->str = strV(o);
    cp->val.id = 0;
    cp->ct = &cp->cts->tab[0];
    return CTOK_IDENT;
  } else if (tvisnumber(o)) {
    cp->val.i32 = numberVint(o);
    cp->val.id = CTID_INT32;
    return CTOK_INTEGER;
  } else {
    GCcdata *cd;
    if (!tviscdata(o))
      lj_err_argtype(cp->L, (int)(o-cp->L->base)+1, "type parameter");
    cd = cdataV(o);
    if (cd->ctypeid == CTID_CTYPEID)
      cp->val.id = *(CTypeID *)cdataptr(cd);
    else
      cp->val.id = cd->ctypeid;
    return '$';
  }
}


static CPToken cp_string(CPState *cp)
{
  CPChar delim = cp->c;
  cp_get(cp);
  while (cp->c != delim) {
    CPChar c = cp->c;
    if (c == '\0') cp_errmsg(cp, CTOK_EOF, LJ_ERR_XSTR);
    if (c == '\\') {
      c = cp_get(cp);
      switch (c) {
      case '\0': cp_errmsg(cp, CTOK_EOF, LJ_ERR_XSTR); break;
      case 'a': c = '\a'; break;
      case 'b': c = '\b'; break;
      case 'f': c = '\f'; break;
      case 'n': c = '\n'; break;
      case 'r': c = '\r'; break;
      case 't': c = '\t'; break;
      case 'v': c = '\v'; break;
      case 'e': c = 27; break;
      case 'x':
	c = 0;
	while (lj_char_isxdigit(cp_get(cp)))
	  c = (c<<4) + (lj_char_isdigit(cp->c) ? cp->c-'0' : (cp->c&15)+9);
	cp_save(cp, (c & 0xff));
	continue;
      default:
	if (lj_char_isdigit(c)) {
	  c -= '0';
	  if (lj_char_isdigit(cp_get(cp))) {
	    c = c*8 + (cp->c - '0');
	    if (lj_char_isdigit(cp_get(cp))) {
	      c = c*8 + (cp->c - '0');
	      cp_get(cp);
	    }
	  }
	  cp_save(cp, (c & 0xff));
	  continue;
	}
	break;
      }
    }
    cp_save(cp, c);
    cp_get(cp);
  }
  cp_get(cp);
  if (delim == '"') {
    cp->str = lj_buf_str(cp->L, &cp->sb);
    return CTOK_STRING;
  } else {
    if (sbuflen(&cp->sb) != 1) cp_err_token(cp, '\'');
    cp->val.i32 = (int32_t)(char)*cp->sb.b;
    cp->val.id = CTID_INT32;
    return CTOK_INTEGER;
  }
}


static void cp_comment_c(CPState *cp)
{
  do {
    if (cp_get(cp) == '*') {
      do {
	if (cp_get(cp) == '/') { cp_get(cp); return; }
      } while (cp->c == '*');
    }
    if (cp_iseol(cp->c)) cp_newline(cp);
  } while (cp->c != '\0');
}


static void cp_comment_cpp(CPState *cp)
{
  while (!cp_iseol(cp_get(cp)) && cp->c != '\0')
    ;
}


static CPToken cp_next_(CPState *cp)
{
  lj_buf_reset(&cp->sb);
  for (;;) {
    if (lj_char_isident(cp->c))
      return lj_char_isdigit(cp->c) ? cp_number(cp) : cp_ident(cp);
    switch (cp->c) {
    case '\n': case '\r': cp_newline(cp);  
    case ' ': case '\t': case '\v': case '\f': cp_get(cp); break;
    case '"': case '\'': return cp_string(cp);
    case '/':
      if (cp_get(cp) == '*') cp_comment_c(cp);
      else if (cp->c == '/') cp_comment_cpp(cp);
      else return '/';
      break;
    case '|':
      if (cp_get(cp) != '|') return '|';
      cp_get(cp); return CTOK_OROR;
    case '&':
      if (cp_get(cp) != '&') return '&';
      cp_get(cp); return CTOK_ANDAND;
    case '=':
      if (cp_get(cp) != '=') return '=';
      cp_get(cp); return CTOK_EQ;
    case '!':
      if (cp_get(cp) != '=') return '!';
      cp_get(cp); return CTOK_NE;
    case '<':
      if (cp_get(cp) == '=') { cp_get(cp); return CTOK_LE; }
      else if (cp->c == '<') { cp_get(cp); return CTOK_SHL; }
      return '<';
    case '>':
      if (cp_get(cp) == '=') { cp_get(cp); return CTOK_GE; }
      else if (cp->c == '>') { cp_get(cp); return CTOK_SHR; }
      return '>';
    case '-':
      if (cp_get(cp) != '>') return '-';
      cp_get(cp); return CTOK_DEREF;
    case '$':
      return cp_param(cp);
    case '\0': return CTOK_EOF;
    default: { CPToken c = cp->c; cp_get(cp); return c; }
    }
  }
}

static LJ_NOINLINE CPToken cp_next(CPState *cp)
{
  return (cp->tok = cp_next_(cp));
}




#define CPNS_DEFAULT \
  ((1u<<CT_KW)|(1u<<CT_TYPEDEF)|(1u<<CT_FUNC)|(1u<<CT_EXTERN)|(1u<<CT_CONSTVAL))
#define CPNS_STRUCT	((1u<<CT_KW)|(1u<<CT_STRUCT)|(1u<<CT_ENUM))

typedef CTypeID CPDeclIdx;	
typedef uint32_t CPscl;		


typedef struct CPDecl {
  CPDeclIdx top;	
  CPDeclIdx pos;	
  CPDeclIdx specpos;	
  uint32_t mode;	
  CPState *cp;		
  GCstr *name;		
  GCstr *redir;		
  CTypeID nameid;	
  CTInfo attr;		
  CTInfo fattr;		
  CTInfo specattr;	
  CTInfo specfattr;	
  CTSize bits;		
  CType stack[CPARSE_MAX_DECLSTACK];  
} CPDecl;


static CPscl cp_decl_spec(CPState *cp, CPDecl *decl, CPscl scl);
static void cp_declarator(CPState *cp, CPDecl *decl);
static CTypeID cp_decl_abstract(CPState *cp);


static void cp_init(CPState *cp)
{
  cp->linenumber = 1;
  cp->depth = 0;
  cp->curpack = 0;
  cp->packstack[0] = 255;
  lj_buf_init(cp->L, &cp->sb);
  lj_assertCP(cp->p != NULL, "uninitialized cp->p");
  cp_get(cp);  
  cp->tok = 0;
  cp->tmask = CPNS_DEFAULT;
  cp_next(cp);  
}


static void cp_cleanup(CPState *cp)
{
  global_State *g = G(cp->L);
  lj_buf_free(g, &cp->sb);
}


static int cp_opt(CPState *cp, CPToken tok)
{
  if (cp->tok == tok) { cp_next(cp); return 1; }
  return 0;
}


static void cp_check(CPState *cp, CPToken tok)
{
  if (cp->tok != tok) cp_err_token(cp, tok);
  cp_next(cp);
}


static int cp_istypedecl(CPState *cp)
{
  if (cp->tok >= CTOK_FIRSTDECL && cp->tok <= CTOK_LASTDECL) return 1;
  if (cp->tok == CTOK_IDENT && ctype_istypedef(cp->ct->info)) return 1;
  if (cp->tok == '$') return 1;
  return 0;
}




static void cp_expr_unary(CPState *cp, CPValue *k);
static void cp_expr_sub(CPState *cp, CPValue *k, int pri);




static void cp_expr_comma(CPState *cp, CPValue *k)
{
  do { cp_expr_sub(cp, k, 0); } while (cp_opt(cp, ','));
}


static void cp_expr_sizeof(CPState *cp, CPValue *k, int wantsz)
{
  CTSize sz;
  CTInfo info;
  if (cp_opt(cp, '(')) {
    if (cp_istypedecl(cp))
      k->id = cp_decl_abstract(cp);
    else
      cp_expr_comma(cp, k);
    cp_check(cp, ')');
  } else {
    cp_expr_unary(cp, k);
  }
  info = lj_ctype_info_raw(cp->cts, k->id, &sz);
  if (wantsz) {
    if (sz != CTSIZE_INVALID)
      k->u32 = sz;
    else if (k->id != CTID_A_CCHAR)  
      cp_err(cp, LJ_ERR_FFI_INVSIZE);
  } else {
    k->u32 = 1u << ctype_align(info);
  }
  k->id = CTID_UINT32;  
}


static void cp_expr_prefix(CPState *cp, CPValue *k)
{
  if (cp->tok == CTOK_INTEGER) {
    *k = cp->val; cp_next(cp);
  } else if (cp_opt(cp, '+')) {
    cp_expr_unary(cp, k);  
  } else if (cp_opt(cp, '-')) {
    cp_expr_unary(cp, k); k->i32 = (int32_t)(~(uint32_t)k->i32+1);
  } else if (cp_opt(cp, '~')) {
    cp_expr_unary(cp, k); k->i32 = ~k->i32;
  } else if (cp_opt(cp, '!')) {
    cp_expr_unary(cp, k); k->i32 = !k->i32; k->id = CTID_INT32;
  } else if (cp_opt(cp, '(')) {
    if (cp_istypedecl(cp)) {  
      CTypeID id = cp_decl_abstract(cp);
      cp_check(cp, ')');
      cp_expr_unary(cp, k);
      k->id = id;  
    } else {  
      cp_expr_comma(cp, k);
      cp_check(cp, ')');
    }
  } else if (cp_opt(cp, '*')) {  
    CType *ct;
    cp_expr_unary(cp, k);
    ct = lj_ctype_rawref(cp->cts, k->id);
    if (!ctype_ispointer(ct->info))
      cp_err_badidx(cp, ct);
    k->u32 = 0; k->id = ctype_cid(ct->info);
  } else if (cp_opt(cp, '&')) {  
    cp_expr_unary(cp, k);
    k->id = lj_ctype_intern(cp->cts, CTINFO(CT_PTR, CTALIGN_PTR+k->id),
			    CTSIZE_PTR);
  } else if (cp_opt(cp, CTOK_SIZEOF)) {
    cp_expr_sizeof(cp, k, 1);
  } else if (cp_opt(cp, CTOK_ALIGNOF)) {
    cp_expr_sizeof(cp, k, 0);
  } else if (cp->tok == CTOK_IDENT) {
    if (ctype_type(cp->ct->info) == CT_CONSTVAL) {
      k->u32 = cp->ct->size; k->id = ctype_cid(cp->ct->info);
    } else if (ctype_type(cp->ct->info) == CT_EXTERN) {
      k->u32 = cp->val.id; k->id = ctype_cid(cp->ct->info);
    } else if (ctype_type(cp->ct->info) == CT_FUNC) {
      k->u32 = cp->val.id; k->id = cp->val.id;
    } else {
      goto err_expr;
    }
    cp_next(cp);
  } else if (cp->tok == CTOK_STRING) {
    CTSize sz = cp->str->len;
    while (cp_next(cp) == CTOK_STRING)
      sz += cp->str->len;
    k->u32 = sz + 1;
    k->id = CTID_A_CCHAR;
  } else {
  err_expr:
    cp_errmsg(cp, cp->tok, LJ_ERR_XSYMBOL);
  }
}


static void cp_expr_postfix(CPState *cp, CPValue *k)
{
  for (;;) {
    CType *ct;
    if (cp_opt(cp, '[')) {  
      CPValue k2;
      cp_expr_comma(cp, &k2);
      ct = lj_ctype_rawref(cp->cts, k->id);
      if (!ctype_ispointer(ct->info)) {
	ct = lj_ctype_rawref(cp->cts, k2.id);
	if (!ctype_ispointer(ct->info))
	  cp_err_badidx(cp, ct);
      }
      cp_check(cp, ']');
      k->u32 = 0;
    } else if (cp->tok == '.' || cp->tok == CTOK_DEREF) {  
      CTSize ofs;
      CType *fct;
      ct = lj_ctype_rawref(cp->cts, k->id);
      if (cp->tok == CTOK_DEREF) {
	if (!ctype_ispointer(ct->info))
	  cp_err_badidx(cp, ct);
	ct = lj_ctype_rawref(cp->cts, ctype_cid(ct->info));
      }
      cp_next(cp);
      if (cp->tok != CTOK_IDENT) cp_err_token(cp, CTOK_IDENT);
      if (!ctype_isstruct(ct->info) || ct->size == CTSIZE_INVALID ||
	  !(fct = lj_ctype_getfield(cp->cts, ct, cp->str, &ofs)) ||
	  ctype_isbitfield(fct->info)) {
	GCstr *s = lj_ctype_repr(cp->cts->L, ctype_typeid(cp->cts, ct), NULL);
	cp_errmsg(cp, 0, LJ_ERR_FFI_BADMEMBER, strdata(s), strdata(cp->str));
      }
      ct = fct;
      k->u32 = ctype_isconstval(ct->info) ? ct->size : 0;
      cp_next(cp);
    } else {
      return;
    }
    k->id = ctype_cid(ct->info);
  }
}


static void cp_expr_infix(CPState *cp, CPValue *k, int pri)
{
  CPValue k2;
  k2.u32 = 0; k2.id = 0;  
  for (;;) {
    switch (pri) {
    case 0:
      if (cp_opt(cp, '?')) {
	CPValue k3;
	cp_expr_comma(cp, &k2);  
	cp_check(cp, ':');
	cp_expr_sub(cp, &k3, 0);
	k->u32 = k->u32 ? k2.u32 : k3.u32;
	k->id = k2.id > k3.id ? k2.id : k3.id;
	continue;
      }
      
    case 1:
      if (cp_opt(cp, CTOK_OROR)) {
	cp_expr_sub(cp, &k2, 2); k->i32 = k->u32 || k2.u32; k->id = CTID_INT32;
	continue;
      }
      
    case 2:
      if (cp_opt(cp, CTOK_ANDAND)) {
	cp_expr_sub(cp, &k2, 3); k->i32 = k->u32 && k2.u32; k->id = CTID_INT32;
	continue;
      }
      
    case 3:
      if (cp_opt(cp, '|')) {
	cp_expr_sub(cp, &k2, 4); k->u32 = k->u32 | k2.u32; goto arith_result;
      }
      
    case 4:
      if (cp_opt(cp, '^')) {
	cp_expr_sub(cp, &k2, 5); k->u32 = k->u32 ^ k2.u32; goto arith_result;
      }
      
    case 5:
      if (cp_opt(cp, '&')) {
	cp_expr_sub(cp, &k2, 6); k->u32 = k->u32 & k2.u32; goto arith_result;
      }
      
    case 6:
      if (cp_opt(cp, CTOK_EQ)) {
	cp_expr_sub(cp, &k2, 7); k->i32 = k->u32 == k2.u32; k->id = CTID_INT32;
	continue;
      } else if (cp_opt(cp, CTOK_NE)) {
	cp_expr_sub(cp, &k2, 7); k->i32 = k->u32 != k2.u32; k->id = CTID_INT32;
	continue;
      }
      
    case 7:
      if (cp_opt(cp, '<')) {
	cp_expr_sub(cp, &k2, 8);
	if (k->id == CTID_INT32 && k2.id == CTID_INT32)
	  k->i32 = k->i32 < k2.i32;
	else
	  k->i32 = k->u32 < k2.u32;
	k->id = CTID_INT32;
	continue;
      } else if (cp_opt(cp, '>')) {
	cp_expr_sub(cp, &k2, 8);
	if (k->id == CTID_INT32 && k2.id == CTID_INT32)
	  k->i32 = k->i32 > k2.i32;
	else
	  k->i32 = k->u32 > k2.u32;
	k->id = CTID_INT32;
	continue;
      } else if (cp_opt(cp, CTOK_LE)) {
	cp_expr_sub(cp, &k2, 8);
	if (k->id == CTID_INT32 && k2.id == CTID_INT32)
	  k->i32 = k->i32 <= k2.i32;
	else
	  k->i32 = k->u32 <= k2.u32;
	k->id = CTID_INT32;
	continue;
      } else if (cp_opt(cp, CTOK_GE)) {
	cp_expr_sub(cp, &k2, 8);
	if (k->id == CTID_INT32 && k2.id == CTID_INT32)
	  k->i32 = k->i32 >= k2.i32;
	else
	  k->i32 = k->u32 >= k2.u32;
	k->id = CTID_INT32;
	continue;
      }
      
    case 8:
      if (cp_opt(cp, CTOK_SHL)) {
	cp_expr_sub(cp, &k2, 9); k->u32 = k->u32 << k2.u32;
	continue;
      } else if (cp_opt(cp, CTOK_SHR)) {
	cp_expr_sub(cp, &k2, 9);
	if (k->id == CTID_INT32)
	  k->i32 = k->i32 >> k2.i32;
	else
	  k->u32 = k->u32 >> k2.u32;
	continue;
      }
      
    case 9:
      if (cp_opt(cp, '+')) {
	cp_expr_sub(cp, &k2, 10); k->u32 = k->u32 + k2.u32;
      arith_result:
	if (k2.id > k->id) k->id = k2.id;  
	continue;
      } else if (cp_opt(cp, '-')) {
	cp_expr_sub(cp, &k2, 10); k->u32 = k->u32 - k2.u32; goto arith_result;
      }
      
    case 10:
      if (cp_opt(cp, '*')) {
	cp_expr_unary(cp, &k2); k->u32 = k->u32 * k2.u32; goto arith_result;
      } else if (cp_opt(cp, '/')) {
	cp_expr_unary(cp, &k2);
	if (k2.id > k->id) k->id = k2.id;  
	if (k2.u32 == 0 ||
	    (k->id == CTID_INT32 && k->u32 == 0x80000000u && k2.i32 == -1))
	  cp_err(cp, LJ_ERR_BADVAL);
	if (k->id == CTID_INT32)
	  k->i32 = k->i32 / k2.i32;
	else
	  k->u32 = k->u32 / k2.u32;
	continue;
      } else if (cp_opt(cp, '%')) {
	cp_expr_unary(cp, &k2);
	if (k2.id > k->id) k->id = k2.id;  
	if (k2.u32 == 0 ||
	    (k->id == CTID_INT32 && k->u32 == 0x80000000u && k2.i32 == -1))
	  cp_err(cp, LJ_ERR_BADVAL);
	if (k->id == CTID_INT32)
	  k->i32 = k->i32 % k2.i32;
	else
	  k->u32 = k->u32 % k2.u32;
	continue;
      }
    default:
      return;
    }
  }
}


static void cp_expr_unary(CPState *cp, CPValue *k)
{
  if (++cp->depth > CPARSE_MAX_DECLDEPTH) cp_err(cp, LJ_ERR_XLEVELS);
  cp_expr_prefix(cp, k);
  cp_expr_postfix(cp, k);
  cp->depth--;
}


static void cp_expr_sub(CPState *cp, CPValue *k, int pri)
{
  cp_expr_unary(cp, k);
  cp_expr_infix(cp, k, pri);
}


static void cp_expr_kint(CPState *cp, CPValue *k)
{
  CType *ct;
  cp_expr_sub(cp, k, 0);
  ct = ctype_raw(cp->cts, k->id);
  if (!ctype_isinteger(ct->info)) cp_err(cp, LJ_ERR_BADVAL);
}


static CTSize cp_expr_ksize(CPState *cp)
{
  CPValue k;
  cp_expr_kint(cp, &k);
  if (k.u32 >= 0x80000000u) cp_err(cp, LJ_ERR_FFI_INVSIZE);
  return k.u32;
}




static CPDeclIdx cp_add(CPDecl *decl, CTInfo info, CTSize size)
{
  CPDeclIdx top = decl->top;
  if (top >= CPARSE_MAX_DECLSTACK) cp_err(decl->cp, LJ_ERR_XLEVELS);
  decl->stack[top].info = info;
  decl->stack[top].size = size;
  decl->stack[top].sib = 0;
  setgcrefnull(decl->stack[top].name);
  decl->stack[top].next = decl->stack[decl->pos].next;
  decl->stack[decl->pos].next = (CTypeID1)top;
  decl->top = top+1;
  return top;
}


static CPDeclIdx cp_push(CPDecl *decl, CTInfo info, CTSize size)
{
  return (decl->pos = cp_add(decl, info, size));
}


static void cp_push_attributes(CPDecl *decl)
{
  CType *ct = &decl->stack[decl->pos];
  if (ctype_isfunc(ct->info)) {  
#if LJ_TARGET_X86
    if ((decl->fattr & CTFP_CCONV))
      ct->info = (ct->info & (CTMASK_NUM|CTF_VARARG|CTMASK_CID)) +
		 (decl->fattr & ~CTMASK_CID);
#endif
  } else {
    if ((decl->attr & CTFP_ALIGNED) && !(decl->mode & CPARSE_MODE_FIELD))
      cp_push(decl, CTINFO(CT_ATTRIB, CTATTRIB(CTA_ALIGN)),
	      ctype_align(decl->attr));
  }
}


static void cp_push_type(CPDecl *decl, CTypeID id)
{
  CType *ct = ctype_get(decl->cp->cts, id);
  CTInfo info = ct->info;
  CTSize size = ct->size;
  switch (ctype_type(info)) {
  case CT_STRUCT: case CT_ENUM:
    cp_push(decl, CTINFO(CT_TYPEDEF, id), 0);  
    if ((decl->attr & CTF_QUAL)) {  
      cp_push(decl, CTINFO(CT_ATTRIB, CTATTRIB(CTA_QUAL)),
	      (decl->attr & CTF_QUAL));
      decl->attr &= ~CTF_QUAL;
    }
    break;
  case CT_ATTRIB:
    if (ctype_isxattrib(info, CTA_QUAL))
      decl->attr &= ~size;  
    cp_push_type(decl, ctype_cid(info));  
    cp_push(decl, info & ~CTMASK_CID, size);  
    break;
  case CT_ARRAY:
    if ((ct->info & (CTF_VECTOR|CTF_COMPLEX))) {
      info |= (decl->attr & CTF_QUAL);
      decl->attr &= ~CTF_QUAL;
    }
    cp_push_type(decl, ctype_cid(info));  
    cp_push(decl, info & ~CTMASK_CID, size);  
    decl->stack[decl->pos].sib = 1;  
    
    break;
  case CT_FUNC:
    
    decl->stack[cp_push(decl, info, size)].sib = ct->sib;
    break;
  default:
    
    cp_push(decl, info|(decl->attr & CTF_QUAL), size);
    decl->attr &= ~CTF_QUAL;
    break;
  }
}


static CTypeID cp_decl_intern(CPState *cp, CPDecl *decl)
{
  CTypeID id = 0;
  CPDeclIdx idx = 0;
  CTSize csize = CTSIZE_INVALID;
  CTSize cinfo = 0;
  do {
    CType *ct = &decl->stack[idx];
    CTInfo info = ct->info;
    CTInfo size = ct->size;
    
    idx = ct->next;
    if (ctype_istypedef(info)) {
      lj_assertCP(id == 0, "typedef not at toplevel");
      id = ctype_cid(info);
      
      cinfo = ctype_get(cp->cts, id)->info;
      csize = ctype_get(cp->cts, id)->size;
      lj_assertCP(ctype_isstruct(cinfo) || ctype_isenum(cinfo),
		  "typedef of bad type");
    } else if (ctype_isfunc(info)) {  
      CType *fct;
      CTypeID fid;
      CTypeID sib;
      if (id) {
	CType *refct = ctype_raw(cp->cts, id);
	
	if (ctype_isfunc(refct->info) || ctype_isrefarray(refct->info))
	  cp_err(cp, LJ_ERR_FFI_INVTYPE);
      }
      
      while (idx) {
	CType *ctn = &decl->stack[idx];
	if (!ctype_isattrib(ctn->info)) break;
	idx = ctn->next;  
      }
      sib = ct->sib;  
      fid = lj_ctype_new(cp->cts, &fct);
      csize = CTSIZE_INVALID;
      fct->info = cinfo = info + id;
      fct->size = size;
      fct->sib = sib;
      id = fid;
    } else if (ctype_isattrib(info)) {
      if (ctype_isxattrib(info, CTA_QUAL))
	cinfo |= size;
      else if (ctype_isxattrib(info, CTA_ALIGN))
	CTF_INSERT(cinfo, ALIGN, size);
      id = lj_ctype_intern(cp->cts, info+id, size);
      
    } else {
      if (ctype_isnum(info)) {  
	lj_assertCP(id == 0, "number not at toplevel");
	if (!(info & CTF_BOOL)) {
	  CTSize msize = ctype_msizeP(decl->attr);
	  CTSize vsize = ctype_vsizeP(decl->attr);
	  if (msize && (!(info & CTF_FP) || (msize == 4 || msize == 8))) {
	    CTSize malign = lj_fls(msize);
	    if (malign > 4) malign = 4;  
	    CTF_INSERT(info, ALIGN, malign);
	    size = msize;  
	  }
	  if (vsize) {  
	    CTSize esize = lj_fls(size);
	    if (vsize >= esize) {
	      
	      id = lj_ctype_intern(cp->cts, info, size);
	      
	      size = (1u << vsize);
	      if (vsize > 4) vsize = 4;  
	      if (ctype_align(info) > vsize) vsize = ctype_align(info);
	      info = CTINFO(CT_ARRAY, (info & CTF_QUAL) + CTF_VECTOR +
				      CTALIGN(vsize));
	    }
	  }
	}
      } else if (ctype_isptr(info)) {
	
	if (id && ctype_isref(ctype_raw(cp->cts, id)->info))
	  cp_err(cp, LJ_ERR_FFI_INVTYPE);
	if (ctype_isref(info)) {
	  info &= ~CTF_VOLATILE;  
	  
	  while (idx) {
	    CType *ctn = &decl->stack[idx];
	    if (!ctype_isattrib(ctn->info)) break;
	    idx = ctn->next;  
	  }
	}
      } else if (ctype_isarray(info)) {  
	if (ct->sib == 0) {  
	  if (ctype_isref(cinfo))  
	    cp_err(cp, LJ_ERR_FFI_INVTYPE);
	  
	  if (ctype_isvltype(cinfo) || csize == CTSIZE_INVALID)
	    cp_err(cp, LJ_ERR_FFI_INVSIZE);
	  
	  if (size != CTSIZE_INVALID) {
	    uint64_t xsz = (uint64_t)size * csize;
	    if (xsz >= 0x80000000u) cp_err(cp, LJ_ERR_FFI_INVSIZE);
	    size = (CTSize)xsz;
	  }
	}
	if ((cinfo & CTF_ALIGN) > (info & CTF_ALIGN))  
	  info = (info & ~CTF_ALIGN) | (cinfo & CTF_ALIGN);
	info |= (cinfo & CTF_QUAL);  
      } else {
	lj_assertCP(ctype_isvoid(info), "bad ctype %08x", info);
      }
      csize = size;
      cinfo = info+id;
      id = lj_ctype_intern(cp->cts, info+id, size);
    }
  } while (idx);
  return id;
}




static void cp_decl_reset(CPDecl *decl)
{
  decl->pos = decl->specpos;
  decl->top = decl->specpos+1;
  decl->stack[decl->specpos].next = 0;
  decl->attr = decl->specattr;
  decl->fattr = decl->specfattr;
  decl->name = NULL;
  decl->redir = NULL;
}



static CTypeID cp_decl_constinit(CPState *cp, CType **ctp, CTypeID ctypeid)
{
  CType *ctt = ctype_get(cp->cts, ctypeid);
  CTInfo info;
  CTSize size;
  CPValue k;
  CTypeID constid;
  while (ctype_isattrib(ctt->info)) {  
    ctypeid = ctype_cid(ctt->info);  
    ctt = ctype_get(cp->cts, ctypeid);
  }
  info = ctt->info;
  size = ctt->size;
  if (!ctype_isinteger(info) || !(info & CTF_CONST) || size > 4)
    cp_err(cp, LJ_ERR_FFI_INVTYPE);
  cp_check(cp, '=');
  cp_expr_sub(cp, &k, 0);
  constid = lj_ctype_new(cp->cts, ctp);
  (*ctp)->info = CTINFO(CT_CONSTVAL, CTF_CONST|ctypeid);
  k.u32 <<= 8*(4-size);
  if ((info & CTF_UNSIGNED))
    k.u32 >>= 8*(4-size);
  else
    k.u32 = (uint32_t)((int32_t)k.u32 >> 8*(4-size));
  (*ctp)->size = k.u32;
  return constid;
}


static CTSize cp_decl_sizeattr(CPState *cp)
{
  CTSize sz;
  uint32_t oldtmask = cp->tmask;
  cp->tmask = CPNS_DEFAULT;  
  cp_check(cp, '(');
  sz = cp_expr_ksize(cp);
  cp->tmask = oldtmask;
  cp_check(cp, ')');
  return sz;
}


static void cp_decl_align(CPState *cp, CPDecl *decl)
{
  CTSize al = 4;  
  if (cp->tok == '(') {
    al = cp_decl_sizeattr(cp);
    al = al ? lj_fls(al) : 0;
  }
  CTF_INSERT(decl->attr, ALIGN, al);
  decl->attr |= CTFP_ALIGNED;
}


static void cp_decl_asm(CPState *cp, CPDecl *decl)
{
  UNUSED(decl);
  cp_next(cp);
  cp_check(cp, '(');
  if (cp->tok == CTOK_STRING) {
    GCstr *str = cp->str;
    while (cp_next(cp) == CTOK_STRING) {
      lj_strfmt_pushf(cp->L, "%s%s", strdata(str), strdata(cp->str));
      cp->L->top--;
      str = strV(cp->L->top);
    }
    decl->redir = str;
  }
  cp_check(cp, ')');
}


static void cp_decl_mode(CPState *cp, CPDecl *decl)
{
  cp_check(cp, '(');
  if (cp->tok == CTOK_IDENT) {
    const char *s = strdata(cp->str);
    CTSize sz = 0, vlen = 0;
    if (s[0] == '_' && s[1] == '_') s += 2;
    if (*s == 'V') {
      s++;
      vlen = *s++ - '0';
      if (*s >= '0' && *s <= '9')
	vlen = vlen*10 + (*s++ - '0');
    }
    switch (*s++) {
    case 'Q': sz = 1; break;
    case 'H': sz = 2; break;
    case 'S': sz = 4; break;
    case 'D': sz = 8; break;
    case 'T': sz = 16; break;
    case 'O': sz = 32; break;
    default: goto bad_size;
    }
    if (*s == 'I' || *s == 'F') {
      CTF_INSERT(decl->attr, MSIZEP, sz);
      if (vlen) CTF_INSERT(decl->attr, VSIZEP, lj_fls(vlen*sz));
    }
  bad_size:
    cp_next(cp);
  }
  cp_check(cp, ')');
}


static void cp_decl_gccattribute(CPState *cp, CPDecl *decl)
{
  cp_next(cp);
  cp_check(cp, '(');
  cp_check(cp, '(');
  while (cp->tok != ')') {
    if (cp->tok == CTOK_IDENT) {
      GCstr *attrstr = cp->str;
      cp_next(cp);
      switch (lj_cparse_case(attrstr,
		"\007aligned" "\013__aligned__"
		"\006packed" "\012__packed__"
		"\004mode" "\010__mode__"
		"\013vector_size" "\017__vector_size__"
#if LJ_TARGET_X86
		"\007regparm" "\013__regparm__"
		"\005cdecl"  "\011__cdecl__"
		"\010thiscall" "\014__thiscall__"
		"\010fastcall" "\014__fastcall__"
		"\007stdcall" "\013__stdcall__"
		"\012sseregparm" "\016__sseregparm__"
#endif
	      )) {
      case 0: case 1: 
	cp_decl_align(cp, decl);
	break;
      case 2: case 3: 
	decl->attr |= CTFP_PACKED;
	break;
      case 4: case 5: 
	cp_decl_mode(cp, decl);
	break;
      case 6: case 7: 
	{
	  CTSize vsize = cp_decl_sizeattr(cp);
	  if (vsize) CTF_INSERT(decl->attr, VSIZEP, lj_fls(vsize));
	}
	break;
#if LJ_TARGET_X86
      case 8: case 9: 
	CTF_INSERT(decl->fattr, REGPARM, cp_decl_sizeattr(cp));
	decl->fattr |= CTFP_CCONV;
	break;
      case 10: case 11: 
	CTF_INSERT(decl->fattr, CCONV, CTCC_CDECL);
	decl->fattr |= CTFP_CCONV;
	break;
      case 12: case 13: 
	CTF_INSERT(decl->fattr, CCONV, CTCC_THISCALL);
	decl->fattr |= CTFP_CCONV;
	break;
      case 14: case 15: 
	CTF_INSERT(decl->fattr, CCONV, CTCC_FASTCALL);
	decl->fattr |= CTFP_CCONV;
	break;
      case 16: case 17: 
	CTF_INSERT(decl->fattr, CCONV, CTCC_STDCALL);
	decl->fattr |= CTFP_CCONV;
	break;
      case 18: case 19: 
	decl->fattr |= CTF_SSEREGPARM;
	decl->fattr |= CTFP_CCONV;
	break;
#endif
      default:  
	goto skip_attr;
      }
    } else if (cp->tok >= CTOK_FIRSTDECL) {  
      cp_next(cp);
    skip_attr:
      if (cp_opt(cp, '(')) {
	while (cp->tok != ')' && cp->tok != CTOK_EOF) cp_next(cp);
	cp_check(cp, ')');
      }
    } else {
      break;
    }
    if (!cp_opt(cp, ',')) break;
  }
  cp_check(cp, ')');
  cp_check(cp, ')');
}


static void cp_decl_msvcattribute(CPState *cp, CPDecl *decl)
{
  cp_next(cp);
  cp_check(cp, '(');
  while (cp->tok == CTOK_IDENT) {
    GCstr *attrstr = cp->str;
    cp_next(cp);
    if (cp_str_is(attrstr, "align")) {
      cp_decl_align(cp, decl);
    } else {  
      if (cp_opt(cp, '(')) {
	while (cp->tok != ')' && cp->tok != CTOK_EOF) cp_next(cp);
	cp_check(cp, ')');
      }
    }
  }
  cp_check(cp, ')');
}


static void cp_decl_attributes(CPState *cp, CPDecl *decl)
{
  for (;;) {
    switch (cp->tok) {
    case CTOK_CONST: decl->attr |= CTF_CONST; break;
    case CTOK_VOLATILE: decl->attr |= CTF_VOLATILE; break;
    case CTOK_RESTRICT: break;  
    case CTOK_EXTENSION: break;  
    case CTOK_ATTRIBUTE: cp_decl_gccattribute(cp, decl); continue;
    case CTOK_ASM: cp_decl_asm(cp, decl); continue;
    case CTOK_DECLSPEC: cp_decl_msvcattribute(cp, decl); continue;
    case CTOK_CCDECL:
#if LJ_TARGET_X86
      CTF_INSERT(decl->fattr, CCONV, cp->ct->size);
      decl->fattr |= CTFP_CCONV;
#endif
      break;
    case CTOK_PTRSZ:
#if LJ_64
      CTF_INSERT(decl->attr, MSIZEP, cp->ct->size);
#endif
      break;
    default: return;
    }
    cp_next(cp);
  }
}


static CTypeID cp_struct_name(CPState *cp, CPDecl *sdecl, CTInfo info)
{
  CTypeID sid;
  CType *ct;
  cp->tmask = CPNS_STRUCT;
  cp_next(cp);
  cp_decl_attributes(cp, sdecl);
  cp->tmask = CPNS_DEFAULT;
  if (cp->tok != '{') {
    if (cp->tok != CTOK_IDENT) cp_err_token(cp, CTOK_IDENT);
    if (cp->val.id) {  
      sid = cp->val.id;
      ct = cp->ct;
      if ((ct->info ^ info) & (CTMASK_NUM|CTF_UNION))  
	cp_errmsg(cp, 0, LJ_ERR_FFI_REDEF, strdata(gco2str(gcref(ct->name))));
    } else {  
      if ((cp->mode & CPARSE_MODE_NOIMPLICIT))
	cp_errmsg(cp, 0, LJ_ERR_FFI_BADTAG, strdata(cp->str));
      sid = lj_ctype_new(cp->cts, &ct);
      ct->info = info;
      ct->size = CTSIZE_INVALID;
      ctype_setname(ct, cp->str);
      lj_ctype_addname(cp->cts, ct, sid);
    }
    cp_next(cp);
  } else {  
    sid = lj_ctype_new(cp->cts, &ct);
    ct->info = info;
    ct->size = CTSIZE_INVALID;
  }
  if (cp->tok == '{') {
    if (ct->size != CTSIZE_INVALID || ct->sib)
      cp_errmsg(cp, 0, LJ_ERR_FFI_REDEF, strdata(gco2str(gcref(ct->name))));
    ct->sib = 1;  
  }
  return sid;
}


static CTSize cp_field_align(CPState *cp, CType *ct, CTInfo info)
{
  CTSize align = ctype_align(info);
  UNUSED(cp); UNUSED(ct);
#if (LJ_TARGET_X86 && !LJ_ABI_WIN) || (LJ_TARGET_ARM && __APPLE__)
  
  if (align > 2 && !(info & CTFP_ALIGNED)) {
    if (ctype_isarray(info) && !(info & CTF_VECTOR)) {
      do {
	ct = ctype_rawchild(cp->cts, ct);
	info = ct->info;
      } while (ctype_isarray(info) && !(info & CTF_VECTOR));
    }
    if (ctype_isnum(info) || ctype_isenum(info))
      align = 2;
  }
#endif
  return align;
}


static void cp_struct_layout(CPState *cp, CTypeID sid, CTInfo sattr)
{
  CTSize bofs = 0, bmaxofs = 0;  
  CTSize maxalign = ctype_align(sattr);
  CType *sct = ctype_get(cp->cts, sid);
  CTInfo sinfo = sct->info;
  CTypeID fieldid = sct->sib;
  while (fieldid) {
    CType *ct = ctype_get(cp->cts, fieldid);
    CTInfo attr = ct->size;  

    if (ctype_isfield(ct->info) ||
	(ctype_isxattrib(ct->info, CTA_SUBTYPE) && attr)) {
      CTSize align, amask;  
      CTSize sz;
      CTInfo info = lj_ctype_info(cp->cts, ctype_cid(ct->info), &sz);
      CTSize bsz, csz = 8*sz;  
      sinfo |= (info & (CTF_QUAL|CTF_VLA));  

      
      if (sz >= 0x20000000u || bofs + csz < bofs || (info & CTF_VLA)) {
	if (!(sz == CTSIZE_INVALID && ctype_isarray(info) &&
	      !(sinfo & CTF_UNION)))
	  cp_err(cp, LJ_ERR_FFI_INVSIZE);
	csz = sz = 0;  
      }
      align = cp_field_align(cp, ct, info);
      if (((attr|sattr) & CTFP_PACKED) ||
	  ((attr & CTFP_ALIGNED) && ctype_align(attr) > align))
	align = ctype_align(attr);
      if (cp->packstack[cp->curpack] < align)
	align = cp->packstack[cp->curpack];
      bsz = ctype_bitcsz(ct->info);  
      if (align > maxalign && bsz) maxalign = align;
      amask = (8u << align) - 1;

      if (bsz == CTBSZ_FIELD || !ctype_isfield(ct->info)) {
	bsz = csz;  
	bofs = (bofs + amask) & ~amask;  
	ct->size = (bofs >> 3);  
	if (ctype_isfield(ct->info))
	  ct->info = CTINFO(CT_FIELD, ctype_cid(ct->info)) + CTALIGN(align);
      } else {  
	if (bsz == 0 || (attr & CTFP_ALIGNED) ||
	    (!((attr|sattr) & CTFP_PACKED) && (bofs & amask) + bsz > csz))
	  bofs = (bofs + amask) & ~amask;  

	
	if (bsz == csz && (bofs & amask) == 0) {
	  ct->info = CTINFO(CT_FIELD, ctype_cid(ct->info)) +
		     CTALIGN(lj_fls(sz));
	  ct->size = (bofs >> 3);  
	} else {
	  if (csz > amask+1 && bsz <= amask+1)
	    csz = amask+1;  
	  ct->info = CTINFO(CT_BITFIELD,
	    (info & (CTF_QUAL|CTF_UNSIGNED|CTF_BOOL)) +
	    (csz << (CTSHIFT_BITCSZ-3)) + (bsz << CTSHIFT_BITBSZ));
#if LJ_BE
	  ct->info += ((csz - (bofs & (csz-1)) - bsz) << CTSHIFT_BITPOS);
#else
	  ct->info += ((bofs & (csz-1)) << CTSHIFT_BITPOS);
#endif
	  ct->size = ((bofs & ~(csz-1)) >> 3);  
	}
      }

      
      if ((sinfo & CTF_UNION)) {
	if (bsz > bmaxofs) bmaxofs = bsz;
      } else {
	bofs += bsz;
      }
    }  

    fieldid = ct->sib;
  }

  
  sct->info = sinfo + CTALIGN(maxalign);
  bofs = (sinfo & CTF_UNION) ? bmaxofs : bofs;
  maxalign = (8u << maxalign) - 1;
  sct->size = (((bofs + maxalign) & ~maxalign) >> 3);
}


static CTypeID cp_decl_struct(CPState *cp, CPDecl *sdecl, CTInfo sinfo)
{
  CTypeID sid = cp_struct_name(cp, sdecl, sinfo);
  if (cp_opt(cp, '{')) {  
    CTypeID lastid = sid;
    int lastdecl = 0;
    while (cp->tok != '}') {
      CPDecl decl;
      CPscl scl = cp_decl_spec(cp, &decl, CDF_STATIC);
      decl.mode = scl ? CPARSE_MODE_DIRECT :
	CPARSE_MODE_DIRECT|CPARSE_MODE_ABSTRACT|CPARSE_MODE_FIELD;

      for (;;) {
	CTypeID ctypeid;

	if (lastdecl) cp_err_token(cp, '}');

	
	decl.bits = CTSIZE_INVALID;
	cp_declarator(cp, &decl);
	ctypeid = cp_decl_intern(cp, &decl);

	if ((scl & CDF_STATIC)) {  
	  CType *ct;
	  CTypeID fieldid = cp_decl_constinit(cp, &ct, ctypeid);
	  ctype_get(cp->cts, lastid)->sib = fieldid;
	  lastid = fieldid;
	  ctype_setname(ct, decl.name);
	} else {
	  CTSize bsz = CTBSZ_FIELD;  
	  CType *ct;
	  CTypeID fieldid = lj_ctype_new(cp->cts, &ct);  
	  CType *tct = ctype_raw(cp->cts, ctypeid);

	  if (decl.bits == CTSIZE_INVALID) {  
	    if (ctype_isarray(tct->info) && tct->size == CTSIZE_INVALID)
	      lastdecl = 1;  

	    
	    if (!decl.name) {
	      if (!((ctype_isstruct(tct->info) && !(tct->info & CTF_VLA)) ||
		    ctype_isenum(tct->info)))
		cp_err_token(cp, CTOK_IDENT);
	      ct->info = CTINFO(CT_ATTRIB, CTATTRIB(CTA_SUBTYPE) + ctypeid);
	      ct->size = ctype_isstruct(tct->info) ?
			 (decl.attr|0x80000000u) : 0;  
	      goto add_field;
	    }
	  } else {  
	    bsz = decl.bits;
	    if (!ctype_isinteger_or_bool(tct->info) ||
		(bsz == 0 && decl.name) || 8*tct->size > CTBSZ_MAX ||
		bsz > ((tct->info & CTF_BOOL) ? 1 : 8*tct->size))
	      cp_errmsg(cp, ':', LJ_ERR_BADVAL);
	  }

	  
	  ct->info = CTINFO(CT_FIELD, ctypeid + (bsz << CTSHIFT_BITCSZ));
	  ct->size = decl.attr;
	  if (decl.name) ctype_setname(ct, decl.name);

	add_field:
	  ctype_get(cp->cts, lastid)->sib = fieldid;
	  lastid = fieldid;
	}
	if (!cp_opt(cp, ',')) break;
	cp_decl_reset(&decl);
      }
      cp_check(cp, ';');
    }
    cp_check(cp, '}');
    ctype_get(cp->cts, lastid)->sib = 0;  
    cp_decl_attributes(cp, sdecl);  
    cp_struct_layout(cp, sid, sdecl->attr);
  }
  return sid;
}


static CTypeID cp_decl_enum(CPState *cp, CPDecl *sdecl)
{
  CTypeID eid = cp_struct_name(cp, sdecl, CTINFO(CT_ENUM, CTID_VOID));
  CTInfo einfo = CTINFO(CT_ENUM, CTALIGN(2) + CTID_UINT32);
  CTSize esize = 4;  
  if (cp_opt(cp, '{')) {  
    CPValue k;
    CTypeID lastid = eid;
    k.u32 = 0;
    k.id = CTID_INT32;
    do {
      GCstr *name = cp->str;
      if (cp->tok != CTOK_IDENT) cp_err_token(cp, CTOK_IDENT);
      if (cp->val.id) cp_errmsg(cp, 0, LJ_ERR_FFI_REDEF, strdata(name));
      cp_next(cp);
      if (cp_opt(cp, '=')) {
	cp_expr_kint(cp, &k);
	if (k.id == CTID_UINT32) {
	  
	  if (k.i32 >= 0) k.id = CTID_INT32;
	} else {
	  
	  k.id = CTID_INT32;
	  if (k.i32 < 0) einfo = CTINFO(CT_ENUM, CTALIGN(2) + CTID_INT32);
	}
      }
      
      {
	CType *ct;
	CTypeID constid = lj_ctype_new(cp->cts, &ct);
	ctype_get(cp->cts, lastid)->sib = constid;
	lastid = constid;
	ctype_setname(ct, name);
	ct->info = CTINFO(CT_CONSTVAL, CTF_CONST|k.id);
	ct->size = k.u32++;
	if (k.u32 == 0x80000000u) k.id = CTID_UINT32;
	lj_ctype_addname(cp->cts, ct, constid);
      }
      if (!cp_opt(cp, ',')) break;
    } while (cp->tok != '}');  
    cp_check(cp, '}');
    
    ctype_get(cp->cts, eid)->info = einfo;
    ctype_get(cp->cts, eid)->size = esize;
  }
  return eid;
}


static CPscl cp_decl_spec(CPState *cp, CPDecl *decl, CPscl scl)
{
  uint32_t cds = 0, sz = 0;
  CTypeID tdef = 0;

  decl->cp = cp;
  decl->mode = cp->mode;
  decl->name = NULL;
  decl->redir = NULL;
  decl->attr = 0;
  decl->fattr = 0;
  decl->pos = decl->top = 0;
  decl->stack[0].next = 0;

  for (;;) {  
    cp_decl_attributes(cp, decl);
    if (cp->tok >= CTOK_FIRSTDECL && cp->tok <= CTOK_LASTDECLFLAG) {
      uint32_t cbit;
      if (cp->ct->size) {
	if (sz) goto end_decl;
	sz = cp->ct->size;
      }
      cbit = (1u << (cp->tok - CTOK_FIRSTDECL));
      cds = cds | cbit | ((cbit & cds & CDF_LONG) << 1);
      if (cp->tok >= CTOK_FIRSTSCL) {
	if (!(scl & cbit)) cp_errmsg(cp, cp->tok, LJ_ERR_FFI_BADSCL);
      } else if (tdef) {
	goto end_decl;
      }
      cp_next(cp);
      continue;
    }
    if (sz || tdef ||
	(cds & (CDF_SHORT|CDF_LONG|CDF_SIGNED|CDF_UNSIGNED|CDF_COMPLEX)))
      break;
    switch (cp->tok) {
    case CTOK_STRUCT:
      tdef = cp_decl_struct(cp, decl, CTINFO(CT_STRUCT, 0));
      continue;
    case CTOK_UNION:
      tdef = cp_decl_struct(cp, decl, CTINFO(CT_STRUCT, CTF_UNION));
      continue;
    case CTOK_ENUM:
      tdef = cp_decl_enum(cp, decl);
      continue;
    case CTOK_IDENT:
      if (ctype_istypedef(cp->ct->info)) {
	tdef = ctype_cid(cp->ct->info);  
	cp_next(cp);
	continue;
      }
      break;
    case '$':
      tdef = cp->val.id;
      cp_next(cp);
      continue;
    default:
      break;
    }
    break;
  }
end_decl:

  if ((cds & CDF_COMPLEX))  
    tdef = sz == 4 ? CTID_COMPLEX_FLOAT : CTID_COMPLEX_DOUBLE;

  if (tdef) {
    cp_push_type(decl, tdef);
  } else if ((cds & CDF_VOID)) {
    cp_push(decl, CTINFO(CT_VOID, (decl->attr & CTF_QUAL)), CTSIZE_INVALID);
    decl->attr &= ~CTF_QUAL;
  } else {
    
    CTInfo info = CTINFO(CT_NUM, (cds & CDF_UNSIGNED) ? CTF_UNSIGNED : 0);
    if ((cds & CDF_BOOL)) {
      if ((cds & ~(CDF_SCL|CDF_BOOL|CDF_INT|CDF_SIGNED|CDF_UNSIGNED)))
	cp_errmsg(cp, 0, LJ_ERR_FFI_INVTYPE);
      info |= CTF_BOOL;
      if (!(cds & CDF_SIGNED)) info |= CTF_UNSIGNED;
      if (!sz) {
	sz = 1;
      }
    } else if ((cds & CDF_FP)) {
      info = CTINFO(CT_NUM, CTF_FP);
      if ((cds & CDF_LONG)) sz = sizeof(long double);
    } else if ((cds & CDF_CHAR)) {
      if ((cds & (CDF_CHAR|CDF_SIGNED|CDF_UNSIGNED)) == CDF_CHAR)
	info |= CTF_UCHAR;  
    } else if ((cds & CDF_SHORT)) {
      sz = sizeof(short);
    } else if ((cds & CDF_LONGLONG)) {
      sz = 8;
    } else if ((cds & CDF_LONG)) {
      info |= CTF_LONG;
      sz = sizeof(long);
    } else if (!sz) {
      if (!(cds & (CDF_SIGNED|CDF_UNSIGNED)))
	cp_errmsg(cp, cp->tok, LJ_ERR_FFI_DECLSPEC);
      sz = sizeof(int);
    }
    lj_assertCP(sz != 0, "basic ctype with zero size");
    info += CTALIGN(lj_fls(sz));  
    info += (decl->attr & CTF_QUAL);  
    cp_push(decl, info, sz);
    decl->attr &= ~CTF_QUAL;
  }
  decl->specpos = decl->pos;
  decl->specattr = decl->attr;
  decl->specfattr = decl->fattr;
  return (cds & CDF_SCL);  
}


static void cp_decl_array(CPState *cp, CPDecl *decl)
{
  CTInfo info = CTINFO(CT_ARRAY, 0);
  CTSize nelem = CTSIZE_INVALID;  
  cp_decl_attributes(cp, decl);
  if (cp_opt(cp, '?'))
    info |= CTF_VLA;  
  else if (cp->tok != ']')
    nelem = cp_expr_ksize(cp);
  cp_check(cp, ']');
  cp_add(decl, info, nelem);
}


static void cp_decl_func(CPState *cp, CPDecl *fdecl)
{
  CTSize nargs = 0;
  CTInfo info = CTINFO(CT_FUNC, 0);
  CTypeID lastid = 0, anchor = 0;
  if (cp->tok != ')') {
    do {
      CPDecl decl;
      CTypeID ctypeid, fieldid;
      CType *ct;
      if (cp_opt(cp, '.')) {  
	cp_check(cp, '.');  
	cp_check(cp, '.');
	info |= CTF_VARARG;
	break;
      }
      cp_decl_spec(cp, &decl, CDF_REGISTER);
      decl.mode = CPARSE_MODE_DIRECT|CPARSE_MODE_ABSTRACT;
      cp_declarator(cp, &decl);
      ctypeid = cp_decl_intern(cp, &decl);
      ct = ctype_raw(cp->cts, ctypeid);
      if (ctype_isvoid(ct->info))
	break;
      else if (ctype_isrefarray(ct->info))
	ctypeid = lj_ctype_intern(cp->cts,
	  CTINFO(CT_PTR, CTALIGN_PTR|ctype_cid(ct->info)), CTSIZE_PTR);
      else if (ctype_isfunc(ct->info))
	ctypeid = lj_ctype_intern(cp->cts,
	  CTINFO(CT_PTR, CTALIGN_PTR|ctypeid), CTSIZE_PTR);
      
      fieldid = lj_ctype_new(cp->cts, &ct);
      if (anchor)
	ctype_get(cp->cts, lastid)->sib = fieldid;
      else
	anchor = fieldid;
      lastid = fieldid;
      if (decl.name) ctype_setname(ct, decl.name);
      ct->info = CTINFO(CT_FIELD, ctypeid);
      ct->size = nargs++;
    } while (cp_opt(cp, ','));
  }
  cp_check(cp, ')');
  if (cp_opt(cp, '{')) {  
    int level = 1;
    cp->mode |= CPARSE_MODE_SKIP;
    for (;;) {
      if (cp->tok == '{') level++;
      else if (cp->tok == '}' && --level == 0) break;
      else if (cp->tok == CTOK_EOF) cp_err_token(cp, '}');
      cp_next(cp);
    }
    cp->mode &= ~CPARSE_MODE_SKIP;
    cp->tok = ';';  
  }
  info |= (fdecl->fattr & ~CTMASK_CID);
  fdecl->fattr = 0;
  fdecl->stack[cp_add(fdecl, info, nargs)].sib = anchor;
}


static void cp_declarator(CPState *cp, CPDecl *decl)
{
  if (++cp->depth > CPARSE_MAX_DECLDEPTH) cp_err(cp, LJ_ERR_XLEVELS);

  for (;;) {  
    if (cp_opt(cp, '*')) {  
      CTSize sz;
      CTInfo info;
      cp_decl_attributes(cp, decl);
      sz = CTSIZE_PTR;
      info = CTINFO(CT_PTR, CTALIGN_PTR);
#if LJ_64
      if (ctype_msizeP(decl->attr) == 4) {
	sz = 4;
	info = CTINFO(CT_PTR, CTALIGN(2));
      }
#endif
      info += (decl->attr & (CTF_QUAL|CTF_REF));
      decl->attr &= ~(CTF_QUAL|(CTMASK_MSIZEP<<CTSHIFT_MSIZEP));
      cp_push(decl, info, sz);
    } else if (cp_opt(cp, '&') || cp_opt(cp, CTOK_ANDAND)) {  
      decl->attr &= ~(CTF_QUAL|(CTMASK_MSIZEP<<CTSHIFT_MSIZEP));
      cp_push(decl, CTINFO_REF(0), CTSIZE_PTR);
    } else {
      break;
    }
  }

  if (cp_opt(cp, '(')) {  
    CPDeclIdx pos;
    cp_decl_attributes(cp, decl);
    
    if ((decl->mode & CPARSE_MODE_ABSTRACT) &&
	(cp->tok == ')' || cp_istypedecl(cp))) goto func_decl;
    pos = decl->pos;
    cp_declarator(cp, decl);
    cp_check(cp, ')');
    decl->pos = pos;
  } else if (cp->tok == CTOK_IDENT) {  
    if (!(decl->mode & CPARSE_MODE_DIRECT)) cp_err_token(cp, CTOK_EOF);
    decl->name = cp->str;
    decl->nameid = cp->val.id;
    cp_next(cp);
  } else {  
    if (!(decl->mode & CPARSE_MODE_ABSTRACT)) cp_err_token(cp, CTOK_IDENT);
  }

  for (;;) {  
    if (cp_opt(cp, '[')) {  
      cp_decl_array(cp, decl);
    } else if (cp_opt(cp, '(')) {  
    func_decl:
      cp_decl_func(cp, decl);
    } else {
      break;
    }
  }

  if ((decl->mode & CPARSE_MODE_FIELD) && cp_opt(cp, ':'))  
    decl->bits = cp_expr_ksize(cp);

  
  cp_decl_attributes(cp, decl);
  cp_push_attributes(decl);

  cp->depth--;
}


static CTypeID cp_decl_abstract(CPState *cp)
{
  CPDecl decl;
  cp_decl_spec(cp, &decl, 0);
  decl.mode = CPARSE_MODE_ABSTRACT;
  cp_declarator(cp, &decl);
  return cp_decl_intern(cp, &decl);
}


static void cp_pragma(CPState *cp, BCLine pragmaline)
{
  cp_next(cp);
  if (cp->tok == CTOK_IDENT && cp_str_is(cp->str, "pack"))  {
    cp_next(cp);
    cp_check(cp, '(');
    if (cp->tok == CTOK_IDENT) {
      if (cp_str_is(cp->str, "push")) {
	if (cp->curpack < CPARSE_MAX_PACKSTACK-1) {
	  cp->packstack[cp->curpack+1] = cp->packstack[cp->curpack];
	  cp->curpack++;
	} else {
	  cp_errmsg(cp, cp->tok, LJ_ERR_XLEVELS);
	}
      } else if (cp_str_is(cp->str, "pop")) {
	if (cp->curpack > 0) cp->curpack--;
      } else {
	cp_errmsg(cp, cp->tok, LJ_ERR_XSYMBOL);
      }
      cp_next(cp);
      if (!cp_opt(cp, ',')) goto end_pack;
    }
    if (cp->tok == CTOK_INTEGER) {
      cp->packstack[cp->curpack] = cp->val.u32 ? lj_fls(cp->val.u32) : 0;
      cp_next(cp);
    } else {
      cp->packstack[cp->curpack] = 255;
    }
  end_pack:
    cp_check(cp, ')');
  } else {  
    while (cp->tok != CTOK_EOF && cp->linenumber == pragmaline)
      cp_next(cp);
  }
}


static void cp_line(CPState *cp, BCLine hashline)
{
  BCLine newline = cp->val.u32;
  
  while (cp->tok != CTOK_EOF && cp->linenumber == hashline)
    cp_next(cp);
  cp->linenumber = newline;
}


static void cp_decl_multi(CPState *cp)
{
  int first = 1;
  while (cp->tok != CTOK_EOF) {
    CPDecl decl;
    CPscl scl;
    if (cp_opt(cp, ';')) {  
      first = 0;
      continue;
    }
    if (cp->tok == '#') {  
      BCLine hashline = cp->linenumber;
      CPToken tok = cp_next(cp);
      if (tok == CTOK_INTEGER) {
	cp_line(cp, hashline);
	continue;
      } else if (tok == CTOK_IDENT && cp_str_is(cp->str, "line")) {
	if (cp_next(cp) != CTOK_INTEGER) cp_err_token(cp, tok);
	cp_line(cp, hashline);
	continue;
      } else if (tok == CTOK_IDENT && cp_str_is(cp->str, "pragma")) {
	cp_pragma(cp, hashline);
	continue;
      } else {
	cp_errmsg(cp, cp->tok, LJ_ERR_XSYMBOL);
      }
    }
    scl = cp_decl_spec(cp, &decl, CDF_TYPEDEF|CDF_EXTERN|CDF_STATIC);
    if ((cp->tok == ';' || cp->tok == CTOK_EOF) &&
	ctype_istypedef(decl.stack[0].info)) {
      CTInfo info = ctype_rawchild(cp->cts, &decl.stack[0])->info;
      if (ctype_isstruct(info) || ctype_isenum(info))
	goto decl_end;  
    }
    for (;;) {
      CTypeID ctypeid;
      cp_declarator(cp, &decl);
      ctypeid = cp_decl_intern(cp, &decl);
      if (decl.name && !decl.nameid) {  
	CType *ct;
	CTypeID id;
	if ((scl & CDF_TYPEDEF)) {  
	  id = lj_ctype_new(cp->cts, &ct);
	  ct->info = CTINFO(CT_TYPEDEF, ctypeid);
	  goto noredir;
	} else if (ctype_isfunc(ctype_get(cp->cts, ctypeid)->info)) {
	  
	  ct = ctype_get(cp->cts, ctypeid);
	  
	  lj_assertCP(gcref(ct->name) == NULL, "unexpected named function");
	  id = ctypeid;  
	} else if ((scl & CDF_STATIC)) {  
	  id = cp_decl_constinit(cp, &ct, ctypeid);
	  goto noredir;
	} else {  
	  id = lj_ctype_new(cp->cts, &ct);
	  ct->info = CTINFO(CT_EXTERN, ctypeid);
	}
	if (decl.redir) {  
	  CType *cta;
	  CTypeID aid = lj_ctype_new(cp->cts, &cta);
	  ct = ctype_get(cp->cts, id);  
	  cta->info = CTINFO(CT_ATTRIB, CTATTRIB(CTA_REDIR));
	  cta->sib = ct->sib;
	  ct->sib = aid;
	  ctype_setname(cta, decl.redir);
	}
      noredir:
	ctype_setname(ct, decl.name);
	lj_ctype_addname(cp->cts, ct, id);
      }
      if (!cp_opt(cp, ',')) break;
      cp_decl_reset(&decl);
    }
  decl_end:
    if (cp->tok == CTOK_EOF && first) break;  
    first = 0;
    cp_check(cp, ';');
  }
}


static void cp_decl_single(CPState *cp)
{
  CPDecl decl;
  cp_decl_spec(cp, &decl, 0);
  cp_declarator(cp, &decl);
  cp->val.id = cp_decl_intern(cp, &decl);
  if (cp->tok != CTOK_EOF) cp_err_token(cp, CTOK_EOF);
}




static TValue *cpcparser(lua_State *L, lua_CFunction dummy, void *ud)
{
  CPState *cp = (CPState *)ud;
  UNUSED(dummy);
  cframe_errfunc(L->cframe) = -1;  
  cp_init(cp);
  if ((cp->mode & CPARSE_MODE_MULTI))
    cp_decl_multi(cp);
  else
    cp_decl_single(cp);
  if (cp->param && cp->param != cp->L->top)
    cp_err(cp, LJ_ERR_FFI_NUMPARAM);
  lj_assertCP(cp->depth == 0, "unbalanced cparser declaration depth");
  return NULL;
}


int lj_cparse(CPState *cp)
{
  LJ_CTYPE_SAVE(cp->cts);
  int errcode = lj_vm_cpcall(cp->L, NULL, cp, cpcparser);
  if (errcode)
    LJ_CTYPE_RESTORE(cp->cts);
  cp_cleanup(cp);
  return errcode;
}

#endif
