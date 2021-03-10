// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "RemoteControlEntity.h"

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RemoteControlBinding.h"
#include "RemoteControlPreset.h"
#include "UObject/SoftObjectPath.h"

#include "RemoteControlActor.generated.h"

class URemoteControlPreset;

/**
 * Represents an actor exposed in the panel.
 */
USTRUCT(BlueprintType)
struct REMOTECONTROL_API FRemoteControlActor : public FRemoteControlEntity
{
	GENERATED_BODY()

	FRemoteControlActor() = default;

	FRemoteControlActor(URemoteControlPreset* InOwner, AActor* Actor, FName Label)
		: FRemoteControlEntity(InOwner, Label)
		, Path(Actor)
	{
	}

	AActor* GetActor() const
	{
		if (Bindings.Num() && Bindings[0].IsValid())
		{
			return Cast<AActor>(Bindings[0]->Resolve());
		}
		return nullptr;
	}

	void SetActor(AActor* InActor)
	{
		if (Owner.IsValid())
		{
			Owner->Modify();
		}

		if (ensure(Bindings.Num()) && Bindings[0].IsValid())
		{
			Bindings[0]->SetBoundObject(InActor);
		}
	}

public:
	/**
	 * Path to the exposed object.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlEntity")
	FSoftObjectPath Path;
};