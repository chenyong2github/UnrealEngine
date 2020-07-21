// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/MeshConvexHull.h"
#include "Solvers/MeshLinearization.h"
#include "MeshSimplification.h"
#include "MeshNormals.h"
#include "ConvexHull3.h"



bool FMeshConvexHull::Compute()
{
	TArray<int32> ToLinear, FromLinear;
	ToLinear.SetNum(Mesh->MaxVertexID());
	FromLinear.SetNum(Mesh->VertexCount());
	int32 LinearIndex = 0;
	for (int32 vid : Mesh->VertexIndicesItr())
	{
		FromLinear[LinearIndex] = vid;
		ToLinear[vid] = LinearIndex++;
	}

	FConvexHull3d HullCompute;
	bool bOK = HullCompute.Solve(FromLinear.Num(),
		[&](int32 Index) { return Mesh->GetVertex(FromLinear[Index]); },
		bUseExactComputation);
	if (!bOK)
	{
		return false;
	}

	TMap<int32, int32> HullVertMap;

	ConvexHull = FDynamicMesh3(EMeshComponents::None);
	HullCompute.GetTriangles([&](FIndex3i Triangle)
	{
		for (int32 j = 0; j < 3; ++j)
		{
			int32 Index = Triangle[j];
			if (HullVertMap.Contains(Index) == false)
			{
				FVector3d OrigPos = Mesh->GetVertex(FromLinear[Index]);
				int32 NewVID = ConvexHull.AppendVertex(OrigPos);
				HullVertMap.Add(Index, NewVID);
				Triangle[j] = NewVID;
			}
			else
			{
				Triangle[j] = HullVertMap[Index];
			}
		}

		Swap(Triangle.B, Triangle.C);		// GTEngine mesh is other-handed
		ConvexHull.AppendTriangle(Triangle);
	});

	if (bPostSimplify)
	{
		bool bSimplified = false;
		if (ConvexHull.TriangleCount() > MaxTargetFaceCount)
		{
			FVolPresMeshSimplification Simplifier(&ConvexHull);
			Simplifier.CollapseMode = FVolPresMeshSimplification::ESimplificationCollapseModes::MinimalExistingVertexError;
			Simplifier.SimplifyToTriangleCount(MaxTargetFaceCount);
			bSimplified = true;
		}


		if (bSimplified)
		{
			// recalculate convex hull
			// TODO: test if simplified mesh is convex first, can just re-use in that case!!
			FMeshConvexHull SimplifiedHull(&ConvexHull);
			if (SimplifiedHull.Compute())
			{
				ConvexHull = MoveTemp(SimplifiedHull.ConvexHull);
			}
		}

	}


	return true;

}