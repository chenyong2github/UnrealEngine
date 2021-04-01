// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ObjectSnapshotData.generated.h"

USTRUCT()
struct LEVELSNAPSHOTS_API FObjectSnapshotData
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<uint8> SerializedData;
	
};