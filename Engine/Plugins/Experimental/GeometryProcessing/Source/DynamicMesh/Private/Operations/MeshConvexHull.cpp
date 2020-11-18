// Copyright Epic Games, Inc. All Rights Reserved.

#include "Operations/MeshConvexHull.h"
#include "Solvers/MeshLinearization.h"
#include "MeshSimplification.h"
#include "MeshNormals.h"
#include "ConvexHull3.h"


bool FMeshConvexHull::Compute()
{
	bool bOK = false;
	if (VertexSet.Num() > 0)
	{
		bOK = Compute_VertexSubset();
	}
	else
	{
		bOK = Compute_FullMesh();
	}
	if (!bOK)
	{
		return false;
	}


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

	return bOK;
}





bool FMeshConvexHull::Compute_FullMesh()
{
	FConvexHull3d HullCompute;
	bool bOK = HullCompute.Solve(Mesh->MaxVertexID(),
		[this](int32 Index) { return Mesh->GetVertex(Index); },
		[this](int32 Index) { return Mesh->IsVertex(Index); });
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
				FVector3d OrigPos = Mesh->GetVertex(Index);
				int32 NewVID = ConvexHull.AppendVertex(OrigPos);
				HullVertMap.Add(Index, NewVID);
				Triangle[j] = NewVID;
			}
			else
			{
				Triangle[j] = HullVertMap[Index];
			}
		}

		ConvexHull.AppendTriangle(Triangle);
	});

	return true;
}



bool FMeshConvexHull::Compute_VertexSubset()
{
	FConvexHull3d HullCompute;
	bool bOK = HullCompute.Solve(VertexSet.Num(),
		[this](int32 Index) { return Mesh->GetVertex(VertexSet[Index]); });
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
				FVector3d OrigPos = Mesh->GetVertex(VertexSet[Index]);
				int32 NewVID = ConvexHull.AppendVertex(OrigPos);
				HullVertMap.Add(Index, NewVID);
				Triangle[j] = NewVID;
			}
			else
			{
				Triangle[j] = HullVertMap[Index];
			}
		}

		ConvexHull.AppendTriangle(Triangle);
	});

	return true;
}