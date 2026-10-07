

#define lj_prng_c
#define LUA_CORE


#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "lj_def.h"
#include "lj_arch.h"
#include "lj_prng.h"






#define TW223_GEN(rs, z, r, i, k, q, s) \
  z = rs->u[i]; \
  z = (((z<<q)^z) >> (k-s)) ^ ((z&((uint64_t)(int64_t)-1 << (64-k)))<<s); \
  r ^= z; rs->u[i] = z;

#define TW223_STEP(rs, z, r) \
  TW223_GEN(rs, z, r, 0, 63, 31, 18) \
  TW223_GEN(rs, z, r, 1, 58, 19, 28) \
  TW223_GEN(rs, z, r, 2, 55, 24,  7) \
  TW223_GEN(rs, z, r, 3, 47, 21,  8)


LJ_NOINLINE uint64_t LJ_FASTCALL lj_prng_u64(PRNGState *rs)
{
  uint64_t z, r = 0;
  TW223_STEP(rs, z, r)
  return r;
}


LJ_NOINLINE uint64_t LJ_FASTCALL lj_prng_u64d(PRNGState *rs)
{
  uint64_t z, r = 0;
  TW223_STEP(rs, z, r)
  
  return (r & U64x(000fffff,ffffffff)) | U64x(3ff00000,00000000);
}


static LJ_AINLINE void lj_prng_condition(PRNGState *rs)
{
  if (rs->u[0] < (1u << 1)) rs->u[0] += (1u << 1);
  if (rs->u[1] < (1u << 6)) rs->u[1] += (1u << 6);
  if (rs->u[2] < (1u << 9)) rs->u[2] += (1u << 9);
  if (rs->u[3] < (1u << 17)) rs->u[3] += (1u << 17);
}



#if LUAJIT_SECURITY_PRNG == 0



#elif LJ_TARGET_XBOX360

extern int XNetRandom(void *buf, unsigned int len);

#elif LJ_TARGET_PS3

extern int sys_get_random_number(void *buf, uint64_t len);

#elif LJ_TARGET_PS4 || LJ_TARGET_PS5 || LJ_TARGET_PSVITA

extern int sceRandomGetRandomNumber(void *buf, size_t len);

#elif LJ_TARGET_NX

#include <unistd.h>

#elif LJ_TARGET_WINDOWS || LJ_TARGET_XBOXONE

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#if LJ_TARGET_UWP || LJ_TARGET_XBOXONE

#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")
#else

typedef BOOLEAN (WINAPI *PRGR)(void *buf, ULONG len);
static PRGR libfunc_rgr;
#endif

#elif LJ_TARGET_POSIX

#if LJ_TARGET_LINUX

#include <sys/syscall.h>
#else

#if LJ_TARGET_OSX && !LJ_TARGET_IOS

#include <Availability.h>
#if __MAC_OS_X_VERSION_MIN_REQUIRED >= 101200
#define LJ_TARGET_HAS_GETENTROPY	1
#endif
#elif (LJ_TARGET_BSD && !defined(__NetBSD__)) || LJ_TARGET_SOLARIS || LJ_TARGET_CYGWIN || LJ_TARGET_QNX || LJ_TARGET_HURD
#define LJ_TARGET_HAS_GETENTROPY	1
#endif

#if LJ_TARGET_HAS_GETENTROPY
extern int getentropy(void *buf, size_t len)
#ifdef __ELF__
  __attribute__((weak))
#endif
;
#endif

#endif


#include <fcntl.h>
#include <unistd.h>

#endif

#if LUAJIT_SECURITY_PRNG == 0


int LJ_FASTCALL lj_prng_seed_secure(PRNGState *rs)
{
  lj_prng_seed_fixed(rs);  
  return 1;
}

#else


int LJ_FASTCALL lj_prng_seed_secure(PRNGState *rs)
{
#if LJ_TARGET_XBOX360

  if (XNetRandom(rs->u, (unsigned int)sizeof(rs->u)) == 0)
    goto ok;

#elif LJ_TARGET_PS3

  if (sys_get_random_number(rs->u, sizeof(rs->u)) == 0)
    goto ok;

#elif LJ_TARGET_PS4 || LJ_TARGET_PS5 || LJ_TARGET_PSVITA

  if (sceRandomGetRandomNumber(rs->u, sizeof(rs->u)) == 0)
    goto ok;

#elif LJ_TARGET_NX

  if (getentropy(rs->u, sizeof(rs->u)) == 0)
    goto ok;

#elif LJ_TARGET_UWP || LJ_TARGET_XBOXONE

  if (BCryptGenRandom(NULL, (PUCHAR)(rs->u), (ULONG)sizeof(rs->u),
		      BCRYPT_USE_SYSTEM_PREFERRED_RNG) >= 0)
    goto ok;

#elif LJ_TARGET_WINDOWS

  
  if (!libfunc_rgr) {
    HMODULE lib = LJ_WIN_LOADLIBA("advapi32.dll");
    if (!lib) return 0;
    libfunc_rgr = (PRGR)GetProcAddress(lib, "SystemFunction036");
    if (!libfunc_rgr) return 0;
  }
  if (libfunc_rgr(rs->u, (ULONG)sizeof(rs->u)))
    goto ok;

#elif LJ_TARGET_POSIX

#if LJ_TARGET_LINUX && defined(SYS_getrandom)

  if (syscall(SYS_getrandom, rs->u, sizeof(rs->u), 0) == (long)sizeof(rs->u))
    goto ok;

#elif LJ_TARGET_HAS_GETENTROPY

#ifdef __ELF__
  if (&getentropy && getentropy(rs->u, sizeof(rs->u)) == 0)
    goto ok;
#else
  if (getentropy(rs->u, sizeof(rs->u)) == 0)
    goto ok;
#endif

#endif

  
  {
    int fd = open("/dev/urandom", O_RDONLY|O_CLOEXEC);
    if (fd != -1) {
      ssize_t n = read(fd, rs->u, sizeof(rs->u));
      (void)close(fd);
      if (n == (ssize_t)sizeof(rs->u))
	goto ok;
    }
  }

#else

  
#error "Missing secure PRNG seed for this OS"

#endif
  return 0;  

ok:
  lj_prng_condition(rs);
  (void)lj_prng_u64(rs);
  return 1;  
}

#endif

