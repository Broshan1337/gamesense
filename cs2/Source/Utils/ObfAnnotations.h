#pragma once














#if defined(NEVERSNOOZE_OBFUSCATE)
#define NS_OBF_FLATTEN __attribute__((annotate("+fla")))    
#define NS_OBF_ICALL __attribute__((annotate("+icall")))    
#define NS_OBF_INDGV __attribute__((annotate("+indgv")))    
#define NS_OBF_CIE __attribute__((annotate("+cie")))        


#else
#define NS_OBF_FLATTEN
#define NS_OBF_ICALL
#define NS_OBF_INDGV
#define NS_OBF_CIE
#endif
