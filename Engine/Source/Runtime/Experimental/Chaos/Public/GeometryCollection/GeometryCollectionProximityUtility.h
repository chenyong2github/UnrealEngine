// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
//#include "GeometryCollection/ManagedArrayCollection.h"

class FGeometryCollection;



class CHAOS_API FGeometryCollectionProximityUtility
{
public:

	struct FFaceTransformData {
		int32 FaceIdx;
		int32 TransformIndex;
		FBox Bounds;
	};

	struct FVertexPair
	{
		FVector Vertex1, Vertex2;
		float Distance() { return (Vertex1 - Vertex2).Size(); }
		float DistanceSquared() { return (Vertex1 - Vertex2).SizeSquared(); }
	};

	struct FOverlappingFacePair
	{
		int32 FaceIdx1;
		int32 FaceIdx2;

		friend inline uint32 GetTypeHash(const FOverlappingFacePair& Other)
		{
			return HashCombine(GetTypeHash(Other.FaceIdx1), GetTypeHash(Other.FaceIdx2));
		}

		friend bool operator==(const FOverlappingFacePair& A, const FOverlappingFacePair& B)
		{
			return A.FaceIdx1 == B.FaceIdx1 && A.FaceIdx2 == B.FaceIdx2;
		}
	};

	struct FOverlappingFacePairTransformIndex
	{
		int32 TransformIdx1;
		int32 TransformIdx2;

		friend inline uint32 GetTypeHash(const FOverlappingFacePairTransformIndex& Other)
		{
			return HashCombine(GetTypeHash(Other.TransformIdx1), GetTypeHash(Other.TransformIdx2));
		}

		friend bool operator==(const FOverlappingFacePairTransformIndex& A, const FOverlappingFacePairTransformIndex& B)
		{
			return A.TransformIdx1 == B.TransformIdx1 && A.TransformIdx2 == B.TransformIdx2;
		}
	};

	struct FFaceEdge
	{
		int32 VertexIdx1;
		int32 VertexIdx2;

		friend inline uint32 GetTypeHash(const FFaceEdge& Other)
		{
			return HashCombine(GetTypeHash(Other.VertexIdx1), GetTypeHash(Other.VertexIdx2));
		}

		friend bool operator==(const FFaceEdge& A, const FFaceEdge& B)
		{
			return A.VertexIdx1 == B.VertexIdx1 && A.VertexIdx2 == B.VertexIdx2;
		}
	};

	//
	// Builds the connectivity data in the GeometryGroup (Proximity array)
	// and builds all the data in the BreakingGroup
	static void UpdateProximity(FGeometryCollection* GeometryCollection);

	static bool IsPointInsideOfTriangle(const FVector& P, const FVector& Vertex0, const FVector& Vertex1, const FVector& Vertex2, float Threshold);

	// For any transform index, get the connected indices at the operating level. If OperatingLevel is -1, the operating level will be determined by the level of the Indexed node.
	static void LevelConnectivity(FGeometryCollection* GeometryCollection, int32 Index, TSet<int32>& OutConnections, int32 OperatingLevel = -1);

private:
};
