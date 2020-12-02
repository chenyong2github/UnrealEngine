// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Elements/Framework/TypedElementHandle.h"

class UTypedElementList;

namespace TypedElementUtil
{

/**
 * Batch the given elements by their type.
 */
TYPEDELEMENTFRAMEWORK_API void BatchElementsByType(const UTypedElementList* InElementsToBatch, TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>>& OutElementsByType);
TYPEDELEMENTFRAMEWORK_API void BatchElementsByType(TArrayView<const FTypedElementHandle> InElementsToBatch, TMap<FTypedHandleTypeId, TArray<FTypedElementHandle>>& OutElementsByType);

} // namespace TypedElementUtil
