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

//#include "LightWeightInstanceSubsystem.generated.h"

ENGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogLightWeightInstance, Log, Warning);

//DECLARE_DYNAMIC_DELEGATE_RetVal_OneParam(FActorInstanceHandle, FOnActorReady, FActorInstanceHandle, InHandle);

struct ENGINE_API FLightWeightInstanceSubsystem
{

	friend struct FActorInstanceHandle;
	friend class ALightWeightInstanceManager;

	static FLightWeightInstanceSubsystem& Get()
	{
		if (!LWISubsystem)
		{
			LWISubsystem = MakeShareable(new FLightWeightInstanceSubsystem());
		}
		return *LWISubsystem;
	}

	// returns the instance manager that handles the given handle
	ALightWeightInstanceManager* FindLightWeightInstanceManager(const FActorInstanceHandle& Handle) const;

	// returns the instance manager that handles actors of type ActorClass in level Level
	ALightWeightInstanceManager* FindLightWeightInstanceManager(UClass* ActorClass, ULevel* Level) const;

	// returns the instance manager that handles instances of type Class that live in Level
	UFUNCTION(Server, Unreliable)
	ALightWeightInstanceManager* FindOrAddLightWeightInstanceManager(UClass* ActorClass, ULevel* Level);

	// Returns the actor specified by Handle. This may require loading and creating the actor object.
	AActor* GetActor(const FActorInstanceHandle& Handle);

	// Returns the actor specified by Handle if it exists. Returns nullptr if it doesn't
	AActor* GetActor_NoCreate(const FActorInstanceHandle& Handle) const;

	// Returns the class of the actor specified by Handle.
	UClass* GetActorClass(const FActorInstanceHandle& Handle);

	FVector GetLocation(const FActorInstanceHandle& Handle);

	FString GetName(const FActorInstanceHandle& Handle);

	ULevel* GetLevel(const FActorInstanceHandle& Handle);

	// returns true if the object represented by Handle is in InLevel
	bool IsInLevel(const FActorInstanceHandle& Handle, const ULevel* InLevel);

protected:
	UClass* FindBestInstanceManagerClass(const UClass* ActorClass);

	// returns the index associated with Manager
	int32 GetManagerIndex(const ALightWeightInstanceManager* Manager) const;

	// returns the light weight instance manager at index Index
	const ALightWeightInstanceManager* GetManagerAt(int32 Index) const;

private:
	/** Application singleton */
	static TSharedPtr<FLightWeightInstanceSubsystem> LWISubsystem;

	// TODO: preallocate the size of this based on a config variable
	UPROPERTY()
	TArray<ALightWeightInstanceManager*> LWInstanceManagers;
};
