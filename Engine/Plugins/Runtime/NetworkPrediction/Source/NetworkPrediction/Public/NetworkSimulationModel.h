// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/EngineTypes.h"
#include "Net/UnrealNetwork.h" // For MakeRelative
#include "NetworkSimulationModel.generated.h"

// -------------------------------------------------------------------------------------------------------------------------------
//	This file contains supporting types that are used by both the UE4 side (UNetworkPredictionComponent etc) and templated
//	network simulation model side (TNetworkedSimulationModel).
// -------------------------------------------------------------------------------------------------------------------------------

#define NETSIM_MODEL_DEBUG !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

NETWORKPREDICTION_API DECLARE_LOG_CATEGORY_EXTERN(LogNetworkSim, Log, All);

UENUM()
enum class EReplicationProxyTarget: uint8
{
	ServerRPC,			// Client -> Server
	AutonomousProxy,	// Owning/Controlling client
	SimulatedProxy,		// Non owning client
	Replay,				// Replay net driver
	Debug,				// Replication target that is disabled in shipping
};

inline FString LexToString(EReplicationProxyTarget A)
{
	return *UEnum::GetValueAsString(TEXT("NetworkPrediction.EReplicationProxyTarget"), A);
}

// The parameters for NetSerialize that are passed around the system. Everything should use this, expecting to have to add more.
struct FNetSerializeParams
{
	FNetSerializeParams(FArchive& InAr) : Ar(InAr) { }
	FArchive& Ar;
};

struct FNetworkSimulationModelInitParameters
{
	int32 InputBufferSize=0;
	int32 SyncedBufferSize=0;
	int32 AuxBufferSize=0;
	int32 DebugBufferSize=0;
	int32 HistoricBufferSize=0;
};

struct FNetSimProcessedFrameDebugInfo
{
	int32 LocalGFrameNumber; // Local GFrame number
	float LocalDeltaTimeSeconds; // Local frame time
	TArray<int32> ProcessedKeyframes; // Which keyframes were processed this frame
	int32 LastProcessedKeyframe; // What LastProcessedKeyframe was at the end of the frame. Does NOT mean we processed it this frame!
	int32 HeadKeyframe; // Head keyframe of the inputbuffer when the frame ended

	void NetSerialize(const FNetSerializeParams& P)
	{	
		P.Ar << LocalGFrameNumber;
		P.Ar << LocalDeltaTimeSeconds;
		P.Ar << LastProcessedKeyframe;
		P.Ar << HeadKeyframe;

		SafeNetSerializeTArray_Default<31>(P.Ar, ProcessedKeyframes);
	}
};

enum class EStandardLoggingContext: uint8
{
	HeaderOnly,		// Minimal logging
	Full			// "Logs everything"
};

struct FStandardLoggingParameters
{
	FStandardLoggingParameters(FOutputDevice* InAr, EStandardLoggingContext InContext, int32 InKeyframe)
		: Ar(InAr), Context(InContext), Keyframe(InKeyframe) { }
	FOutputDevice* Ar;
	EStandardLoggingContext Context;
	int32 Keyframe;
};

UENUM()
enum class EVisualLoggingContext: uint8
{
	LastPredicted,		// The last predicted state. Where the character "is now"
	OtherPredicted,		// "Middle" states between LastComfirmed and LastPredicted. Recommend to draw small crumbs (not full model/cylinder/etc) to avoid washing out the scene.
	LastConfirmed,		// The last confirmed state from the server
	FirstMispredicted,	// The first state that was mispredicted on the client. This correlates with LastConfirmed.
	OtherMispredicted,	// subsequent mispredicted states on the client. Recommend to draw small crumbs (not full model/cylinder/etc) to avoid washing out the scene.
	LastMispredicted,	// The latest predictive state when a misprediction was detected.
	CurrentServerPIE,	// The current server position *right now*. Only available in PIE
	MAX
};

inline FString LexToString(EVisualLoggingContext A)
{
	return *UEnum::GetValueAsString(TEXT("NetworkPrediction.EVisualLoggingContext"), A);
}

enum class EVisualLoggingDrawType : uint8
{
	Full,				// Draw "the full thing" (maybe a collision capsule for example)
	Crumb,				// Draw a small/minimal representation (E.g, a point or small axis)
};

enum class EVisualLoggingLifetime : uint8
{
	Transient,			// This logging is transient and will (probably) be done every frame. Don't persist (in contexts where that would matter).
	Persistent,			// This is more of a persistent/one-off event that should be drawn for some longer amount of time (probably configurable at whatever level is doing the logging)
};

struct FVisualLoggingParameters
{
	FVisualLoggingParameters(EVisualLoggingContext InContext, int32 InKeyframe, EVisualLoggingLifetime InLifetime) :
		 Context(InContext), Keyframe(InKeyframe), Lifetime(InLifetime) { }

	EVisualLoggingContext Context;
	int32 Keyframe;
	EVisualLoggingLifetime Lifetime;

	FColor GetDebugColor() const { return DebugColors[(int32)Context]; }
	static NETWORKPREDICTION_API FColor DebugColors[(int32)EVisualLoggingContext::MAX];
};

// -------------------------------------------------------------------------------------------------------------------------------
// Interface that the proxy talks to. This is what will implement the replication.
// -------------------------------------------------------------------------------------------------------------------------------
class IReplicationProxy
{
public:

	virtual ~IReplicationProxy() { }
	
	virtual void NetSerializeProxy(EReplicationProxyTarget Target, const FNetSerializeParams& Params) = 0;
	virtual int32 GetProxyDirtyCount(EReplicationProxyTarget Target) = 0;
};

// -------------------------------------------------------------------------------------------------------------------------------
//	FReplicationProxy
//	Replicated USTRUCT that point to the IReplicationProxy to do the replication.
// -------------------------------------------------------------------------------------------------------------------------------
USTRUCT()
struct FReplicationProxy
{
	GENERATED_BODY()

	void Init(IReplicationProxy* InNetworkSimModel, EReplicationProxyTarget InReplicationTarget);
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
	void OnPreReplication();	
	bool Identical(const FReplicationProxy* Other, uint32 PortFlags) const;
	
private:

	IReplicationProxy* NetworkSimModel;
	EReplicationProxyTarget ReplicationTarget;
	int32 CachedDirtyCount = 0;
};

template<>
struct TStructOpsTypeTraits<FReplicationProxy> : public TStructOpsTypeTraitsBase2<FReplicationProxy>
{
	enum
	{
		WithNetSerializer = true,
		WithIdentical = true,
	};
};

// -------------------------------------------------------------------------------------------------------------------------------
//	FServerRPCProxyParameter
//	Used for the client->Server RPC. Since this is instantiated on the stack by the replication system prior to net serializing,
//	we have no opportunity to point the RPC parameter to the member variables we want. So we serialize into a generic temp byte buffer
//	and move them into the real buffers on the component in the RPC body (via ::NetSerialzeToProxy).
// -------------------------------------------------------------------------------------------------------------------------------
USTRUCT()
struct FServerReplicationRPCParameter
{
	GENERATED_BODY()

	// Receive flow: ctor() -> NetSerializetoProxy
	FServerReplicationRPCParameter() : Proxy(nullptr)	{ }
	void NetSerializeToProxy(FReplicationProxy& InProxy);

	// Send flow: ctor(Proxy) -> NetSerialize
	FServerReplicationRPCParameter(FReplicationProxy& InProxy) : Proxy(&InProxy) { }
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

private:

	static TArray<uint8> TempStorage;

	FReplicationProxy* Proxy;
	int64 CachedNumBits = -1;
	class UPackageMap* CachedPackageMap = nullptr;
};

template<>
struct TStructOpsTypeTraits<FServerReplicationRPCParameter> : public TStructOpsTypeTraitsBase2<FServerReplicationRPCParameter>
{
	enum
	{
		WithNetSerializer = true
	};
};

// Helper struct to bypass the bandwidth limit imposed by the engine's NetDriver (QueuedBits, NetSpeed, etc).
// This is really a temp measure to make the system easier to drop in/try in a project without messing with your engine settings.
// (bandwidth optimizations have not been done yet and the system in general hasn't been stressed with packetloss / gaps in command streams)
// So, you are free to use this in your own code but it may be removed one day. Hopefully in general bandwidth limiting will also become more robust.
struct FScopedBandwidthLimitBypass
{
	FScopedBandwidthLimitBypass(AActor* OwnerActor);
	~FScopedBandwidthLimitBypass();
private:

	int64 RestoreBits = 0;
	class UNetConnection* CachedNetConnection = nullptr;
};