// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentInstanceDataCache.h"
#include "ObjectSnapshotData.h"
#include "ComponentSnapshotData.generated.h"

class UPackage;
struct FWorldSnapshotData;
struct FPropertySelection;

USTRUCT()
struct LEVELSNAPSHOTS_API FComponentSnapshotData
{
	GENERATED_BODY()

	static TOptional<FComponentSnapshotData> SnapshotComponent(UActorComponent* OriginalComponent, FWorldSnapshotData& WorldData);
	
	bool IsRestoreSupportedForSavedComponent() const;
	
	void DeserializeIntoTransient(FObjectSnapshotData& SerializedComponentData, UActorComponent* ComponentToDeserializeInto, FWorldSnapshotData& WorldData, UPackage* InLocalisationSnapshotPackage);
	/* Describes how the component was created */
	UPROPERTY()
	EComponentCreationMethod CreationMethod;
	
};