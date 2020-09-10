// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Actor.h"
#include "GameplayCueNotify_Instanced.generated.h"


/**
 * AGameplayCueNotify_Instanced
 *
 *	Base class for instanced gameplay cue notifies (such as Looping and BurstLatent).
 */
UCLASS(NotBlueprintable, notplaceable, Abstract, Meta = (ShowWorldContextPin), HideCategories = (Tick, Rendering, Input, Actor))
class AGameplayCueNotify_Instanced : public AGameplayCueNotify_Actor
{
	GENERATED_BODY()

public:

	AGameplayCueNotify_Instanced();

protected:

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void K2_DestroyActor() override;
	virtual void Destroyed() override;

	virtual bool WhileActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
	virtual bool OnRemove_Implementation(AActor* Target, const FGameplayCueParameters& Parameters) override;
};
