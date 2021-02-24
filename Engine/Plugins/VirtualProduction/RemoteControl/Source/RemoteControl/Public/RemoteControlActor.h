// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RemoteControlEntity.h"
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

	AActor* GetActor()
	{
		return Cast<AActor>(Path.ResolveObject());
	}

	/**
	 * Path to the exposed object.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "RemoteControlPreset")
	FSoftObjectPath Path;
};