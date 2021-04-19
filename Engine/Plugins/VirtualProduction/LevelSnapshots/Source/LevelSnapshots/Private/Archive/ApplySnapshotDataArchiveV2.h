// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySelection.h"
#include "SnapshotArchive.h"

class ULevelSnapshotSelectionSet;

/* For writing data into an object */
class FApplySnapshotDataArchiveV2 : public FSnapshotArchive
{
	using Super = FSnapshotArchive;
public:
	
	static void ApplyToExistingWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject, UObject* InDeserializedVersion, const FPropertySelection& InSelectionSet);
	static void ApplyToRecreatedWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject, UObject* InDeserializedVersion);
	
	//~ Begin FTakeSnapshotArchiveV2 Interface
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	virtual void PushSerializedProperty(FProperty* InProperty, const bool bIsEditorOnlyProperty) override;
	virtual void PopSerializedProperty(FProperty* InProperty, const bool bIsEditorOnlyProperty) override;
	//~ End FTakeSnapshotArchiveV2 Interface

private:

	FApplySnapshotDataArchiveV2(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject, const FPropertySelection& InSelectionSet);
	FApplySnapshotDataArchiveV2(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject);
	
	/* Immutable list of properties we are supposed to serialise */
	TOptional<const FPropertySelection*> SelectionSet;
	/* A property is removed when we serialise it.
	 * After serialisation is done, this list tells us which properties are still left to serialise.
	 */
	mutable FPropertySelection PropertiesLeftToSerialize;
	
	/* Object we are serializing into.
	 * Needed for Pre and Post Edit change
	 */
	UObject* OriginalObject;
	
};
