




TREDEF(RECERR,	"error thrown or hook called during recording")
TREDEF(TRACEUV,	"trace too short")
TREDEF(TRACEOV,	"trace too long")
TREDEF(STACKOV,	"trace too deep")
TREDEF(SNAPOV,	"too many snapshots")
TREDEF(BLACKL,	"blacklisted")
TREDEF(RETRY,	"retry recording")
TREDEF(NYIBC,	"NYI: bytecode %s")


TREDEF(LLEAVE,	"leaving loop in root trace")
TREDEF(LINNER,	"inner loop in root trace")
TREDEF(LUNROLL,	"loop unroll limit reached")


TREDEF(BADTYPE,	"bad argument type")
TREDEF(CJITOFF,	"JIT compilation disabled for function")
TREDEF(CUNROLL,	"call unroll limit reached")
TREDEF(DOWNREC,	"down-recursion, restarting")
TREDEF(NYIFFU,	"NYI: unsupported variant of FastFunc %s")
TREDEF(NYIRETL,	"NYI: return to lower frame")


TREDEF(STORENN,	"store with nil or NaN key")
TREDEF(NOMM,	"missing metamethod")
TREDEF(IDXLOOP,	"looping index lookup")
TREDEF(NYITMIX,	"NYI: mixed sparse/dense table")


TREDEF(NOCACHE,	"symbol not in cache")
TREDEF(NYICONV,	"NYI: unsupported C type conversion")
TREDEF(NYICALL,	"NYI: unsupported C function type")


TREDEF(GFAIL,	"guard would always fail")
TREDEF(PHIOV,	"too many PHIs")
TREDEF(TYPEINS,	"persistent type instability")


TREDEF(MCODEAL,	"failed to allocate mcode memory")
TREDEF(MCODEOV,	"machine code too long")
TREDEF(MCODELM,	"hit mcode limit (retrying)")
TREDEF(SPILLOV,	"too many spill slots")
TREDEF(BADRA,	"inconsistent register allocation")
TREDEF(NYIIR,	"NYI: cannot assemble IR instruction %d")
TREDEF(NYIPHI,	"NYI: PHI shuffling too complex")
TREDEF(NYICOAL,	"NYI: register coalescing too complex")

#undef TREDEF


