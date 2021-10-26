// Copyright Epic Games, Inc. All Rights Reserved.

#include "Archive/LoadSnapshotObjectArchive.h"

#include "LevelSnapshotsModule.h"
#include "WorldSnapshotData.h"
#include "Data/Util/SnapshotObjectUtil.h"

#include "Serialization/ObjectWriter.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Internationalization/TextPackageNamespaceUtil.h"

void FLoadSnapshotObjectArchive::ApplyToSnapshotWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InObjectToRestore, FProcessObjectDependency ProcessObjectDependency, UPackage* InLocalisationSnapshotPackage)
{
	ApplyToSnapshotWorldObject(
		InObjectData,
		InSharedData,
		InObjectToRestore,
		MoveTemp(ProcessObjectDependency),
#if USE_STABLE_LOCALIZATION_KEYS
		TextNamespaceUtil::EnsurePackageNamespace(InLocalisationSnapshotPackage)
#else
		FString()
#endif
		);
}

void FLoadSnapshotObjectArchive::ApplyToSnapshotWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InObjectToRestore, FProcessObjectDependency ProcessObjectDependency, const FString& InLocalisationNamespace)
{
	FLoadSnapshotObjectArchive Archive(InObjectData, InSharedData, InObjectToRestore, MoveTemp(ProcessObjectDependency));
#if USE_STABLE_LOCALIZATION_KEYS
	Archive.SetLocalizationNamespace(InLocalisationNamespace);
#endif

	InObjectToRestore->Serialize(Archive);
	FLevelSnapshotsModule::GetInternalModuleInstance().OnPostLoadSnapshotObject({ InObjectToRestore, InSharedData });
}

UObject* FLoadSnapshotObjectArchive::ResolveObjectDependency(int32 ObjectIndex) const
{
	FString LocalizationNamespace;
#if USE_STABLE_LOCALIZATION_KEYS
	LocalizationNamespace = GetLocalizationNamespace();
#endif

	ProcessObjectDependency(ObjectIndex);
	return SnapshotUtil::Object::ResolveObjectDependencyForSnapshotWorld(GetSharedData(), ObjectIndex, ProcessObjectDependency, LocalizationNamespace);
}

FLoadSnapshotObjectArchive::FLoadSnapshotObjectArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InSerializedObject, FProcessObjectDependency ProcessObjectDependency)
	:
	Super(InObjectData, InSharedData, true, InSerializedObject),
	ProcessObjectDependency(ProcessObjectDependency)
{}