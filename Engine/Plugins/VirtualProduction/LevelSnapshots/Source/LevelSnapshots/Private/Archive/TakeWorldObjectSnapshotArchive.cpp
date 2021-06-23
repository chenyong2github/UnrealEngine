// Copyright Epic Games, Inc. All Rights Reserved.

#include "TakeWorldObjectSnapshotArchive.h"

#include "ObjectSnapshotData.h"
#include "WorldSnapshotData.h"

#include "Serialization/ArchiveSerializedPropertyChain.h"
#include "UObject/UnrealType.h"

FTakeWorldObjectSnapshotArchive FTakeWorldObjectSnapshotArchive::MakeArchiveForSavingWorldObject(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject)
{
	ensure(InOriginalObject);
	return FTakeWorldObjectSnapshotArchive(InObjectData, InSharedData, InOriginalObject);
}

FTakeWorldObjectSnapshotArchive::FTakeWorldObjectSnapshotArchive(FObjectSnapshotData& InObjectData, FWorldSnapshotData& InSharedData, UObject* InOriginalObject)
	:
	Super(InObjectData, InSharedData, false, InOriginalObject)
{}

bool FTakeWorldObjectSnapshotArchive::ShouldSkipProperty(const FProperty* InProperty) const
{
	const bool bSuperWantsToSkip = Super::ShouldSkipProperty(InProperty);

	if (!bSuperWantsToSkip)
	{
		const bool bIsRootProperty = [this]()
		{
			const FArchiveSerializedPropertyChain* PropertyChain = GetSerializedPropertyChain();
			return PropertyChain == nullptr || PropertyChain->GetNumProperties() == 0;
		}();
		if (!bIsRootProperty)
		{
			// We are within a struct property and we have already checked that the struct property does not have equal values
			// Simply allow all properties but only ones which are not deprecated.
			// TODO: We could save more disk space here by checking this property but let's test this first...
			return InProperty->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient);
		}

		// Always save object properties regardless whether different from CDO or not ... this makes restoring easier: see FApplySnapshotDataArchiveV2::ApplyToExistingWorldObject
		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(InProperty))
		{
			return false;
		}
		
		UObject* OriginalContainer = GetSerializedObject();
		UObject* ClassDefaultContainer = OriginalContainer->GetClass()->GetDefaultObject(); 
		for (int32 ArrayDim = 0; ArrayDim < InProperty->ArrayDim; ++ArrayDim)
		{
			if (!InProperty->Identical_InContainer(OriginalContainer, ClassDefaultContainer, ArrayDim))
			{
				return false; 
			}
		}
	}
	
	return true;
}
