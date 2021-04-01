// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ObjectSnapshotData.h"
#include "SubobjectSnapshotData.generated.h"

USTRUCT()
struct LEVELSNAPSHOTS_API FSubobjectSnapshotData : public FObjectSnapshotData
{
	GENERATED_BODY()

	/* Index to FWorldSnapshotData::SerializedObjectReferences */
	UPROPERTY()
	int32 OuterIndex;

	UPROPERTY()
	FSoftClassPath Class;
	
};