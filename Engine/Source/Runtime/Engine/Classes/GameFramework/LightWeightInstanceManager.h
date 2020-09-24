// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectBaseUtility.h"
#include "UObject/Object.h"
#include "Engine/EngineTypes.h"
#include "Engine/EngineBaseTypes.h"
#include "Templates/SharedPointer.h"

#include "Actor.h"

#include "LightWeightInstanceManager.generated.h"


struct FActorSpawnParameters;
//DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FActorInstanceHandle, FOnActorReady, FActorInstanceHandle, InHandle);

// Used for initializing light weight instances.
struct ENGINE_API FLWIData
{
	FTransform Transform;
};

UCLASS(BlueprintType, Blueprintable)
class ENGINE_API ALightWeightInstanceManager : public AActor
{
	GENERATED_UCLASS_BODY()

	friend struct FLightWeightInstanceSubsystem;
	friend struct FActorInstanceHandle;
	friend class ULightWeightInstanceBlueprintFunctionLibrary;

public:
	virtual ~ALightWeightInstanceManager();

	virtual void Tick(float DeltaSeconds);

	// Returns the location of the instance specified by Handle
	FVector GetLocation(const FActorInstanceHandle& Handle) const;

	// Returns the name of the instance specified by Handle
	FString GetName(const FActorInstanceHandle& Handle) const;

	// Returns true if this manager stores instances that can be turned into full weight objects of class OtherClass
	bool DoesRepresentClass(const UClass* OtherClass) const;

	// Returns true if this manager is capable of representing objects of type OtherClass
	bool DoesAcceptClass(const UClass* OtherClass) const;

	// Returns the specific class that this manages
	UClass* GetRepresentedClass() const;

	// Returns the base class of types that this can manage
	UClass* GetAcceptedClass() const;

	// Sets the specific class that this manages
	virtual void SetRepresentedClass(UClass* ActorClass);

	// Returns the actor associated with Handle if one exists
	AActor* GetActorFromHandle(const FActorInstanceHandle& Handle);

	// Returns the index of the light weight instance associated with InActor if one exists; otherwise we return INDEX_NONE
	int32 FindIndexForActor(const AActor* InActor) const;

	virtual int32 ConvertCollisionIndexToLightWeightIndex(int32 InIndex) const;

protected:
	// Takes a polymorphic struct to set the initial data for a new instance
	int32 AddNewInstance(FLWIData* InitData);

	// Adds a new instance at the specified index. This function should only be called by AddNewInstance.
	// Provided to allow subclasses to update their per instance data
	virtual void AddNewInstanceAt(FLWIData* InitData, int32 Index);

	// Removes the instance
	virtual void RemoveInstance(int32 Index);

	// Returns true if we have current information for an instance at Index
	bool IsIndexValid(int32 Index) const;

	// Checks if we already have an actor for this handle. If an actor already exists we set the Actor variable on Handle and return true.
	bool FindActorForHandle(const FActorInstanceHandle& Handle) const;

	// Sets the parameters for actor spawning.
	virtual void SetSpawnParameters(FActorSpawnParameters& SpawnParams);

	// Called after spawning a new actor from a light weight instance
	virtual void PostActorSpawn(const FActorInstanceHandle& Handle);

	//
	// Data and replication functions
	//

public:

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:

	FString BaseInstanceName;

	UPROPERTY(EditDefaultsOnly, Replicated, Category=LightWeightInstance)
	TSubclassOf<AActor> RepresentedClass;

	UPROPERTY()
	TSubclassOf<AActor> AcceptedClass;

	//
	// Per instance data
	// Stored in separate arrays to make ticking more efficient when we need to update everything
	// 

	// Current per instance transforms
	UPROPERTY(ReplicatedUsing=OnRep_Transforms)
	TArray<FTransform> InstanceTransforms;
	UFUNCTION()
	virtual void OnRep_Transforms();

	//
	// Bookkeeping info
	//

	// keep track of which instances are currently represented by an actor
	TMap<int32, AActor*> Actors;

	// list of indices that we are no longer using
	UPROPERTY(Replicated)
	TArray<int32> FreeIndices;

	// handy way to check indices quickly so we don't need to iterate through the free indices list
	UPROPERTY(Replicated)
	TArray<bool> ValidIndices;
#if WITH_EDITOR
	//
	// Editor functions
	//
protected:
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;	
#endif
};
