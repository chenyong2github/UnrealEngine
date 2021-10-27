// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ObjectSnapshotData.h"
#include "SubobjectSnapshotData.generated.h"

USTRUCT()
struct LEVELSNAPSHOTS_API FSubobjectSnapshotData : public FObjectSnapshotData
{
	GENERATED_BODY()

	static FSubobjectSnapshotData MakeSkippedSubobjectData()
	{
		FSubobjectSnapshotData Result;
		Result.bWasSkippedClass = true;
		return Result;
	}

	/* Index to FWorldSnapshotData::SerializedObjectReferences */
	UPROPERTY()
	int32 OuterIndex = INDEX_NONE;

	UPROPERTY()
	FSoftClassPath Class;

	/** Whether this class was marked as unsupported when the snapshot was taken */
	UPROPERTY()
	bool bWasSkippedClass = false;

	/** Allocated in snapshot world */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> SnapshotObject;

	/** Allocated in editor world */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> EditorObject;
};