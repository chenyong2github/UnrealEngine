// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RemoteControlEntity.h"
#include "UObject/SoftObjectPath.h"
#include "RemoteControlBinding.h"
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
		if (ensure(Bindings.Num()) && Bindings[0].IsValid())
		{
			Bindings[0]->SetBoundObject(InActor);
		}
	}

public:
	/**
	 * Path to the exposed object.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FSoftObjectPath Path;
};