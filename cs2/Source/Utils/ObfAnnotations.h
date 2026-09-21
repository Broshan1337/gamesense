#pragma once

// Per-function Arkari (goron-derived OLLVM) obfuscation opt-in.
//
// Stock toolchains (GCC / system clang) ignore these - the macros expand to nothing.
// When the module is built with the Arkari clang (NEVERSNOOZE_OBFUSCATE), the annotate
// attribute becomes function metadata consumed by the obfuscation passes at -O time
// (see obfuscator/Arkari/llvm/lib/Transforms/Obfuscation/ObfuscationOptions.cpp:
// "+<opt>" enables, "-<opt>" disables per function; the global -mllvm flags stay OFF and
// only -arkari-cfg=<path> is passed so the pass manager runs with per-function policy).
//
// DISCIPLINE (this is what keeps FPS safe): annotate ONLY cold, secret-carrying code -
// vault decryptions, session verification, integrity checks. Never the per-frame paths
// (present/render/CreateMove dispatch); flattening those costs milliseconds per frame for
// zero security. A flattening "level" suffix (^irobf-cff=N) exists if a pass needs tuning.
#if defined(NEVERSNOOZE_OBFUSCATE)
#define NS_OBF_FLATTEN __attribute__((annotate("+fla")))    // control flow flattening
#define NS_OBF_ICALL __attribute__((annotate("+icall")))    // indirect call + target encryption
#define NS_OBF_INDGV __attribute__((annotate("+indgv")))    // indirect global reads
#define NS_OBF_CIE __attribute__((annotate("+cie")))        // integer constant encryption
// NOTE: string encryption (cse) is enabled GLOBALLY via the arkari-cfg file (lazy
// one-time decrypt per string, safe on warm paths) - there is no per-function +cse use.
#else
#define NS_OBF_FLATTEN
#define NS_OBF_ICALL
#define NS_OBF_INDGV
#define NS_OBF_CIE
#endif
