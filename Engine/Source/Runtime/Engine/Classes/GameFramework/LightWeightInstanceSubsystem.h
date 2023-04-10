// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "UObject/ObjectMacros.h"
#include "GameFramework/LightWeightInstanceManager.h"
#include "Templates/SharedPointer.h"

#include "Actor.h"

ENGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogLightWeightInstance, Log, Warning);

class FAutoConsoleVariableRef;

struct ENGINE_API FLightWeightInstanceSubsystem
{
	static FCriticalSection GetFunctionCS;

	static FLightWeightInstanceSubsystem& Get()
	{
		if (!LWISubsystem)
		{
			FScopeLock Lock(&GetFunctionCS);
			if (!LWISubsystem)
			{
				LWISubsystem = MakeShareable(new FLightWeightInstanceSubsystem());
			}
		}
		return *LWISubsystem;
	}

	FLightWeightInstanceSubsystem();
	~FLightWeightInstanceSubsystem();

	// Returns the instance manager that handles the given handle
	ALightWeightInstanceManager* FindLightWeightInstanceManager(const FActorInstanceHandle& Handle) const;

	// Returns the instance manager that handles actors of type ActorClass in level Level
	UE_DEPRECATED(5.3, "Use the version that takes in a position.")
	ALightWeightInstanceManager* FindLightWeightInstanceManager(UClass* ActorClass, const UDataLayerInstance* Layer, UWorld* World) const;

	// Returns the instance manager that handles instances of type Class that live in Level
	UE_DEPRECATED(5.3, "Use the version that takes in a position.")
	UFUNCTION(Server, Unreliable)
	ALightWeightInstanceManager* FindOrAddLightWeightInstanceManager(UClass* ActorClass, const UDataLayerInstance* DataLayer, UWorld* World);

	// Returns the instance manager that handles actors of type ActorClass in level Level
	ALightWeightInstanceManager* FindLightWeightInstanceManager(UClass& ActorClass, UWorld& World, const FVector& InPos, const UDataLayerInstance* DataLayer = nullptr) const;

	// Returns the instance manager that handles instances of type Class that live in Level
	UFUNCTION(Server, Unreliable)
	ALightWeightInstanceManager* FindOrAddLightWeightInstanceManager(UClass& ActorClass, UWorld& World, const FVector& InPos, const UDataLayerInstance* DataLayer = nullptr);

	// Returns the actor specified by Handle. This may require loading and creating the actor object.
	AActor* FetchActor(const FActorInstanceHandle& Handle);

	// Returns the actor specified by Handle if it exists. Returns nullptr if it doesn't
	AActor* GetActor_NoCreate(const FActorInstanceHandle& Handle) const;

	// Returns the class of the actor specified by Handle.
	UClass* GetActorClass(const FActorInstanceHandle& Handle);

	FVector GetLocation(const FActorInstanceHandle& Handle);

	FString GetName(const FActorInstanceHandle& Handle);

	ULevel* GetLevel(const FActorInstanceHandle& Handle);

	// Returns true if the object represented by Handle is in InLevel
	bool IsInLevel(const FActorInstanceHandle& Handle, const ULevel* InLevel);

	// Returns a handle to a new light weight instance that represents an object of type ActorClass
	FActorInstanceHandle CreateNewLightWeightInstance(UClass* ActorClass, FLWIData* InitData, UDataLayerInstance* Layer, UWorld* World);

	// deletes the instance identified by Handle
	void DeleteInstance(const FActorInstanceHandle& Handle);

	// Returns true if the handle can return an object that implements the interface U
	template<typename U>
	bool IsInterfaceSupported(const FActorInstanceHandle& Handle) const
	{
		if (ALightWeightInstanceManager* InstanceManager = FindLightWeightInstanceManager(Handle))
		{
			return InstanceManager->IsInterfaceSupported<U>();
		}

		return false;
	}

	// Returns an object that implements the interface I for Handle
	template<typename I>
	I* FetchInterfaceObject(const FActorInstanceHandle& Handle)
	{
		if (ALightWeightInstanceManager* InstanceManager = FindLightWeightInstanceManager(Handle))
		{
			return InstanceManager->FetchInterfaceObject<I>(Handle);
		}

		return nullptr;
	}

	// Helper that converts a position (world space) into a coordinate for the LWI grid.
	UE_DEPRECATED(5.3, "Use LWI Managers version of ConvertPositionToCoord()")
	static FInt32Vector3 ConvertPositionToCoord(const FVector& InPosition);

	// Add a manager to the subsystem, thread safe.
	bool AddManager(ALightWeightInstanceManager* Manager);

	// Remove a manager from the subsystem, thread safe.
	bool RemoveManager(ALightWeightInstanceManager* Manager);

protected:
	// Returns the class of the instance manager best suited to support instances of type ActorClass
	UClass* FindBestInstanceManagerClass(const UClass* ActorClass);

	// Returns the index associated with Manager
	int32 GetManagerIndex(const ALightWeightInstanceManager* Manager) const;

	// Returns the light weight instance manager at index Index
	const ALightWeightInstanceManager* GetManagerAt(int32 Index) const;

private:
	/** Application singleton */
	static TSharedPtr<FLightWeightInstanceSubsystem, ESPMode::ThreadSafe> LWISubsystem;

	// TODO: preallocate the size of this based on a config variable
	UPROPERTY()
	TArray<ALightWeightInstanceManager*> LWInstanceManagers;

	// Mutex to make sure we don't change the LWInstanceManagers array while reading/writing it.
	mutable FRWLock LWIManagersRWLock;

#ifdef WITH_EDITOR
private:
	FDelegateHandle OnLevelActorAddedHandle;
	FDelegateHandle OnLevelActorDeletedHandle;
#endif // WITH_EDITOR
};
