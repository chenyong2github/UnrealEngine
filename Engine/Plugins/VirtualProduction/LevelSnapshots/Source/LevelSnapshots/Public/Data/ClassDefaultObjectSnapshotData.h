// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ObjectSnapshotData.h"
#include "ClassDefaultObjectSnapshotData.generated.h"

USTRUCT()
struct LEVELSNAPSHOTS_API FClassDefaultObjectSnapshotData : public FObjectSnapshotData
{
	GENERATED_BODY()

	/* Holds a value if the value was already loaded from the snapshot. */
	UPROPERTY(Transient)
	UObject* CachedLoadedClassDefault = nullptr;
	
};

