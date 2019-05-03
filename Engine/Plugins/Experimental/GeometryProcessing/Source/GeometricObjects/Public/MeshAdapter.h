// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TriangleTypes.h"
#include "VectorTypes.h"

#include "Math/IntVector.h"
#include "Math/Vector.h"

template <typename RealType>
struct TTriangleMeshAdapter
{
	TFunction<bool(int32 index)> IsTriangle;
	TFunction<bool(int32 index)> IsVertex;
	TFunction<int32()> MaxTriangleID;
	TFunction<int32()> MaxVertexID;
	TFunction<int32()> TriangleCount;
	TFunction<int32()> VertexCount;
	TFunction<int32()> GetShapeTimestamp;
	TFunction<FIndex3i(int32)> GetTriangle;
	TFunction<FVector3<RealType>(int32)> GetVertex;

	inline void GetTriVertices(int TID, FVector3<RealType>& V0, FVector3<RealType>& V1, FVector3<RealType>& V2) const
	{
		FIndex3i TriIndices = GetTriangle(TID);
		V0 = GetVertex(TriIndices.A);
		V1 = GetVertex(TriIndices.B);
		V2 = GetVertex(TriIndices.C);
	}
};

typedef TTriangleMeshAdapter<double> TTriangleMeshAdapterd;
typedef TTriangleMeshAdapter<float> TTriangleMeshAdapterf;

TTriangleMeshAdapterd GetArrayMesh(TArray<FVector>& Vertices, TArray<FIntVector>& Triangles)
{
	return {
		[&](int) { return true; },
		[&](int) { return true; },
		[&]() { return Triangles.Num(); },
		[&]() { return Vertices.Num(); },
		[&]() { return Triangles.Num(); },
		[&]() { return Vertices.Num(); },
		[&]() { return 0; },
		[&](int Idx) { return FIndex3i(Triangles[Idx]); },
		[&](int Idx) { return FVector3d(Vertices[Idx]); }};
}
