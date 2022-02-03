// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Chaos/Array.h"
#include "Chaos/DynamicParticles.h"
#include "Chaos/Core.h"
#include "Chaos/Vector.h"

namespace Chaos
{

class FGraphColoring
{
	typedef TArray<int32, TInlineAllocator<8>> FColorSet;
	
	struct FGraphNode
	{
		FGraphNode()
			: NextColor(0)
		{
		}

		TArray<int32, TInlineAllocator<8>> Edges;
		int32 NextColor;
		FColorSet UsedColors;
	};

	struct FGraphEdge
	{
		FGraphEdge()
			: FirstNode(INDEX_NONE)
			, SecondNode(INDEX_NONE)
			, Color(INDEX_NONE)
		{
		}

		int32 FirstNode;
		int32 SecondNode;
		int32 Color;
	};

	struct FGraph3dEdge : FGraphEdge
	{
		FGraph3dEdge()
			: ThirdNode(INDEX_NONE)
		{
		}

		int32 ThirdNode;
	};

  public:
	template<typename T>
	CHAOS_API static TArray<TArray<int32>> ComputeGraphColoring(const TArray<TVector<int32, 2>>& Graph, const TDynamicParticles<T, 3>& InParticles);
	template<typename T>
	CHAOS_API static TArray<TArray<int32>> ComputeGraphColoring(const TArray<TVector<int32, 3>>& Graph, const TDynamicParticles<T, 3>& InParticles);
};

}
