// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ComponentInstanceDataCache.h"
#include "Templates/UnrealTemplate.h"

class AActor;
struct FSubobjectSnapshotData;

/**
 * Convenience type that calls FLevelSnaphshotsModule::OnPreRecreateComponent and FLevelSnaphshotsModule::OnPostRecreateComponent.
 */
class FRecreateComponentScope : public FNoncopyable
{
	const FSubobjectSnapshotData& SubobjectSnapshotData;
public:

	FRecreateComponentScope(
		const FSubobjectSnapshotData& SubobjectSnapshotData,
		AActor* Owner,
		FName ComponentName,
		UClass* ComponentClass,
		EComponentCreationMethod CreationMethod
		);
	~FRecreateComponentScope();
};