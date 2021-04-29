// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class AActor;
class FLevelSnapshotsModule;
class FProperty;
class UActorComponent;

class LEVELSNAPSHOTS_API FSnapshotRestorability
{
	friend FLevelSnapshotsModule;
	/**
	 * Performance optimisation for IsRestorableProperty. Set by FLevelSnapshotsModule.
	 * Using cached module halves execution time of IsRestorableProperty.
	 */
	static FLevelSnapshotsModule* Module;
	
public:

	/* Is this actor captured by the snapshot system? */
	static bool IsActorDesirableForCapture(const AActor* Actor);
	/* Is this actor captured by the snapshot system? */
	static bool IsComponentDesirableForCapture(const UActorComponent* Component);
	
	/* Is this property captured by the snapshot system? */
	static bool IsRestorableProperty(const FProperty* LeafProperty);
};
