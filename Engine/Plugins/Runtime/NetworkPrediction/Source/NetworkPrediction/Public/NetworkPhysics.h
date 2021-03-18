// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Subsystems/WorldSubsystem.h"
#include "Engine/World.h"
#include "Chaos/Core.h"
#include "Chaos/Particles.h"

#include "NetworkPhysics.generated.h"

NETWORKPREDICTION_API DECLARE_LOG_CATEGORY_EXTERN(LogNetworkPhysics, Log, All);

struct FNetworkPhysicsRewindCallback;

// PhysicsState that is networked and marshelled between GT and PT
USTRUCT()
struct FNetworkPhysicsState
{
	GENERATED_BODY()
	
	// Local proxy we are working with. Must be set locally on initialization.
	FSingleParticlePhysicsProxy* Proxy = nullptr;

	// Local handle used by UNetworkPhysicsManager for registering/unregistering
	int32 LocalManagedHandle;

	// Physics State
	Chaos::EObjectStateType ObjectState = Chaos::EObjectStateType::Uninitialized;
	Chaos::FVec3 Location;
	Chaos::FRotation3 Rotation;
	Chaos::FVec3 LinearVelocity;
	Chaos::FVec3 AngularVelocity;

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
		uint8 ObjStateByte = (uint8)ObjectState;
		Ar << ObjStateByte;
		ObjectState = (Chaos::EObjectStateType)ObjStateByte;

		// Fixme: quantize
		Ar << Frame;
		Ar << Location;
		Ar << Rotation;
		Ar << LinearVelocity;
		Ar << AngularVelocity;
		return true;
	}
};

template<>
struct TStructOpsTypeTraits<FNetworkPhysicsState> : public TStructOpsTypeTraitsBase2<FNetworkPhysicsState>
{
	enum
	{
		WithNetSerializer = true,
		WithIdentical = true,
	};
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

private:

	FNetworkPhysicsRewindCallback* RewindCallback;

	FDelegateHandle PostTickDispatchHandle; // NetRecv
	FDelegateHandle TickFlushHandle; // NetSend

	int32 LastProcessedFrame = INDEX_NONE;
	int32 LocalOffset = 0;

	TSortedMap<int32, int32> ManagedHandleToIndexMap;
	TSparseArray<FNetworkPhysicsState*> ManagedPhysicsStates;
	int32 LastFreeIndex = 0;
	int32 UniqueHandleCounter = 0;
};


// ========================================================================================

// Helper component for testing

UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class NETWORKPREDICTION_API UNetworkPhysicsComponent : public UActorComponent
{
	GENERATED_BODY()

public:

	UNetworkPhysicsComponent();

	virtual void InitializeComponent() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(Replicated, transient)
	FNetworkPhysicsState NetworkPhysicsState;
};