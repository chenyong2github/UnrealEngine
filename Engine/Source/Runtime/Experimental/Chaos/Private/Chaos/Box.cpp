// Copyright Epic Games, Inc. All Rights Reserved.
#include "Chaos/Box.h"

//PRAGMA_DISABLE_OPTIMIZATION

namespace Chaos
{
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

	//TArray<TArray<int32>> MakeBoxPlaneVertices()
	//{
	//	TArray<TArray<int32>> PlaneVertices;
	//	PlaneVertices.SetNum(6);
	//	TArray<int32>({6. 7, 3, 2}),	// X
	//	TArray<int32>({1, 5, 6, 2}),	// Y
	//	TArray<int32>({7, 6, 5, 4}),	// Z,
	//	TArray<int32>({1, 0, 4, 5}),	// -X
	//	TArray<int32>({0, 4, 7, 3}),	// -Y
	//	TArray<int32>({0, 1, 2, 3}),	// -Z,
	//};


	template<typename T, int D> TArray<TArray<int32>> TBox<T, D>::SPlaneVertices
	{
		{6, 7, 3, 2},	// X
		{1, 5, 6, 2},	// Y
		{7, 6, 5, 4},	// Z,
		{1, 0, 4, 5},	// -X
		{0, 4, 7, 3},	// -Y
		{0, 1, 2, 3},	// -Z,
	};

	template<typename T, int D> TArray<TArray<int32>> TBox<T, D>::SVertexPlanes =
	{
		{3, 4, 5},
		{1, 3, 5},
		{0, 1, 5},
		{0, 4, 5},
		{2, 3, 4},
		{1, 2, 3},
		{0, 1, 2},
		{0, 2, 4},
	};
}