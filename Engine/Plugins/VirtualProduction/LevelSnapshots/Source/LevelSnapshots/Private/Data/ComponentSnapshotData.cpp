// Copyright Epic Games, Inc. All Rights Reserved.

#include "Data/ComponentSnapshotData.h"

#include "Archive/LoadSnapshotObjectArchive.h"
#include "Data/WorldSnapshotData.h"
#include "LevelSnapshotsLog.h"

#include "Components/ActorComponent.h"
#include "UObject/Package.h"

TOptional<FComponentSnapshotData> FComponentSnapshotData::SnapshotComponent(UActorComponent* OriginalComponent, FWorldSnapshotData& WorldData)
{
	if (OriginalComponent->CreationMethod == EComponentCreationMethod::UserConstructionScript)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Components created dynamically in the construction script are not supported (%s). Skipping..."), *OriginalComponent->GetPathName());
		return {};
	}
	FComponentSnapshotData Result;
	Result.CreationMethod = OriginalComponent->CreationMethod;
	return Result;
}

void FComponentSnapshotData::DeserializeIntoTransient(FObjectSnapshotData& SerializedComponentData, UActorComponent* ComponentToDeserializeInto, FWorldSnapshotData& WorldData, const FProcessObjectDependency& ProcessObjectDependency, UPackage* InLocalisationSnapshotPackage)
{
	if (ComponentToDeserializeInto->CreationMethod == EComponentCreationMethod::UserConstructionScript)
	{
		UE_LOG(LogLevelSnapshots, Warning, TEXT("Components created dynamically in the construction script are not supported (%s). Skipping..."), *ComponentToDeserializeInto->GetPathName());
		return;
	}
	
	// We cannot supply a template object for components whose CreationMethod is not Instance.
	// Yet every actor class, native and Blueprint, has their own archetype for each component, i.e. in the details panel the property has a yellow "Reset to Default" icon next to it if the value is different from the Blueprint's value.
	// We apply the CDO data we saved before we apply the component's serialized data.
		// Scenario: We have a custom Blueprint StaticMeshBP that has a UStaticMeshComponent. Suppose we change some default property value, like ComponentTags.
		// 1st case: We change no default values, then take a snapshot.
		//   - After the snapshot was taken, we change a default value.
		//   - The value of the property was not saved in the component's serialized data because the value was equal to the CDO's value.
		//   - When applying the snapshot, we will override the new default value because we serialize the CDO here. Good.
		// 2nd case: We change some default value, then take a snapshot
		//   - The value was saved into the component's serialized data because it was different from the CDO's default value at the time of taking the snapshot
		//   - We apply the CDO and afterwards we override it with the serialized data. Good.
	WorldData.SerializeClassDefaultsInto(ComponentToDeserializeInto);
	
	FLoadSnapshotObjectArchive::ApplyToSnapshotWorldObject(SerializedComponentData, WorldData, ComponentToDeserializeInto, ProcessObjectDependency, InLocalisationSnapshotPackage);
}
