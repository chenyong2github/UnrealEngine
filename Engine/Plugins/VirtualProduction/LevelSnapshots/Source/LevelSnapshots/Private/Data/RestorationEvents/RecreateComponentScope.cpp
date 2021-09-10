// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/RestorationEvents/RecreateComponentScope.h"

#include "LevelSnapshotsLog.h"
#include "LevelSnapshotsModule.h"
#include "Data/SubobjectSnapshotData.h"

#include "Components/ActorComponent.h"

FRecreateComponentScope::FRecreateComponentScope(
	const FSubobjectSnapshotData& SubobjectSnapshotData,
	AActor* Owner,
	FName ComponentName,
	UClass* ComponentClass,
	EComponentCreationMethod CreationMethod)
		:
	SubobjectSnapshotData(SubobjectSnapshotData)
{
	FLevelSnapshotsModule::GetInternalModuleInstance().OnPreRecreateComponent({ Owner, ComponentName, ComponentClass, CreationMethod });
}

FRecreateComponentScope::~FRecreateComponentScope()
{
	// Creation might fail in exceptional cases
	if (UActorComponent* Component = Cast<UActorComponent>(SubobjectSnapshotData.EditorObject))
	{
		FLevelSnapshotsModule::GetInternalModuleInstance().OnPostRecreateComponent(Component);
	}
	else
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Skipping calls to IRestorationListener::PostRecreateComponent"));
	}
}
