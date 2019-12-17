// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma  once

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	NetSimCue Traits: compile time settings for NetSimeCue types that determine who can invoke the event and who it replicates to.
//	See "Mock Cue Example" in NetworkedSimulationModelCues.cpp for minimal examples for using/setting traits.
//
//	There are two traits:
//
//	// Who can Invoke this Cue in their simulation (if this test fails, the Invoke call is supressed locally)
//	static constexpr uint8 InvokeMask { InInvokeMask };
//
//	// Does the cue replicate? (from authority). This will also determine if the cue needs to be saved locally for NetIdentical tests (to avoid double playing)
//	static constexpr ENetSimCueReplicationTarget ReplicationTarget { InReplicationTarget };
//
// ----------------------------------------------------------------------------------------------------------------------------------------------

enum class ESimulationTickContext : uint8
{
	None			= 0,
	Authority		= 1 << 0,
	Predict			= 1 << 1,
	Resimulate		= 1 << 2,

	AuthorityPredict	= Authority | Predict,
	PredictResimulate	= Predict | Resimulate,

	All				= (Authority | Predict | Resimulate),
};

enum class ENetSimCueReplicationTarget : uint8
{
	None,			// Do not replicate cue to anyone
	Interpolators,	// Only replicate to / accept on clients that are interpolating (not running the simulation themselves)
	All,			// Replicate to everyone
};


// ----------------------------------------------------------------------------------------------------------------------------------------------
//	NetSimCue Traits Presets. These are reasonable settings that may be appropriate in common cases. Presets are provided so that individual settings
//	are not duplicated throughout the code base and to establish consistent vocabulary for the common types.
//
//	For quick reference, we expect these to be the most common:
//		NetSimCueTraits::Weak - The default trait type and cheapest cue to use. No replication or NetIdentical testing. Never rolled back. Predicted but never resimulated.
//		NetSimCueTraits::Strong - The most robust trait type. Will replicate to everyone and will rollback/resimulate. Must implement NetSerialize/NetIdentical. Most expensive.
//
//	The other presets fall somewhere in the middle and require thought/nuance to decide if they are right for your case.
//
// ----------------------------------------------------------------------------------------------------------------------------------------------

namespace NetSimCueTraits
{
	// Default Traits: if you do not explicitly set Traits on your class, this is what it defaults to
	using Default = struct Weak;

	// Non replicated cue that only plays during "latest" simulate. Will not be played during rewind/resimulate.
	// Lightest weight cue. Best used for cosmetic, non critical events. Footsteps, impact effects, etc.
	struct Weak
	{
		static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority | (uint8)ESimulationTickContext::Predict };
		static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::None };
	};

	// Same as above but will only play on predicting client, not authority
	struct WeakClientOnly
	{
		static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Predict };
		static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::None };
	};

	// Will only play on the authority path and not replicate to anyone else
	struct AuthorityOnly
	{
		static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority };
		static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::None };
	};

	// Only invoked on authority and will replicate to everyone else. Not predicted so controlling client will see delays!
	// Best for events that are critical that cannot be rolled back/undown and do not need to be predicted.
	struct ReplicatedNonPredicted
	{
		static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority };
		static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::All };
	};

	// Replicated to interpolating proxies, predicted by autonomous proxy
	// Best for events you want everyone to see but don't need to get perfect in the predicting cases: doesn't need to rollback and cheap on cpu (no NetIdentical tests on predicted path)
	struct ReplicatedXOrPredicted
	{
		static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::Authority | (uint8)ESimulationTickContext::Predict };
		static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::Interpolators };
	};

	// Invoked and replicated to all. NetIdentical testing to avoid double playing, rollbackable so that it can (re)play during resimulates
	// Most expensive (bandwidth/CPU) and requires rollback callbacks to be implemented to be correct. But will always be shown "as correct as possible"
	struct Strong
	{
		static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::All };
		static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::All };
	};

	// Non replicated but if a resimulate happens, the cue is undone and replayed.
	// This is not common and doesn't really have a clear use case. But the system can support it.
	struct NonReplicatedResimulated
	{
		static constexpr uint8 InvokeMask { (uint8)ESimulationTickContext::All };
		static constexpr ENetSimCueReplicationTarget ReplicationTarget { ENetSimCueReplicationTarget::None };
	};
}

// Explicit trait settings. This can be used to explicitly set your traits without using a preset.
template<uint8 InInvokeMask, ENetSimCueReplicationTarget InReplicationTarget>
struct TNetSimCueTraitsExplicit
{
	// Who can Invoke this Cue in their simulation (if this test fails, the Invoke call is supressed locally)
	static constexpr uint8 InvokeMask { InInvokeMask };

	// Does the cue replicate? (from authority). This will also determine if the cue needs to be saved locally for NetIdentical tests (to avoid double playing)
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { InReplicationTarget };
};


// ----------------------------------------------------------------------------------------------------------------------------------------------
//	NetSimCue Traits template helpers
// ----------------------------------------------------------------------------------------------------------------------------------------------

// SFINAE helper
template<typename T>
struct TToVoid
{
	using type  = void;
};

// Helper to see if T has a Traits type or not
template<typename T, typename dummy=void>
struct THasNetSimCueTraits
{
	enum { Value = false };
};

template<typename T>
struct THasNetSimCueTraits<T, typename TToVoid<typename T::Traits>::type>
{
	enum { Value = true };
};

// Selects explicit traits set by T
template<typename T, bool HasTraits = THasNetSimCueTraits<T>::Value>
struct TSelectNetSimCueTraits
{
	using Traits = typename T::Traits;
};

// Fall back to Default traits
template<typename T>
struct TSelectNetSimCueTraits<T, false>
{
	using Traits = NetSimCueTraits::Default;
};

// Actual trait struct that we use to look up traits. The ways this can be set:
//	1. Explicitly specialize TNetSimCueTraits for your type (non intrusive, but does not inherit)
//	2. Explicitly set Traits inside your struct. E.g:
//			using Traits = TNetSimCueTraits_Strong; (intrustive but more concise, does support inherited types)
//	3. Automatically falls back to NetSimCueTraits::Default if not explicitly set above
template<typename TCue>
struct TNetSimCueTraits
{
	using Traits = typename TSelectNetSimCueTraits<TCue>::Traits;

	static constexpr uint8 InvokeMask { Traits::InvokeMask };
	static constexpr ENetSimCueReplicationTarget ReplicationTarget { Traits::ReplicationTarget };
};

// Type requirements: helper to determine if NetSerialize/NetIdentical functions need to be defined for user types based on the above traits
template <typename TCue>
struct TNetSimCueTypeRequirements
{
	enum {
		// NetSerialize is required if we ever need to replicate
		RequiresNetSerialize = (TNetSimCueTraits<TCue>::ReplicationTarget != ENetSimCueReplicationTarget::None),
		// Likewise for NetIdentical, but also if we plan to invoke during Resimulate too (even if non replicated, we use NetIdentical for comparisons. though this is probably a non practical use case).
		RequiresNetIdentical = (TNetSimCueTraits<TCue>::ReplicationTarget != ENetSimCueReplicationTarget::None) || (TNetSimCueTraits<TCue>::InvokeMask & (uint8)ESimulationTickContext::Resimulate)
	};
};

// ----------------------------------------------------------------------------------------------------------------------------------------------
//	Function checking helpers - helps us do clean checks for member function (NetSerialize/NetIdentical) when registering types
// ----------------------------------------------------------------------------------------------------------------------------------------------

// Helper to compile time check if NetSerialize exists
GENERATE_MEMBER_FUNCTION_CHECK(NetSerialize, void,, FArchive&);

// Helper to compile time check if NetIdentical exists (since argument is template type, must be wrapped in helper struct)
template<typename TCue>
struct THasNetIdenticalHelper
{
	GENERATE_MEMBER_FUNCTION_CHECK(NetIdentical, bool, const, const TCue&);
	enum { Value = THasMemberFunction_NetIdentical<TCue>::Value };
};

// Helper to call NetIdentical if type defines it
template<typename TCue, bool Enabled=THasNetIdenticalHelper<TCue>::Value>
struct TNetCueNetIdenticalHelper
{
	static bool CallNetIdenticalOrNot(const TCue& Cue, const TCue& Other) { ensure(false); return false; } // This should never be hit by cue types that don't need to NetIdentical
};

template<typename TCue>
struct TNetCueNetIdenticalHelper<TCue, true>
{
	static bool CallNetIdenticalOrNot(const TCue& Cue, const TCue& Other) { return Cue.NetIdentical(Other); }
};

// Helper to call NetSerialize if type defines it
template<typename TCue, bool Enabled=THasMemberFunction_NetSerialize<TCue>::Value>
struct TNetCueNetSerializeHelper
{
	static void CallNetSerializeOrNot(TCue& Cue, FArchive& Ar) { ensure(false); } // This should never be hit by cue types that don't need to NetSerialize
};

template<typename TCue>
struct TNetCueNetSerializeHelper<TCue, true>
{
	static void CallNetSerializeOrNot(TCue& Cue, FArchive& Ar) { Cue.NetSerialize(Ar); }
};