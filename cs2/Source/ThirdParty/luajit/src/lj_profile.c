

#define lj_profile_c
#define LUA_CORE

#include "lj_obj.h"

#if LJ_HASPROFILE

#include "lj_buf.h"
#include "lj_frame.h"
#include "lj_debug.h"
#include "lj_dispatch.h"
#if LJ_HASJIT
#include "lj_jit.h"
#include "lj_trace.h"
#endif
#include "lj_profile.h"

#include "luajit.h"

#if LJ_PROFILE_SIGPROF

#include <sys/time.h>
#include <signal.h>
#define profile_lock(ps)	UNUSED(ps)
#define profile_unlock(ps)	UNUSED(ps)

#elif LJ_PROFILE_PTHREAD

#include <pthread.h>
#include <time.h>
#if LJ_TARGET_PS3
#include <sys/timer.h>
#endif
#define profile_lock(ps)	pthread_mutex_lock(&ps->lock)
#define profile_unlock(ps)	pthread_mutex_unlock(&ps->lock)

#elif LJ_PROFILE_WTHREAD

#define WIN32_LEAN_AND_MEAN
#if LJ_TARGET_XBOX360
#include <xtl.h>
#include <xbox.h>
#else
#include <windows.h>
#endif
typedef unsigned int (WINAPI *WMM_TPFUNC)(unsigned int);
#define profile_lock(ps)	EnterCriticalSection(&ps->lock)
#define profile_unlock(ps)	LeaveCriticalSection(&ps->lock)

#endif


typedef struct ProfileState {
  global_State *g;		
  luaJIT_profile_callback cb;	
  void *data;			
  SBuf sb;			
  int interval;			
  int samples;			
  int vmstate;			
#if LJ_PROFILE_SIGPROF
  struct sigaction oldsa;	
#elif LJ_PROFILE_PTHREAD
  pthread_mutex_t lock;		
  pthread_t thread;		
  int abort;			
#elif LJ_PROFILE_WTHREAD
#if LJ_TARGET_WINDOWS
  HINSTANCE wmm;		
  WMM_TPFUNC wmm_tbp;		
  WMM_TPFUNC wmm_tep;		
#endif
  CRITICAL_SECTION lock;	
  HANDLE thread;		
  int abort;			
#endif
} ProfileState;


static ProfileState profile_state;


#define LJ_PROFILE_INTERVAL_DEFAULT	10



#if !LJ_PROFILE_SIGPROF
void LJ_FASTCALL lj_profile_hook_enter(global_State *g)
{
  ProfileState *ps = &profile_state;
  if (ps->g) {
    profile_lock(ps);
    hook_enter(g);
    profile_unlock(ps);
  } else {
    hook_enter(g);
  }
}

void LJ_FASTCALL lj_profile_hook_leave(global_State *g)
{
  ProfileState *ps = &profile_state;
  if (ps->g) {
    profile_lock(ps);
    hook_leave(g);
    profile_unlock(ps);
  } else {
    hook_leave(g);
  }
}

int lj_profile_lock(void)
{
  ProfileState *ps = &profile_state;
  if (ps->g) {
    profile_lock(ps);
    return 1;
  } else {
    return 0;
  }
}

void lj_profile_unlock(void)
{
  ProfileState *ps = &profile_state;
  profile_unlock(ps);
}
#endif




void LJ_FASTCALL lj_profile_interpreter(lua_State *L)
{
  ProfileState *ps = &profile_state;
  global_State *g = G(L);
  uint8_t mask;
  profile_lock(ps);
  mask = (g->hookmask & ~HOOK_PROFILE);
  if (!(mask & HOOK_VMEVENT)) {
    int samples = ps->samples;
    ps->samples = 0;
    g->hookmask = HOOK_VMEVENT;
    lj_dispatch_update(g, 1);
    profile_unlock(ps);
    ps->cb(ps->data, L, samples, ps->vmstate);  
    profile_lock(ps);
    mask |= (g->hookmask & HOOK_PROFILE);
  }
  g->hookmask = mask;
  lj_dispatch_update(g, 1);
  profile_unlock(ps);
}


static void profile_trigger(ProfileState *ps)
{
  global_State *g = ps->g;
  uint8_t mask;
  profile_lock(ps);
  ps->samples++;  
  mask = g->hookmask;
  if (!(mask & (HOOK_PROFILE|HOOK_VMEVENT|HOOK_GC))) {  
    int st = g->vmstate;
    ps->vmstate = st >= 0 ? 'N' :
		  st == ~LJ_VMST_INTERP ? 'I' :
		  st == ~LJ_VMST_C ? 'C' :
		  st == ~LJ_VMST_GC ? 'G' : 'J';
    g->hookmask = (mask | HOOK_PROFILE);
    lj_dispatch_update(g, 1);
  }
  profile_unlock(ps);
}



#if LJ_PROFILE_SIGPROF


static void profile_signal(int sig)
{
  UNUSED(sig);
  profile_trigger(&profile_state);
}


static void profile_timer_start(ProfileState *ps)
{
  int interval = ps->interval;
  struct itimerval tm;
  struct sigaction sa;
  tm.it_value.tv_sec = tm.it_interval.tv_sec = interval / 1000;
  tm.it_value.tv_usec = tm.it_interval.tv_usec = (interval % 1000) * 1000;
  setitimer(ITIMER_PROF, &tm, NULL);
#if LJ_TARGET_QNX
  sa.sa_flags = 0;
#else
  sa.sa_flags = SA_RESTART;
#endif
  sa.sa_handler = profile_signal;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGPROF, &sa, &ps->oldsa);
}


static void profile_timer_stop(ProfileState *ps)
{
  struct itimerval tm;
  tm.it_value.tv_sec = tm.it_interval.tv_sec = 0;
  tm.it_value.tv_usec = tm.it_interval.tv_usec = 0;
  setitimer(ITIMER_PROF, &tm, NULL);
  sigaction(SIGPROF, &ps->oldsa, NULL);
}

#elif LJ_PROFILE_PTHREAD


static void *profile_thread(ProfileState *ps)
{
  int interval = ps->interval;
#if !LJ_TARGET_PS3
  struct timespec ts;
  ts.tv_sec = interval / 1000;
  ts.tv_nsec = (interval % 1000) * 1000000;
#endif
  while (1) {
#if LJ_TARGET_PS3
    sys_timer_usleep(interval * 1000);
#else
    nanosleep(&ts, NULL);
#endif
    if (ps->abort) break;
    profile_trigger(ps);
  }
  return NULL;
}


static void profile_timer_start(ProfileState *ps)
{
  pthread_mutex_init(&ps->lock, 0);
  ps->abort = 0;
  pthread_create(&ps->thread, NULL, (void *(*)(void *))profile_thread, ps);
}


static void profile_timer_stop(ProfileState *ps)
{
  ps->abort = 1;
  pthread_join(ps->thread, NULL);
  pthread_mutex_destroy(&ps->lock);
}

#elif LJ_PROFILE_WTHREAD


static DWORD WINAPI profile_thread(void *psx)
{
  ProfileState *ps = (ProfileState *)psx;
  int interval = ps->interval;
#if LJ_TARGET_WINDOWS && !LJ_TARGET_UWP
  ps->wmm_tbp(interval);
#endif
  while (1) {
    Sleep(interval);
    if (ps->abort) break;
    profile_trigger(ps);
  }
#if LJ_TARGET_WINDOWS && !LJ_TARGET_UWP
  ps->wmm_tep(interval);
#endif
  return 0;
}


static void profile_timer_start(ProfileState *ps)
{
#if LJ_TARGET_WINDOWS && !LJ_TARGET_UWP
  if (!ps->wmm) {  
    ps->wmm = LJ_WIN_LOADLIBA("winmm.dll");
    if (ps->wmm) {
      ps->wmm_tbp = (WMM_TPFUNC)GetProcAddress(ps->wmm, "timeBeginPeriod");
      ps->wmm_tep = (WMM_TPFUNC)GetProcAddress(ps->wmm, "timeEndPeriod");
      if (!ps->wmm_tbp || !ps->wmm_tep) {
	ps->wmm = NULL;
	return;
      }
    }
  }
#endif
  InitializeCriticalSection(&ps->lock);
  ps->abort = 0;
  ps->thread = CreateThread(NULL, 0, profile_thread, ps, 0, NULL);
}


static void profile_timer_stop(ProfileState *ps)
{
  ps->abort = 1;
  WaitForSingleObject(ps->thread, INFINITE);
  DeleteCriticalSection(&ps->lock);
}

#endif




LUA_API void luaJIT_profile_start(lua_State *L, const char *mode,
				  luaJIT_profile_callback cb, void *data)
{
  ProfileState *ps = &profile_state;
  int interval = LJ_PROFILE_INTERVAL_DEFAULT;
  while (*mode) {
    int m = *mode++;
    switch (m) {
    case 'i':
      interval = 0;
      while (*mode >= '0' && *mode <= '9')
	interval = interval * 10 + (*mode++ - '0');
      if (interval <= 0) interval = 1;
      break;
#if LJ_HASJIT
    case 'l': case 'f':
      L2J(L)->prof_mode = m;
      lj_trace_flushall(L);
      break;
#endif
    default:  
      break;
    }
  }
  if (ps->g) {
    luaJIT_profile_stop(L);
    if (ps->g) return;  
  }
  ps->g = G(L);
  ps->interval = interval;
  ps->cb = cb;
  ps->data = data;
  ps->samples = 0;
  lj_buf_init(L, &ps->sb);
  profile_timer_start(ps);
}


LUA_API void luaJIT_profile_stop(lua_State *L)
{
  ProfileState *ps = &profile_state;
  global_State *g = ps->g;
  if (G(L) == g) {  
    ps->g = NULL;
    profile_timer_stop(ps);
    g->hookmask &= ~HOOK_PROFILE;
    lj_dispatch_update(g, 0);
#if LJ_HASJIT
    G2J(g)->prof_mode = 0;
    lj_trace_flushall(L);
#endif
    lj_buf_free(g, &ps->sb);
    ps->sb.w = ps->sb.e = NULL;
  }
}


LUA_API const char *luaJIT_profile_dumpstack(lua_State *L, const char *fmt,
					     int depth, size_t *len)
{
  ProfileState *ps = &profile_state;
  SBuf *sb = &ps->sb;
  setsbufL(sb, L);
  lj_buf_reset(sb);
  lj_debug_dumpstack(L, sb, fmt, depth);
  *len = (size_t)sbuflen(sb);
  return sb->b;
}

#endif
