set pagination off
set confirm off
printf "== GlobalContext ==\n"
set $initialized = GlobalContext::globalContext.isInitialized()
printf "isInitialized = %d\n", $initialized
if $initialized
  set $g = GlobalContext::instance()
  printf "instance @ %p, isComplete=%d\n", &$g, $g.isComplete()
  if $g.isComplete()
    set $full = $g.fullContext()
    set $psr = &$full.patternSearchResults
    printf "patternSearchResults @ %p\n", $psr
    printf "oneByteResults[0..7]: "
    set $i = 0
    while $i < 8
      printf "%d ", (int)$psr->oneByteResults[$i]
      set $i = $i + 1
    end
    printf "\nfourByteResults[0..15] (hex): "
    set $i = 0
    while $i < 16
      printf "%x ", (unsigned)$psr->fourByteResults[$i]
      set $i = $i + 1
    end
    printf "\neightByteResults[0..11] (hex): "
    set $i = 0
    while $i < 12
      printf "%lx ", (unsigned long)$psr->eightByteResults[$i]
      set $i = $i + 1
    end
    printf "\n"
  end
end
printf "== done ==\n"
detach
