// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Algo/KahnTopologicalSort.h"

namespace Algo
{
	template <typename RangeType, typename GetElementDependenciesType>
	FORCEINLINE bool TopologicalSort(RangeType& UniqueRange, GetElementDependenciesType GetElementDependencies)
	{
		return KahnTopologicalSort(UniqueRange, GetElementDependencies);
	}
}