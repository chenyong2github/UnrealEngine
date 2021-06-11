// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySelection.h"
#include "PropertySelectionMap.h"
#include "SnapshotArchive.h"

/* For writing data into an editor object. */
class FApplySnapshotDataArchiveV2 : public FSnapshotArchive
{
	using Super = FSnapshotArchive;
public:
	
	static void ApplyToExistingEditorWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject, UObject* InDeserializedVersion, const FPropertySelectionMap& InSelectionMapForResolvingSubobjects, const FPropertySelection& InSelectionSet);
	static void ApplyToRecreatedEditorWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject, UObject* InDeserializedVersion, const FPropertySelectionMap& InSelectionMapForResolvingSubobjects);
	
	//~ Begin FTakeSnapshotArchiveV2 Interface
	virtual bool ShouldSkipProperty(const FProperty* InProperty) const override;
	virtual void PushSerializedProperty(FProperty* InProperty, const bool bIsEditorOnlyProperty) override;
	//~ End FTakeSnapshotArchiveV2 Interface

protected:

	//~ Begin FSnapshotArchive Interface
	virtual UObject* ResolveObjectDependency(int32 ObjectIndex) const override;
	//~ End FSnapshotArchive Interface

private:

	FApplySnapshotDataArchiveV2(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject, const FPropertySelectionMap& InSelectionMapForResolvingSubobjects, TOptional<const FPropertySelection*> InSelectionSet);

	bool ShouldSerializeAllProperties() const;

	/* Needed so subobjects can be fully reconstructed */
	const FPropertySelectionMap& SelectionMapForResolvingSubobjects; 
	
	/* Immutable list of properties we are supposed to serialise */
	TOptional<const FPropertySelection*> SelectionSet;
	/* A property is removed when we serialise it.
	 * After serialisation is done, this list tells us which properties are still left to serialise.
	 */
	mutable FPropertySelection PropertiesLeftToSerialize;
	
	
};
