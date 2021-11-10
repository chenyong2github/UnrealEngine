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
	
public:

	/* Is this actor captured by the snapshot system? */
	static bool IsActorDesirableForCapture(const AActor* Actor);
	/** Can this actor be restored? Stronger requirement than IsActorDesirableForCapture: we may capture the data but not support restoring it at the moment. */
	static bool IsActorRestorable(const AActor* Actor);
	
	/* Is this component captured by the snapshot system? */
	static bool IsComponentDesirableForCapture(const UActorComponent* Component);
	
	/* Is this subobject class captured by the snapshot system?*/
	static bool IsSubobjectClassDesirableForCapture(const UClass* SubobjectClass);
	/** Is this subobject captured by the snapshot system? */
	static bool IsSubobjectDesirableForCapture(const UObject* Subobject);

	/** Can the property be captured? */
	static bool IsPropertyDesirableForCapture(const FProperty* Property);
	/* Is this property never captured by the snapshot system? */
	static bool IsPropertyExplicitlyUnsupportedForCapture(const FProperty* Property);
	/* Is this property always captured by the snapshot system? */
	static bool IsPropertyExplicitlySupportedForCapture(const FProperty* Property);

	/* The actor did not exist in the snapshot. Should we show it in the list of added actors? */
	static bool ShouldConsiderNewActorForRemoval(const AActor* Actor);
	
	/* Is this property captured by the snapshot system? */
	static bool IsRestorableProperty(const FProperty* LeafProperty);
};
