// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "VisualGraph.h"

class VISUALGRAPHUTILS_API FVisualGraphObjectUtils
{
public:

	static FVisualGraph TraverseUObjectReferences(
		const TArray<UObject*>& InObjects,
		const TArray<UClass*>& InClassesToSkip = TArray<UClass*>(),
		const TArray<UObject*>& InOutersToSkip = TArray<UObject*>(),
		const TArray<UObject*>& InOutersToUse = TArray<UObject*>(),
		bool bTraverseObjectsInOuter = true,
		bool bCollectReferencesBySerialize = true,
		bool bRecursive = true
		);

	static FVisualGraph TraverseTickOrder(
		const TArray<UObject*>& InObjects
		);
};

