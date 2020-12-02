// Copyright Epic Games, Inc. All Rights Reserved.

#include "Elements/Framework/TypedElementUtil.h"
#include "Elements/Framework/TypedElementList.h"

namespace TypedElementUtil
{

void BatchElementsByType(const UTypedElementList* InElementsToBatch, TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>>& OutElementsByType)
{
	OutElementsByType.Reset();
	InElementsToBatch->ForEachElementHandle([&OutElementsByType](const FTypedElementHandle& InElementHandle)
	{
		TArray<FTypedElementHandle>& ElementsForType = OutElementsByType.FindOrAdd(InElementHandle.GetId().GetTypeId());
		ElementsForType.Add(InElementHandle);
		return true;
	});
}

void BatchElementsByType(TArrayView<const FTypedElementHandle> InElementsToBatch, TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>>& OutElementsByType)
{
	OutElementsByType.Reset();
	for (const FTypedElementHandle& ElementHandle : InElementsToBatch)
	{
		TArray<FTypedElementHandle>& ElementsForType = OutElementsByType.FindOrAdd(ElementHandle.GetId().GetTypeId());
		ElementsForType.Add(ElementHandle);
	}
}

} // namespace TypedElementUtil
