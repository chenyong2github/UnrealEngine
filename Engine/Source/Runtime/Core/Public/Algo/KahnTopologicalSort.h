// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Traits/ElementType.h"
#include "Containers/Array.h"
#include "Containers/Map.h"

namespace Algo
{
	template <typename RangeType, typename GetElementDependenciesType>
	bool KahnTopologicalSort(RangeType& UniqueRange, GetElementDependenciesType GetElementDependencies)
	{
		using ElementType = typename TElementType<RangeType>::Type;
		using ElementMultiMap = TMultiMap<ElementType, ElementType>;

		ElementMultiMap Dependencies;
		for (const ElementType& Element : UniqueRange)
		{
			for (const ElementType& Dependency : GetElementDependencies(Element))
			{
				Dependencies.Add(Element, Dependency);
			}
		}

		TArray<ElementType> SortedRange;
		TArray<ElementType> IndependentElements;
		TArray<ElementType> UniqueElements;
		UniqueElements.Reserve(GetNum(UniqueRange));
		for (const ElementType& Element : UniqueRange)
		{
			UniqueElements.Add(Element);
		}

		// Sort graph so that vertices with no dependencies always go first.
		while (UniqueElements.Num() > 0)
		{
			IndependentElements = UniqueElements.FilterByPredicate([&](const ElementType& InVertex) {
				return (Dependencies.Num(InVertex) == 0);
			});

			if (0 == IndependentElements.Num())
			{
				// If there are no independent vertices, then likely there is a 
				// cycle in the graph.
				return false;
			}

			ElementMultiMap UpdatedDependencies;

			// Remove independent vertices from dependency map.
			for (const auto& Element : Dependencies)
			{
				if (!IndependentElements.Contains(Element.Value))
				{
					UpdatedDependencies.AddUnique(Element.Key, Element.Value);
				}
			}
			Dependencies = MoveTemp(UpdatedDependencies);

			// Remove independent vertices from node list
			for (const ElementType& Vertex : IndependentElements)
			{
				UniqueElements.RemoveSwap(Vertex);
			}

			// Add independent vertices to output.
			SortedRange.Append(MoveTemp(IndependentElements));
		}

		UniqueRange = MoveTemp(SortedRange);
		return true;
	}
}