// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ObjectReferenceSnapshotMetaData.generated.h"

UENUM()
enum class EObjectReferenceSnapshotFlags : uint8
{
	None = 0x00000000,

	/**
	 * When this reference was saved, it was discovered FTakeClassDefaultObjectSnapshotArchive and marked to be skipped.
	 * 
	 * The following references are skipped when saving an archetype / CDO:
	 *	- references to default objects, e.g. if the object RF_ClassDefaultObject | RF_ArchetypeObject flags or the reference points to the serialized archetype.
	 *	- references to default subobjects, e.g. if the object has the RF_DefaultSubObject flag or is in archetype.
	 */
	SkipWhenSerializingArchetypeData = 0x00000001
};

ENUM_CLASS_FLAGS(EObjectReferenceSnapshotFlags)

/** Holds additional information about a saved object reference (see FWorldSnapshotData::SerializedReferences) */
USTRUCT()
struct LEVELSNAPSHOTS_API FObjectReferenceSnapshotMetaData
{
	GENERATED_BODY()

	UPROPERTY()
	EObjectReferenceSnapshotFlags Flags;
};