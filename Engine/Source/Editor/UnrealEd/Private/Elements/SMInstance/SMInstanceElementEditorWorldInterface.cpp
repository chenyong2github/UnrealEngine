// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/SMInstance/SMInstanceElementEditorWorldInterface.h"
#include "Elements/SMInstance/SMInstanceElementData.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "Elements/Framework/EngineElementsLibrary.h"
#include "Elements/Framework/TypedElementSelectionSet.h"

bool USMInstanceElementEditorWorldInterface::CanDeleteElement(const FTypedElementHandle& InElementHandle)
{
	const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle);
	return SMInstance && CanEditSMInstance(SMInstance);
}

bool USMInstanceElementEditorWorldInterface::DeleteElement(const FTypedElementHandle& InElementHandle, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	if (const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle))
	{
		if (CanEditSMInstance(SMInstance))
		{
			SMInstance.ISMComponent->Modify();
			InSelectionSet->DeselectElement(InElementHandle, FTypedElementSelectionOptions());
			return SMInstance.ISMComponent->RemoveInstance(SMInstance.InstanceIndex);
		}
	}

	return false;
}

bool USMInstanceElementEditorWorldInterface::DeleteElements(TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, UTypedElementSelectionSet* InSelectionSet, const FTypedElementDeletionOptions& InDeletionOptions)
{
	const TArray<FSMInstanceId> SMInstancesToDelete = SMInstanceElementDataUtil::GetSMInstancesFromHandles(InElementHandles);

	if (SMInstancesToDelete.Num() > 0)
	{
		// Batch by the ISM component
		TMap<UInstancedStaticMeshComponent*, TArray<int32>> BatchedInstancesToDelete;
		for (const FSMInstanceId& SMInstance : SMInstancesToDelete)
		{
			if (CanEditSMInstance(SMInstance))
			{
				TArray<int32>& InstanceIndices = BatchedInstancesToDelete.FindOrAdd(SMInstance.ISMComponent);
				InstanceIndices.Add(SMInstance.InstanceIndex);
			}
		}

		bool bDidDelete = false;

		FTypedElementListLegacySyncScopedBatch LegacySyncBatch(InSelectionSet->GetElementList());
		for (TTuple<UInstancedStaticMeshComponent*, TArray<int32>>& BatchedInstancesToDeletePair : BatchedInstancesToDelete)
		{
			BatchedInstancesToDeletePair.Key->Modify();

			// Sort the instance indices in descending order to avoid invalidating resolved indices mid-delete
			BatchedInstancesToDeletePair.Value.Sort(TGreater<int32>());
			for (const int32 InstanceIndex : BatchedInstancesToDeletePair.Value)
			{
				InSelectionSet->DeselectElement(UEngineElementsLibrary::AcquireEditorSMInstanceElementHandle(FSMInstanceId{ BatchedInstancesToDeletePair.Key, InstanceIndex }), FTypedElementSelectionOptions());
				bDidDelete |= BatchedInstancesToDeletePair.Key->RemoveInstance(InstanceIndex);
			}
		}

		return bDidDelete;
	}

	return false;
}

FTypedElementHandle USMInstanceElementEditorWorldInterface::DuplicateElement(const FTypedElementHandle& InElementHandle, UWorld* InWorld, bool bOffsetLocations)
{
	if (const FSMInstanceId SMInstance = SMInstanceElementDataUtil::GetSMInstanceFromHandle(InElementHandle))
	{
		if (CanEditSMInstance(SMInstance))
		{
			FTransform NewInstanceTransform = FTransform::Identity;
			SMInstance.ISMComponent->GetInstanceTransform(SMInstance.InstanceIndex, NewInstanceTransform);

			const int32 NewInstanceIndex = SMInstance.ISMComponent->AddInstance(NewInstanceTransform);
			return UEngineElementsLibrary::AcquireEditorSMInstanceElementHandle(FSMInstanceId{ SMInstance.ISMComponent, NewInstanceIndex });
		}
	}

	return FTypedElementHandle();
}

void USMInstanceElementEditorWorldInterface::DuplicateElements(TArrayView<const FTypedElementHandle> InElementHandles, UWorld* InWorld, bool bOffsetLocations, TArray<FTypedElementHandle>& OutNewElements)
{
	const TArray<FSMInstanceId> SMInstancesToDuplicate = SMInstanceElementDataUtil::GetSMInstancesFromHandles(InElementHandles);

	if (SMInstancesToDuplicate.Num() > 0)
	{
		// Batch by the ISM component
		TMap<UInstancedStaticMeshComponent*, TArray<int32>> BatchedInstancesToDuplicate;
		for (const FSMInstanceId& SMInstance : SMInstancesToDuplicate)
		{
			if (CanEditSMInstance(SMInstance))
			{
				TArray<int32>& InstanceIndices = BatchedInstancesToDuplicate.FindOrAdd(SMInstance.ISMComponent);
				InstanceIndices.Add(SMInstance.InstanceIndex);
			}
		}

		for (const TTuple<UInstancedStaticMeshComponent*, TArray<int32>>& BatchedInstancesToDuplicatePair : BatchedInstancesToDuplicate)
		{
			TArray<FTransform> NewInstanceTransforms;
			NewInstanceTransforms.Reserve(BatchedInstancesToDuplicatePair.Value.Num());
			for (const int32 InstanceIndex : BatchedInstancesToDuplicatePair.Value)
			{
				FTransform& NewInstanceTransform = NewInstanceTransforms.Add_GetRef(FTransform::Identity);
				BatchedInstancesToDuplicatePair.Key->GetInstanceTransform(InstanceIndex, NewInstanceTransform);
			}

			const TArray<int32> NewInstanceIndices = BatchedInstancesToDuplicatePair.Key->AddInstances(NewInstanceTransforms, /*bShouldReturnIndices*/true);
			OutNewElements.Reserve(OutNewElements.Num() + NewInstanceIndices.Num());
			for (const int32 NewInstanceIndex : NewInstanceIndices)
			{
				OutNewElements.Add(UEngineElementsLibrary::AcquireEditorSMInstanceElementHandle(FSMInstanceId{ BatchedInstancesToDuplicatePair.Key, NewInstanceIndex }));
			}
		}
	}
}
