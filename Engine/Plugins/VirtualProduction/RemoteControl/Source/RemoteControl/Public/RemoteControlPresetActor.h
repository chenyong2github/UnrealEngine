// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "GameFramework/Actor.h"
#include "RemoteControlPresetActor.generated.h"

class URemoteControlPreset;

/**
 * Actor that wraps a remote control preset, allows linking a specific preset to a level.
 */
UCLASS(hideCategories = (Rendering, Physics, LOD, Activation, Input))
class REMOTECONTROL_API ARemoteControlPresetActor : public AActor
{
public:
	GENERATED_BODY()

	ARemoteControlPresetActor()
		: Preset(nullptr)
	{}
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="General")
	URemoteControlPreset* Preset;
};