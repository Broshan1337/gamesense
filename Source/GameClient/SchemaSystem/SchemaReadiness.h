#pragma once

// When schema-resolved field offsets may first be trusted, in map-elapsed seconds.
//
// This used to be 15 seconds, in two separate places, for a reason that turned out not to be real.
// It was introduced on the theory that the six crashes inside libschemasystem.so were a TOCTOU race
// against the engine asynchronously populating the type scope during map load, so waiting longer
// would avoid them. Test round 8 disproved that outright: with the 15s gate in place the crash
// happened at the exact instant curtime crossed 15.0, at the same address as every previous one.
// The real cause was a wrong pointer TYPE - a CSchemaSystem* being passed where
// FindDeclaredClassOrEnum wanted a CSchemaSystemTypeScope* - and it was fixed properly in round 9
// by obtaining the scope through CSchemaSystem::FindTypeScopeForName. The gate was never protecting
// against anything; it just outlived the theory that produced it.
//
// What is kept is a much smaller gate, for the one thing that IS true: there is no point asking for
// field offsets before the client is meaningfully running. A second and a half is comfortably past
// that without being something a player notices.
namespace schema_readiness
{

// Before this, the client-scope lookup is skipped and the skin changer does not run.
constexpr float kMinMapTime = 1.5f;

// Hard stop for RE-resolving offsets that keep coming back incomplete. Offsets are re-resolved
// rather than cached-on-first-touch (see HookContext::resolveOffsets), which is what actually made
// lowering the gate safe - but a field genuinely removed by a game update would otherwise never
// resolve and would turn into a schema walk on every frame forever. Past this point the best answer
// obtained so far is accepted and kept, and an unresolved offset simply makes its feature no-op.
constexpr float kGiveUpMapTime = 30.0f;

}
