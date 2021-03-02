// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementData.h"
#include "Elements/SMInstance/SMInstanceElementId.h"

struct FTypedElementHandle;

/**
 * Element data that represents a specific instance within an ISM.
 */
struct ENGINE_API FSMInstanceElementData
{
	UE_DECLARE_TYPED_ELEMENT_DATA_RTTI(FSMInstanceElementData);

	FSMInstanceElementId InstanceElementId;
};

namespace SMInstanceElementDataUtil
{

/**
 * Test whether static mesh instance elements are currently enabled?
 * @note Controlled by the CVar: "TypedElements.EnableSMInstanceElements".
 */
ENGINE_API bool SMInstanceElementsEnabled();

/**
 * Test whether the given ISM component is valid to be used with static mesh instance elements.
 */
ENGINE_API bool IsValidComponentForSMInstanceElements(const UInstancedStaticMeshComponent* InComponent);

/**
 * Attempt to get the static mesh instance ID from the given element handle.
 * @note This is not typically something you'd want to use outside of data access within an interface implementation.
 * @return The static mesh instance ID if the element handle contains FSMInstanceElementData, otherwise an invalid ID.
 */
ENGINE_API FSMInstanceId GetSMInstanceFromHandle(const FTypedElementHandle& InHandle, const bool bSilent = false);

/**
 * Attempt to get the static mesh instance ID from the given element handle, asserting if the element handle doesn't contain FSMInstanceElementData.
 * @note This is not typically something you'd want to use outside of data access within an interface implementation.
 * @return The static mesh instance ID.
 */
ENGINE_API FSMInstanceId GetSMInstanceFromHandleChecked(const FTypedElementHandle& InHandle);

/**
 * Attempt to get the static mesh instance IDs from the given element handles.
 * @note This is not typically something you'd want to use outside of data access within an interface implementation.
 * @return The static mesh instance IDs of any element handles that contain FSMInstanceElementData, skipping any that don't.
 */
template <typename ElementHandleType>
TArray<FSMInstanceId> GetSMInstancesFromHandles(TArrayView<const ElementHandleType> InHandles, const bool bSilent = false)
{
	TArray<FSMInstanceId> SMInstanceIds;
	SMInstanceIds.Reserve(InHandles.Num());

	for (const FTypedElementHandle& Handle : InHandles)
	{
		if (FSMInstanceId SMInstanceId = GetSMInstanceFromHandle(Handle, bSilent))
		{
			SMInstanceIds.Add(SMInstanceId);
		}
	}

	return SMInstanceIds;
}

/**
 * Attempt to get the static mesh instance IDs from the given element handles, asserting if any element handle doesn't contain FSMInstanceElementData.
 * @note This is not typically something you'd want to use outside of data access within an interface implementation.
 * @return The static mesh instance IDs.
 */
template <typename ElementHandleType>
TArray<FSMInstanceId> GetSMInstancesFromHandlesChecked(TArrayView<const ElementHandleType> InHandles)
{
	TArray<FSMInstanceId> SMInstanceIds;
	SMInstanceIds.Reserve(InHandles.Num());

	for (const FTypedElementHandle& Handle : InHandles)
	{
		SMInstanceIds.Add(GetSMInstanceFromHandleChecked(Handle));
	}

	return SMInstanceIds;
}

} // namespace SMInstanceElementDataUtil
