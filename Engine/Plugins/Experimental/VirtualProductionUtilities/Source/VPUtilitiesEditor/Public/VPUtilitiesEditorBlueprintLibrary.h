// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "VPEditorTickableActorBase.h"
#include "VPTransientEditorTickableActorBase.h"
#include "VPUtilitiesEditorBlueprintLibrary.generated.h"


UCLASS()
class VPUTILITIESEDITOR_API UVPUtilitiesEditorBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Spawn an editor-only virtual production tickable actor */
	UFUNCTION(BlueprintCallable, Category = "Virtual Production")
	static AVPEditorTickableActorBase* SpawnVPEditorTickableActor(UObject* ContextObject, const TSubclassOf<AVPEditorTickableActorBase> ActorClass, const FVector Location, const FRotator Rotation);
		
	/** Spawn an editor-only Transient virtual production tickable actor */
	UFUNCTION(BlueprintCallable, Category = "Virtual Production")
	static AVPTransientEditorTickableActorBase* SpawnVPTransientEditorTickableActor(UObject* ContextObject, const TSubclassOf<AVPTransientEditorTickableActorBase> ActorClass, const FVector Location, const FRotator Rotation);
};