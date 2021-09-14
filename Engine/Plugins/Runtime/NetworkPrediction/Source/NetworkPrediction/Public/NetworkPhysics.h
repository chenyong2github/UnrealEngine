// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Subsystems/WorldSubsystem.h"
#include "Engine/World.h"
#include "Chaos/Core.h"
#include "Chaos/Particles.h"
#include "Components/ActorComponent.h"
#include "UObject/ObjectKey.h"
#include "Misc/NetworkGuid.h"
#include "RewindData.h"

#include "NetworkPhysics.generated.h"

NETWORKPREDICTION_API DECLARE_LOG_CATEGORY_EXTERN(LogNetworkPhysics, Log, All);

struct FNetworkPhysicsRewindCallback;
class FMockObjectManager;
class FSingleParticlePhysicsProxy;

namespace Chaos { struct FSimCallbackInputAndObject; }

#ifndef NETWORK_PHYSICS_REPLICATE_EXTRAS
	#if (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)
		#define NETWORK_PHYSICS_REPLICATE_EXTRAS 1
	#else
		#define NETWORK_PHYSICS_REPLICATE_EXTRAS 0
	#endif
#endif

// FIXME: use FRigidBodyState instead
struct FBasePhysicsState
{
	Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;
	Chaos::FVec3 Location;
	Chaos::FRotation3 Rotation;
	Chaos::FVec3 LinearVelocity;
	Chaos::FVec3 AngularVelocity;
};

struct FDebugPhysicsState
{	
	Chaos::FVec3 Force;
	Chaos::FVec3 Torque;	

	Chaos::FVec3 LinearImpulse;
	Chaos::FVec3 AngularImpulse;
};


// PhysicsState that is networked and marshelled between GT and PT
USTRUCT()
struct FNetworkPhysicsState
{
	GENERATED_BODY()
	
	// Local proxy we are working with. Must be set locally on initialization.
	FSingleParticlePhysicsProxy* Proxy = nullptr;

	// Local handle used by UNetworkPhysicsManager for registering/unregistering
	int32 LocalManagedHandle = INDEX_NONE;

	// The actual physics properties
	FBasePhysicsState Physics;

	// Frame number associated with this data
	//				GT		PT
	//	Client:	   Remote	Local	
	//	Server:	   Local	Local
	int32 Frame = 0;

	// This is what networking will diff to tell if things have changed
	bool Identical(const FNetworkPhysicsState* Other, uint32 PortFlags) const
	{
		return Frame == Other->Frame;
	}

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
	{
		uint8 ObjStateByte = (uint8)Physics.ObjectState;
		Ar << ObjStateByte;
		Physics.ObjectState = (Chaos::EObjectStateType)ObjStateByte;

		// Fixme: quantize
		Ar << Frame;
		Ar << Physics.Location;
		Ar << Physics.Rotation;
		Ar << Physics.LinearVelocity;
		Ar << Physics.AngularVelocity;

#if NETWORK_PHYSICS_REPLICATE_EXTRAS
		for (int32 i=(int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::PrePushData; i < (int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::NumPhases; ++i)
		{
			Ar << DebugState[i].Force;
			Ar << DebugState[i].Torque;
			Ar << DebugState[i].LinearImpulse;
			Ar << DebugState[i].AngularImpulse;
		}
#endif

		return true;
	}

	// LOD: should probably be moved out of this struct
	int32 LocalLOD = 0;
	AActor* OwningActor = nullptr;

#if NETWORK_PHYSICS_REPLICATE_EXTRAS
	FDebugPhysicsState	DebugState[(int32)Chaos::FFrameAndPhase::EParticleHistoryPhase::NumPhases];
#endif
};

template<>
struct TStructOpsTypeTraits<FNetworkPhysicsState> : public TStructOpsTypeTraitsBase2<FNetworkPhysicsState>
{
	enum
	{
		WithNetSerializer = true,
		WithIdentical = true,
		WithNetSharedSerialization = true
	};
};

class NETWORKPREDICTION_API INetworkPhysicsSubsystem
{
public:
	virtual ~INetworkPhysicsSubsystem() = default;
	virtual void PostNetRecv(UWorld* World, int32 LocalOffset, int32 LastProcessedFrame) = 0;
	virtual void PreNetSend(UWorld* World, float DeltaSeconds) = 0;

	// Finalizes InputCmds, marshalls them to PT and Network.
	virtual void ProcessInputs_External(int32 PhysicsStep, int32 LocalFrameOffset, bool& bOutSendClientInputCmd) { }
	
	// Records "Final" Inputs for a frame and marshalls them back to GT for networking
	virtual void ProcessInputs_Internal(int32 PhysicsStep) { }

	// ShouldReconcile
	virtual int32 TriggerRewindIfNeeded_Internal(int32 LastCompletedStep) { return INDEX_NONE; }

	// Applies Corrections
	virtual void PreResimStep_Internal(int32 PhysicsStep, bool bFirst) { }
};


UCLASS()
class NETWORKPREDICTION_API UNetworkPhysicsManager : public UWorldSubsystem
{
public:

	GENERATED_BODY()

	UNetworkPhysicsManager();

	// Subsystem Init/Deinit
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void OnWorldPostInit(UWorld* World, const UWorld::InitializationValues);

	void PostNetRecv();
	void PreNetSend(float DeltaSeconds);

	void RegisterPhysicsProxy(FNetworkPhysicsState* State);
	void UnregisterPhysicsProxy(FNetworkPhysicsState* State);

	template<typename T>
	T* RegisterSubsystem(TUniquePtr<INetworkPhysicsSubsystem>&& Sys) {  return (T*)SubSystems.Emplace_GetRef(T::GetName(), MoveTemp(Sys)).Value.Get(); }

	template<typename T>
	T* GetSubsystem()
	{
		auto* Pair = SubSystems.FindByPredicate([](const TPair<FName, TUniquePtr<INetworkPhysicsSubsystem>>& E) { return E.Key == T::GetName(); });
		return Pair ? (T*)Pair->Value.Get() : nullptr;
	};

	struct FDrawDebugParams
	{
		UWorld* DrawWorld;
		Chaos::FVec3 Loc;
		Chaos::FRotation3 Rot;
		FColor Color;
		float Lifetime = -1.f;
	};

	void RegisterPhysicsProxyDebugDraw(FNetworkPhysicsState* State, TUniqueFunction<void(const FDrawDebugParams&)>&& Func);

	void DumpDebugHistory();

private:

	void TickDrawDebug();

	FNetworkPhysicsRewindCallback* RewindCallback=nullptr;

	FDelegateHandle PostTickDispatchHandle; // NetRecv
	FDelegateHandle TickFlushHandle; // NetSend

	int32 LatestConfirmedFrame = INDEX_NONE;	// Latest frame the client has heard about from the server.
	int32 LatestSimulatedFrame = INDEX_NONE;	// Latest frame we have sent off to PT for simulation. Doesn't mean its been completed and marshalled back.
	int32 LocalOffset = 0; // Calculated client/server frame offset. ClientFrame = ServerFrame + LocalOffset

	int32 ClientServerMaxFrameDelta = 0;

	TSortedMap<int32, int32> ManagedHandleToIndexMap;
	TSparseArray<FNetworkPhysicsState*> ManagedPhysicsStates;
	int32 LastFreeIndex = 0;
	int32 UniqueHandleCounter = 0;

	TMap<FSingleParticlePhysicsProxy*, TUniqueFunction<void(const FDrawDebugParams&)>> DrawDebugMap;
	
	TArray<TPair<FName, TUniquePtr<INetworkPhysicsSubsystem>>> SubSystems;

	friend struct FNetworkPhysicsRewindCallback;
	void ProcessInputs_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs);

	void ProcessClientInputBuffers_External(int32 PhysicsStep, const TArray<Chaos::FSimCallbackInputAndObject>& SimCallbackInputs);

	// Historic GT recording for GT only (compiled out. probably temp until Insights integration is back)
	struct FDebugSnapshot
	{
		int32 LocalFrameOffset = 0;
		struct FDebugObject
		{
			FNetworkGUID NetGUID;
			TWeakObjectPtr<AActor> WeakOwningActor;
			FNetworkPhysicsState State;
		};

		TArray<FDebugObject> Objects;
	};

	TStaticArray<FDebugSnapshot, 1024> DebugSnapshots;
	int32 DebugSnapshotHead = 0;

	bool bRecordDebugSnapshots = false;
};

namespace UE_NETWORK_PHYSICS
{
	extern NETWORKPREDICTION_API int32 GBreakAtFrame;
	extern NETWORKPREDICTION_API int32 GServerFrame;
	extern NETWORKPREDICTION_API int32 GClientFrame;

	extern NETWORKPREDICTION_API bool ConditionalFrameBreakpoint();
	extern NETWORKPREDICTION_API void ConditionalFrameEnsure();

	// Read bIsServer and SimulationFrame from thread local storage. This is only enabled in development builds!
	extern NETWORKPREDICTION_API int32 DebugSimulationFrame();
	extern NETWORKPREDICTION_API bool DebugServer();
};

#ifndef NETWORK_PHYSICS_THREAD_CONTEXT
	#if (!UE_BUILD_SHIPPING && !UE_BUILD_TEST)
		#define NETWORK_PHYSICS_THREAD_CONTEXT 1
	#else
		#define NETWORK_PHYSICS_THREAD_CONTEXT 0
	#endif
#endif

#if NETWORK_PHYSICS_THREAD_CONTEXT
class NETWORKPREDICTION_API FNetworkPhysicsThreadContext : public TThreadSingleton<FNetworkPhysicsThreadContext>
{
public:

	void Update(int32 InFrame, bool InServer)
	{
		SimulationFrame = InFrame;
		bIsServer = InServer;
		int32* Ptr = InServer ? &UE_NETWORK_PHYSICS::GServerFrame : &UE_NETWORK_PHYSICS::GClientFrame;
		*Ptr = InFrame;
	}

	bool bIsServer;
	int32 SimulationFrame;
};
#endif