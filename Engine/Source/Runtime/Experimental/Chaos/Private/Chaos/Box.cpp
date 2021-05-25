// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Box.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
	FConvexHalfEdgeStructureDataS16 MakeBoxStructureData()
	{
		TArray<TArray<int32>> PlaneVertices
		{
			{6, 7, 3, 2},	// X
			{1, 5, 6, 2},	// Y
			{7, 6, 5, 4},	// Z,
			{1, 0, 4, 5},	// -X
			{0, 3, 7, 4},	// -Y
			{0, 1, 2, 3},	// -Z,
		};

		return FConvexHalfEdgeStructureDataS16::MakePlaneVertices(PlaneVertices, 8);
	}

	template<typename T, int D> TArray<FVec3> TBox<T, D>::SNormals =
	{
		FVec3(1,0,0),	// X
		FVec3(0,1,0),	// Y
		FVec3(0,0,1),	// Z
		FVec3(-1,0,0),	// -X
		FVec3(0,-1,0),	// -Y
		FVec3(0,0,-1),	// -Z
	};

	template<typename T, int D> TArray<FVec3> TBox<T, D>::SVertices =
	{
		FVec3(-1,-1,-1),
		FVec3(-1,1,-1),
		FVec3(1,1,-1),
		FVec3(1,-1,-1),
		FVec3(-1,-1,1),
		FVec3(-1,1,1),
		FVec3(1,1,1),
		FVec3(1,-1,1),
	};

	template<typename T, int D> FConvexHalfEdgeStructureDataS16 TBox<T, D>::SStructureData = MakeBoxStructureData();

	template class TBox<FReal, 3>;
}