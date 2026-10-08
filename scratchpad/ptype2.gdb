set pagination off
set confirm off
printf "== ManuallyDestructible<GlobalContext> ==\n"
ptype /o ManuallyDestructible<GlobalContext>
printf "== DeferredCompleteObject<PartialGlobalContext, FullGlobalContext> ==\n"
ptype /o DeferredCompleteObject<PartialGlobalContext, FullGlobalContext>
detach
