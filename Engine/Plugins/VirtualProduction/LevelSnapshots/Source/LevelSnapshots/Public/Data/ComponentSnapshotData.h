// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentInstanceDataCache.h"
#include "ObjectSnapshotData.h"
#include "ComponentSnapshotData.generated.h"

struct FWorldSnapshotData;
struct FPropertySelection;

USTRUCT()
struct LEVELSNAPSHOTS_API FComponentSnapshotData
{
	GENERATED_BODY()

	static TOptional<FComponentSnapshotData> SnapshotComponent(UActorComponent* OriginalComponent, FWorldSnapshotData& WorldData);
	
	bool IsRestoreSupportedForSavedComponent();
	
	void DeserializeIntoTransient(FObjectSnapshotData& SerializedComponentData, UActorComponent* ComponentToDeserializeInto, FWorldSnapshotData& WorldData);
	void DeserializeIntoWorld(FObjectSnapshotData& SerializedComponentData, UActorComponent* OriginalComponentToDeserializeInto, UActorComponent* DeserializedComponentCounterpart, FWorldSnapshotData& WorldData, const FPropertySelection& PropertySelection);

	/* Describes how the component was created */
	UPROPERTY()
	EComponentCreationMethod CreationMethod;
	
};