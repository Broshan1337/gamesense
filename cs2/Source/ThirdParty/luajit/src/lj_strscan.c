

#include <math.h>

#define lj_strscan_c
#define LUA_CORE

#include "lj_obj.h"
#include "lj_char.h"
#include "lj_strscan.h"






#define STRSCAN_DIG	1024
#define STRSCAN_MAXDIG	800		
#define STRSCAN_DDIG	(STRSCAN_DIG/2)
#define STRSCAN_DMASK	(STRSCAN_DDIG-1)
#define STRSCAN_MAXEXP	(1 << 20)


#define DNEXT(a)	(((a)+1) & STRSCAN_DMASK)
#define DPREV(a)	(((a)-1) & STRSCAN_DMASK)
#define DLEN(lo, hi)	((int32_t)(((lo)-(hi)) & STRSCAN_DMASK))

#define casecmp(c, k)	(((c) | 0x20) == k)


static void strscan_double(uint64_t x, TValue *o, int32_t ex2, int32_t neg)
{
  double n;

  
  if (LJ_UNLIKELY(ex2 <= -1075 && x != 0)) {
    
#if (defined(__GNUC__) || defined(__clang__)) && LJ_64
    int32_t b = (int32_t)(__builtin_clzll(x)^63);
#else
    int32_t b = (x>>32) ? 32+(int32_t)lj_fls((uint32_t)(x>>32)) :
			  (int32_t)lj_fls((uint32_t)x);
#endif
    if ((int32_t)b + ex2 <= -1023 && (int32_t)b + ex2 >= -1075) {
      uint64_t rb = (uint64_t)1 << (-1075-ex2);
      if ((x & rb) && ((x & (rb+rb+rb-1)))) x += rb+rb;
      x = (x & ~(rb+rb-1));
    }
  }

  
  lj_assertX((int64_t)x >= 0, "bad double conversion");
  n = (double)(int64_t)x;
  if (neg) n = -n;
  if (ex2) n = ldexp(n, ex2);
  o->n = n;
}


static StrScanFmt strscan_hex(const uint8_t *p, TValue *o,
			      StrScanFmt fmt, uint32_t opt,
			      int32_t ex2, int32_t neg, uint32_t dig)
{
  uint64_t x = 0;
  uint32_t i;

  
  for (i = dig > 16 ? 16 : dig ; i; i--, p++) {
    uint32_t d = (*p != '.' ? *p : *++p); if (d > '9') d += 9;
    x = (x << 4) + (d & 15);
  }

  
  for (i = 16; i < dig; i++, p++)
    x |= ((*p != '.' ? *p : *++p) != '0'), ex2 += 4;

  
  switch (fmt) {
  case STRSCAN_INT:
    if (!(opt & STRSCAN_OPT_TONUM) && x < 0x80000000u+neg &&
	!(x == 0 && neg)) {
      o->i = neg ? (int32_t)(~x+1u) : (int32_t)x;
      return STRSCAN_INT;  
    }
    if (!(opt & STRSCAN_OPT_C)) { fmt = STRSCAN_NUM; break; }
    
  case STRSCAN_U32:
    if (dig > 8) return STRSCAN_ERROR;
    o->i = neg ? (int32_t)(~x+1u) : (int32_t)x;
    return STRSCAN_U32;
  case STRSCAN_I64:
  case STRSCAN_U64:
    if (dig > 16) return STRSCAN_ERROR;
    o->u64 = neg ? ~x+1u : x;
    return fmt;
  default:
    break;
  }

  
  if ((x & U64x(c0000000,0000000))) { x = (x >> 2) | (x & 3); ex2 += 2; }
  strscan_double(x, o, ex2, neg);
  return fmt;
}


static StrScanFmt strscan_oct(const uint8_t *p, TValue *o,
			      StrScanFmt fmt, int32_t neg, uint32_t dig)
{
  uint64_t x = 0;

  
  if (dig > 22 || (dig == 22 && *p > '1')) return STRSCAN_ERROR;
  while (dig-- > 0) {
    if (!(*p >= '0' && *p <= '7')) return STRSCAN_ERROR;
    x = (x << 3) + (*p++ & 7);
  }

  
  switch (fmt) {
  case STRSCAN_INT:
    if (x >= 0x80000000u+neg) fmt = STRSCAN_U32;
    
  case STRSCAN_U32:
    if ((x >> 32)) return STRSCAN_ERROR;
    o->i = neg ? (int32_t)(~(uint32_t)x+1u) : (int32_t)x;
    break;
  default:
  case STRSCAN_I64:
  case STRSCAN_U64:
    o->u64 = neg ? ~x+1u : x;
    break;
  }
  return fmt;
}


static StrScanFmt strscan_dec(const uint8_t *p, TValue *o,
			      StrScanFmt fmt, uint32_t opt,
			      int32_t ex10, int32_t neg, uint32_t dig)
{
  uint8_t xi[STRSCAN_DDIG], *xip = xi;

  if (dig) {
    uint32_t i = dig;
    if (i > STRSCAN_MAXDIG) {
      ex10 += (int32_t)(i - STRSCAN_MAXDIG);
      i = STRSCAN_MAXDIG;
    }
    
    if (((ex10^i) & 1))
      *xip++ = ((*p != '.' ? *p : *++p) & 15), i--, p++;
    
    for ( ; i > 1; i -= 2) {
      uint32_t d = 10 * ((*p != '.' ? *p : *++p) & 15); p++;
      *xip++ = d + ((*p != '.' ? *p : *++p) & 15); p++;
    }
    
    if (i) *xip++ = 10 * ((*p != '.' ? *p : *++p) & 15), ex10--, dig++, p++;

    
    if (dig > STRSCAN_MAXDIG) {
      do {
	if ((*p != '.' ? *p : *++p) != '0') { xip[-1] |= 1; break; }
	p++;
      } while (--dig > STRSCAN_MAXDIG);
      dig = STRSCAN_MAXDIG;
    } else {  
      while (ex10 > 0 && dig <= 18) *xip++ = 0, ex10 -= 2, dig += 2;
    }
  } else {  
    ex10 = 0;
    xi[0] = 0;
  }

  
  if (dig <= 20 && ex10 == 0) {
    uint8_t *xis;
    uint64_t x = xi[0];
    double n;
    for (xis = xi+1; xis < xip; xis++) x = x * 100 + *xis;
    if (!(dig == 20 && (xi[0] > 18 || (int64_t)x >= 0))) {  
      
      switch (fmt) {
      case STRSCAN_INT:
	if (!(opt & STRSCAN_OPT_TONUM) && x < 0x80000000u+neg) {
	  o->i = neg ? (int32_t)(~x+1u) : (int32_t)x;
	  return STRSCAN_INT;  
	}
	if (!(opt & STRSCAN_OPT_C)) { fmt = STRSCAN_NUM; goto plainnumber; }
	
      case STRSCAN_U32:
	if ((x >> 32) != 0) return STRSCAN_ERROR;
	o->i = neg ? (int32_t)(~x+1u) : (int32_t)x;
	return STRSCAN_U32;
      case STRSCAN_I64:
      case STRSCAN_U64:
	o->u64 = neg ? ~x+1u : x;
	return fmt;
      default:
      plainnumber:  
	if ((int64_t)x < 0) break;
	n = (double)(int64_t)x;
	if (neg) n = -n;
	o->n = n;
	return fmt;
      }
    }
  }

  
  if (fmt == STRSCAN_INT) {
    if ((opt & STRSCAN_OPT_C)) return STRSCAN_ERROR;
    fmt = STRSCAN_NUM;
  } else if (fmt > STRSCAN_INT) {
    return STRSCAN_ERROR;
  }
  {
    uint32_t hi = 0, lo = (uint32_t)(xip-xi);
    int32_t ex2 = 0, idig = (int32_t)lo + (ex10 >> 1);

    lj_assertX(lo > 0 && (ex10 & 1) == 0, "bad lo %d ex10 %d", lo, ex10);

    
    if (idig > 310/2) { if (neg) setminfV(o); else setpinfV(o); return fmt; }
    else if (idig < -326/2) { o->n = neg ? -0.0 : 0.0; return fmt; }

    
    while (idig < 9 && idig < DLEN(lo, hi)) {
      uint32_t i, cy = 0;
      ex2 -= 6;
      for (i = DPREV(lo); ; i = DPREV(i)) {
	uint32_t d = (xi[i] << 6) + cy;
	cy = (((d >> 2) * 5243) >> 17); d = d - cy * 100;  
	xi[i] = (uint8_t)d;
	if (i == hi) break;
	if (d == 0 && i == DPREV(lo)) lo = i;
      }
      if (cy) {
	hi = DPREV(hi);
	if (xi[DPREV(lo)] == 0) lo = DPREV(lo);
	else if (hi == lo) { lo = DPREV(lo); xi[DPREV(lo)] |= xi[lo]; }
	xi[hi] = (uint8_t)cy; idig++;
      }
    }

    
    while (idig > 9) {
      uint32_t i = hi, cy = 0;
      ex2 += 6;
      do {
	cy += xi[i];
	xi[i] = (cy >> 6);
	cy = 100 * (cy & 0x3f);
	if (xi[i] == 0 && i == hi) hi = DNEXT(hi), idig--;
	i = DNEXT(i);
      } while (i != lo);
      while (cy) {
	if (hi == lo) { xi[DPREV(lo)] |= 1; break; }
	xi[lo] = (cy >> 6); lo = DNEXT(lo);
	cy = 100 * (cy & 0x3f);
      }
    }

    
    {
      uint64_t x = xi[hi];
      uint32_t i;
      for (i = DNEXT(hi); --idig > 0 && i != lo; i = DNEXT(i))
	x = x * 100 + xi[i];
      if (i == lo) {
	while (--idig >= 0) x = x * 100;
      } else {  
	x <<= 1; ex2--;
	do {
	  if (xi[i]) { x |= 1; break; }
	  i = DNEXT(i);
	} while (i != lo);
      }
      strscan_double(x, o, ex2, neg);
    }
  }
  return fmt;
}


static StrScanFmt strscan_bin(const uint8_t *p, TValue *o,
			      StrScanFmt fmt, uint32_t opt,
			      int32_t ex2, int32_t neg, uint32_t dig)
{
  uint64_t x = 0;
  uint32_t i;

  if (ex2 || dig > 64) return STRSCAN_ERROR;

  
  for (i = dig; i; i--, p++) {
    if ((*p & ~1) != '0') return STRSCAN_ERROR;
    x = (x << 1) | (*p & 1);
  }

  
  switch (fmt) {
  case STRSCAN_INT:
    if (!(opt & STRSCAN_OPT_TONUM) && x < 0x80000000u+neg) {
      o->i = neg ? (int32_t)(~x+1u) : (int32_t)x;
      return STRSCAN_INT;  
    }
    if (!(opt & STRSCAN_OPT_C)) { fmt = STRSCAN_NUM; break; }
    
  case STRSCAN_U32:
    if (dig > 32) return STRSCAN_ERROR;
    o->i = neg ? (int32_t)(~x+1u) : (int32_t)x;
    return STRSCAN_U32;
  case STRSCAN_I64:
  case STRSCAN_U64:
    o->u64 = neg ? ~x+1u : x;
    return fmt;
  default:
    break;
  }

  
  if ((x & U64x(c0000000,0000000))) { x = (x >> 2) | (x & 3); ex2 += 2; }
  strscan_double(x, o, ex2, neg);
  return fmt;
}


StrScanFmt lj_strscan_scan(const uint8_t *p, MSize len, TValue *o,
			   uint32_t opt)
{
  int32_t neg = 0;
  const uint8_t *pe = p + len;

  
  if (LJ_UNLIKELY(!lj_char_isdigit(*p))) {
    while (lj_char_isspace(*p)) p++;
    if (*p == '+' || *p == '-') neg = (*p++ == '-');
    if (LJ_UNLIKELY(*p >= 'A')) {  
      TValue tmp;
      setnanV(&tmp);
      if (casecmp(p[0],'i') && casecmp(p[1],'n') && casecmp(p[2],'f')) {
	if (neg) setminfV(&tmp); else setpinfV(&tmp);
	p += 3;
	if (casecmp(p[0],'i') && casecmp(p[1],'n') && casecmp(p[2],'i') &&
	    casecmp(p[3],'t') && casecmp(p[4],'y')) p += 5;
      } else if (casecmp(p[0],'n') && casecmp(p[1],'a') && casecmp(p[2],'n')) {
	p += 3;
      }
      while (lj_char_isspace(*p)) p++;
      if (*p || p < pe) return STRSCAN_ERROR;
      o->u64 = tmp.u64;
      return STRSCAN_NUM;
    }
  }

  
  {
    StrScanFmt fmt = STRSCAN_INT;
    int cmask = LJ_CHAR_DIGIT;
    int base = (opt & STRSCAN_OPT_C) && *p == '0' ? 0 : 10;
    const uint8_t *sp, *dp = NULL;
    uint32_t dig = 0, hasdig = 0, x = 0;
    int32_t ex = 0;

    
    if (LJ_UNLIKELY(*p <= '0')) {
      if (*p == '0') {
	if (casecmp(p[1], 'x'))
	  base = 16, cmask = LJ_CHAR_XDIGIT, p += 2;
	else if (casecmp(p[1], 'b'))
	  base = 2, cmask = LJ_CHAR_DIGIT, p += 2;
      }
      for ( ; ; p++) {
	if (*p == '0') {
	  hasdig = 1;
	} else if (*p == '.') {
	  if (dp) return STRSCAN_ERROR;
	  dp = p;
	} else {
	  break;
	}
      }
    }

    
    for (sp = p; ; p++) {
      if (LJ_LIKELY(lj_char_isa(*p, cmask))) {
	x = x * 10 + (*p & 15);  
	dig++;
      } else if (*p == '.') {
	if (dp) return STRSCAN_ERROR;
	dp = p;
      } else {
	break;
      }
    }
    if (!(hasdig | dig)) return STRSCAN_ERROR;

    
    if (dp) {
      if (base == 2) return STRSCAN_ERROR;
      fmt = STRSCAN_NUM;
      if (dig) {
	ex = (int32_t)(dp-(p-1)); dp = p-1;
	while (ex < 0 && *dp-- == '0') ex++, dig--;  
	if (ex <= -STRSCAN_MAXEXP) return STRSCAN_ERROR;
	if (base == 16) ex *= 4;
      }
    }

    
    if (base >= 10 && casecmp(*p, (uint32_t)(base == 16 ? 'p' : 'e'))) {
      uint32_t xx;
      int negx = 0;
      fmt = STRSCAN_NUM; p++;
      if (*p == '+' || *p == '-') negx = (*p++ == '-');
      if (!lj_char_isdigit(*p)) return STRSCAN_ERROR;
      xx = (*p++ & 15);
      while (lj_char_isdigit(*p)) {
	xx = xx * 10 + (*p & 15);
	if (xx >= STRSCAN_MAXEXP) return STRSCAN_ERROR;
	p++;
      }
      ex += negx ? (int32_t)(~xx+1u) : (int32_t)xx;
    }

    
    if (*p) {
      
      
      if (casecmp(*p, 'i')) {
	if (!(opt & STRSCAN_OPT_IMAG)) return STRSCAN_ERROR;
	p++; fmt = STRSCAN_IMAG;
      } else if (fmt == STRSCAN_INT) {
	if (casecmp(*p, 'u')) p++, fmt = STRSCAN_U32;
	if (casecmp(*p, 'l')) {
	  p++;
	  if (casecmp(*p, 'l')) p++, fmt += STRSCAN_I64 - STRSCAN_INT;
	  else if (!(opt & STRSCAN_OPT_C)) return STRSCAN_ERROR;
	  else if (sizeof(long) == 8) fmt += STRSCAN_I64 - STRSCAN_INT;
	}
	if (casecmp(*p, 'u') && (fmt == STRSCAN_INT || fmt == STRSCAN_I64))
	  p++, fmt += STRSCAN_U32 - STRSCAN_INT;
	if ((fmt == STRSCAN_U32 && !(opt & STRSCAN_OPT_C)) ||
	    (fmt >= STRSCAN_I64 && !(opt & STRSCAN_OPT_LL)))
	  return STRSCAN_ERROR;
      }
      while (lj_char_isspace(*p)) p++;
      if (*p) return STRSCAN_ERROR;
    }
    if (p < pe) return STRSCAN_ERROR;

    
    if (fmt == STRSCAN_INT && base == 10 &&
	(dig < 10 || (dig == 10 && *sp <= '2' && x < 0x80000000u+neg))) {
      if ((opt & STRSCAN_OPT_TONUM)) {
	o->n = neg ? -(double)x : (double)x;
	return STRSCAN_NUM;
      } else if (x == 0 && neg) {
	o->n = -0.0;
	return STRSCAN_NUM;
      } else {
	o->i = neg ? (int32_t)(~x+1u) : (int32_t)x;
	return STRSCAN_INT;
      }
    }

    
    if (base == 0 && !(fmt == STRSCAN_NUM || fmt == STRSCAN_IMAG))
      return strscan_oct(sp, o, fmt, neg, dig);
    if (base == 16)
      fmt = strscan_hex(sp, o, fmt, opt, ex, neg, dig);
    else if (base == 2)
      fmt = strscan_bin(sp, o, fmt, opt, ex, neg, dig);
    else
      fmt = strscan_dec(sp, o, fmt, opt, ex, neg, dig);

    
    if (fmt == STRSCAN_NUM && (opt & STRSCAN_OPT_TOINT)) {
      int64_t tmp;
      if (lj_num2int_check(o->n, tmp, o->i) && !tvismzero(o))
	return STRSCAN_INT;
    }
    return fmt;
  }
}

int LJ_FASTCALL lj_strscan_num(GCstr *str, TValue *o)
{
  StrScanFmt fmt = lj_strscan_scan((const uint8_t *)strdata(str), str->len, o,
				   STRSCAN_OPT_TONUM);
  lj_assertX(fmt == STRSCAN_ERROR || fmt == STRSCAN_NUM, "bad scan format");
  return (fmt != STRSCAN_ERROR);
}

#if LJ_DUALNUM
int LJ_FASTCALL lj_strscan_number(GCstr *str, TValue *o)
{
  StrScanFmt fmt = lj_strscan_scan((const uint8_t *)strdata(str), str->len, o,
				   STRSCAN_OPT_TOINT);
  lj_assertX(fmt == STRSCAN_ERROR || fmt == STRSCAN_NUM || fmt == STRSCAN_INT,
	     "bad scan format");
  if (fmt == STRSCAN_INT) setitype(o, LJ_TISNUM);
  return (fmt != STRSCAN_ERROR);
}
#endif

#undef DNEXT
#undef DPREV
#undef DLEN

