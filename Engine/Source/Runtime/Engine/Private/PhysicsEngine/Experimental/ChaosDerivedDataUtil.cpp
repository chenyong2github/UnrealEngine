// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ChaosDerivedDataUtil.h"
#include "CoreMinimal.h"

#if INCLUDE_CHAOS
#include "ChaosLog.h"

namespace Chaos
{

	void CleanTrimesh(TArray<FVector>& InOutVertices, TArray<int32>& InOutIndices, TArray<int32>* OutOptFaceRemap)
	{
		TArray<FVector> LocalSourceVerts = InOutVertices;
		TArray<int32> LocalSourceIndices = InOutIndices;
		
		const int32 NumSourceVerts = LocalSourceVerts.Num();
		const int32 NumSourceTriangles = LocalSourceIndices.Num() / 3;

		if(NumSourceVerts == 0 || (LocalSourceIndices.Num() % 3) != 0)
		{
			// No valid geometry passed in
			return;
		}

		// New condensed list of unique verts from the trimesh
		TArray<FVector> LocalUniqueVerts;
		// New condensed list of indices after cleaning
		TArray<int32> LocalUniqueIndices;
		// Array mapping unique vertex index back to source data index
		TArray<int32> LocalUniqueToSourceIndices;
		// Remapping table from source index to unique index
		TArray<int32> LocalVertexRemap;
		// Remapping table from source triangle to unique triangle
		TArray<int32> LocalTriangleRemap;

		LocalUniqueVerts.Reserve(NumSourceVerts);
		LocalVertexRemap.AddUninitialized(NumSourceVerts);
		LocalTriangleRemap.AddUninitialized(NumSourceTriangles);

		auto ValidateTrianglesPre = [&InOutVertices](int32 A, int32 B, int32 C) -> bool
		{
			const FVector v0 = InOutVertices[A];
			const FVector v1 = InOutVertices[B];
			const FVector v2 = InOutVertices[C];
			return v0 != v1 && v0 != v2 && v1 != v2;
		};

		int32 NumBadTris = 0;
		for(int32 SrcTriIndex = 0; SrcTriIndex < NumSourceTriangles; ++SrcTriIndex)
		{
			const int32 A = InOutIndices[SrcTriIndex * 3];
			const int32 B = InOutIndices[SrcTriIndex * 3 + 1];
			const int32 C = InOutIndices[SrcTriIndex * 3 + 2];

			if(!ValidateTrianglesPre(A, B, C))
			{
				++NumBadTris;
			}
		}
		UE_CLOG(NumBadTris > 0, LogChaos, Warning, TEXT("Input trimesh contains %d bad triangles."), NumBadTris);

		float WeldThresholdSq = 0.0f;// SMALL_NUMBER * SMALL_NUMBER;

		for(int32 SourceVertIndex = 0; SourceVertIndex < NumSourceVerts; ++SourceVertIndex)
		{
			const FVector& SourceVert = LocalSourceVerts[SourceVertIndex];

			bool bUnique = true; // assume the vertex is unique until we find otherwise
			int32 RemapIndex = INDEX_NONE; // if the vertex isn't unique this will be set

			const int32 NumUniqueVerts = LocalUniqueVerts.Num();
			for(int32 UniqueVertIndex = 0; UniqueVertIndex < NumUniqueVerts; ++UniqueVertIndex)
			{
				const FVector& UniqueVert = LocalUniqueVerts[UniqueVertIndex];

				if((UniqueVert - SourceVert).SizeSquared() <= WeldThresholdSq)
				{
					//This vertex isn't unique and needs to be merged with the unique one we found
					bUnique = false;
					RemapIndex = UniqueVertIndex;

					// Done searching
					break;
				}
			}

			if(bUnique)
			{
				LocalUniqueVerts.Add(SourceVert);
				LocalUniqueToSourceIndices.Add(SourceVertIndex);
				LocalVertexRemap[SourceVertIndex] = LocalUniqueVerts.Num() - 1;
			}
			else
			{
				LocalVertexRemap[SourceVertIndex] = RemapIndex;
			}
		}

		// Build the new index buffer, removing now invalid merged triangles
		auto ValidateTriangleIndices = [](int32 A, int32 B, int32 C) -> bool
		{
			return A != B && A != C && B != C;
		};

		auto ValidateTriangleArea = [](const FVector& A, const FVector& B, const FVector& C)
		{
			const float AreaSq = FVector::CrossProduct(A - B, A - C).SizeSquared();

			return AreaSq > SMALL_NUMBER;
		};

		int32 NumDiscardedTriangles_Welded = 0;
		int32 NumDiscardedTriangles_Area = 0;

		for(int32 OriginalTriIndex = 0; OriginalTriIndex < NumSourceTriangles; ++OriginalTriIndex)
		{
			const int32 OrigAIndex = LocalSourceIndices[OriginalTriIndex * 3];
			const int32 OrigBIndex = LocalSourceIndices[OriginalTriIndex * 3 + 1];
			const int32 OrigCIndex = LocalSourceIndices[OriginalTriIndex * 3 + 2];

			const int32 RemappedAIndex = LocalVertexRemap[OrigAIndex];
			const int32 RemappedBIndex = LocalVertexRemap[OrigBIndex];
			const int32 RemappedCIndex = LocalVertexRemap[OrigCIndex];

			const FVector& A = LocalUniqueVerts[RemappedAIndex];
			const FVector& B = LocalUniqueVerts[RemappedBIndex];
			const FVector& C = LocalUniqueVerts[RemappedCIndex];

			// Only consider triangles that are actually valid for collision
			// #BG Consider being able to fix small triangles by collapsing them if we hit this a lot
			const bool bValidIndices = ValidateTriangleIndices(RemappedAIndex, RemappedBIndex, RemappedCIndex);
			const bool bValidArea = ValidateTriangleArea(A, B, C);
			if(bValidIndices && bValidArea)
			{
				LocalUniqueIndices.Add(RemappedAIndex);
				LocalUniqueIndices.Add(RemappedBIndex);
				LocalUniqueIndices.Add(RemappedCIndex);
				LocalTriangleRemap.Add(OriginalTriIndex);
			}
			else
			{
				if(!bValidIndices)
				{
					++NumDiscardedTriangles_Welded;
				}
				else if(!bValidArea)
				{
					++NumDiscardedTriangles_Area;
				}
			}
		}

		UE_CLOG(NumDiscardedTriangles_Welded > 0, LogChaos, Warning, TEXT("Discarded %d welded triangles when cooking trimesh."), NumDiscardedTriangles_Welded);
		UE_CLOG(NumDiscardedTriangles_Area > 0, LogChaos, Warning, TEXT("Discarded %d small triangles when cooking trimesh."), NumDiscardedTriangles_Area);

		InOutVertices = LocalUniqueVerts;
		InOutIndices = LocalUniqueIndices;

		if(OutOptFaceRemap)
		{
			*OutOptFaceRemap = LocalTriangleRemap;
		}
	}

}

#endif // INCLUDE_CHAOS