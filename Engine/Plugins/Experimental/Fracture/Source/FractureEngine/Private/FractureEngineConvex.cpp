// Copyright Epic Games, Inc. All Rights Reserved.

#include "FractureEngineConvex.h"
#include "Chaos/Convex.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "ProjectionTargets.h"
#include "MeshSimplification.h"
#include "GeometryCollection/GeometryCollection.h"
#include "GeometryCollection/GeometryCollectionConvexUtility.h"

namespace UE::FractureEngine::Convex
{
	bool SimplifyConvexHulls(FManagedArrayCollection& Collection, const FSimplifyHullSettings& Settings, bool bRestrictToSelection, const TArrayView<const int32> TransformSelection)
	{
		if (!FGeometryCollectionConvexUtility::HasConvexHullData(&Collection))
		{
			// nothing to simplify
			return false;
		}
		
		TManagedArray<TSet<int32>>& TransformToConvexInds = Collection.ModifyAttribute<TSet<int32>>("TransformToConvexIndices", FTransformCollection::TransformGroup);
		TManagedArray<TUniquePtr<::Chaos::FConvex>>& ConvexHulls = Collection.ModifyAttribute<TUniquePtr<::Chaos::FConvex>>("ConvexHull", "Convex");
		
		auto SimplifyBone = [&TransformToConvexInds, &ConvexHulls, &Settings](int32 BoneIdx) -> bool
		{
			if (BoneIdx < 0 || BoneIdx >= TransformToConvexInds.Num())
			{
				// invalid bone index
				return false;
			}
			bool bNoFailures = true;
			for (int32 ConvexIdx : TransformToConvexInds[BoneIdx])
			{
				bool bSuccess = SimplifyConvexHull(ConvexHulls[ConvexIdx].Get(), ConvexHulls[ConvexIdx].Get(), Settings);
				bNoFailures = bNoFailures && bSuccess;
			}
			return bNoFailures;
		};

		bool bNoFailures = true;

		if (bRestrictToSelection)
		{
			for (int32 BoneIdx : TransformSelection)
			{
				bool bSuccess = SimplifyBone(BoneIdx);
				bNoFailures = bNoFailures && bSuccess;
			}
		}
		else
		{
			for (int32 BoneIdx = 0; BoneIdx < Collection.NumElements(FGeometryCollection::TransformGroup); ++BoneIdx)
			{
				bool bSuccess = SimplifyBone(BoneIdx);
				bNoFailures = bNoFailures && bSuccess;
			}
		}

		return bNoFailures;
	}

	bool SimplifyConvexHull(const ::Chaos::FConvex* InConvexHull, ::Chaos::FConvex* OutConvexHull, const FSimplifyHullSettings& Settings)
	{
		if (!InConvexHull || !OutConvexHull || !InConvexHull->HasStructureData())
		{
			return false;
		}

		const ::Chaos::FConvexStructureData& ConvexStructure = InConvexHull->GetStructureData();
		const int32 NumV = InConvexHull->NumVertices();
		const int32 NumP = InConvexHull->NumPlanes();

		// Check if no simplification required, and skip simplification in that case
		int32 ExpectNumT = 0;
		for (int32 PIdx = 0; PIdx < NumP; ++PIdx)
		{
			const int32 NumFaceV = ConvexStructure.NumPlaneVertices(PIdx);
			ExpectNumT += FMath::Max(0, NumFaceV - 2);
		}
		if (Settings.bUseTargetTriangleCount && ExpectNumT <= Settings.TargetTriangleCount)
		{
			if (OutConvexHull != InConvexHull)
			{
				*OutConvexHull = MoveTemp(*InConvexHull->CopyAsConvex());
			}
			return true;
		}
	
		// Convert to DynamicMesh to run simplifier
		UE::Geometry::FDynamicMesh3 Mesh;
		for (int32 VIdx = 0; VIdx < NumV; ++VIdx)
		{
			int32 MeshVIdx = Mesh.AppendVertex((FVector3d)InConvexHull->GetVertex(VIdx));
			checkSlow(MeshVIdx == VIdx);
		}
		for (int32 PIdx = 0; PIdx < NumP; ++PIdx)
		{
			const int32 NumFaceV = ConvexStructure.NumPlaneVertices(PIdx);
			const int32 V0 = ConvexStructure.GetPlaneVertex(PIdx, 0);
			for (int32 SubIdx = 1; SubIdx + 1 < NumFaceV; ++SubIdx)
			{
				const int32 V1 = ConvexStructure.GetPlaneVertex(PIdx, SubIdx);
				const int32 V2 = ConvexStructure.GetPlaneVertex(PIdx, SubIdx + 1);
				Mesh.AppendTriangle(UE::Geometry::FIndex3i(V0, V1, V2));
			}
		}
		
		// Run simplification
		UE::Geometry::FVolPresMeshSimplification Simplifier(&Mesh);
		
		Simplifier.CollapseMode =
			Settings.bUseExistingVertexPositions ?
				UE::Geometry::FVolPresMeshSimplification::ESimplificationCollapseModes::MinimalExistingVertexError
			:	UE::Geometry::FVolPresMeshSimplification::ESimplificationCollapseModes::MinimalQuadricPositionError;
		if (Settings.bUseGeometricTolerance)
		{
			Simplifier.GeometricErrorConstraint = UE::Geometry::FVolPresMeshSimplification::EGeometricErrorCriteria::PredictedPointToProjectionTarget;
			Simplifier.GeometricErrorTolerance = Settings.ErrorTolerance;
		}
		if (Settings.bUseGeometricTolerance)
		{
			// Simplify to the smallest non-degenerate number of triangles, relying on geometric error criteria
			UE::Geometry::FDynamicMesh3 ProjectionTargetMesh(Mesh);
			UE::Geometry::FDynamicMeshAABBTree3 ProjectionTargetSpatial(&ProjectionTargetMesh, true);
			UE::Geometry::FMeshProjectionTarget ProjTarget(&ProjectionTargetMesh, &ProjectionTargetSpatial);
			Simplifier.SetProjectionTarget(&ProjTarget);
			int32 TargetTriCount = Settings.bUseTargetTriangleCount ? Settings.TargetTriangleCount : 4;
			Simplifier.SimplifyToTriangleCount(TargetTriCount);
		}
		else if (Settings.bUseTargetTriangleCount)
		{
			Simplifier.SimplifyToTriangleCount(Settings.TargetTriangleCount);
		}
		else
		{
			// Note: Quadric error threshold doesn't have the same geometric meaning as distance; this is not equivalent to using a geometric error tolerance
			Simplifier.SimplifyToMaxError(Settings.ErrorTolerance * Settings.ErrorTolerance);
		}

		TArray<::Chaos::FVec3f> NewConvexVerts;
		NewConvexVerts.Reserve(Mesh.VertexCount());
		for (int32 VIdx : Mesh.VertexIndicesItr())
		{
			NewConvexVerts.Add((::Chaos::FVec3f)Mesh.GetVertex(VIdx));
		}
		*OutConvexHull = ::Chaos::FConvex(NewConvexVerts, InConvexHull->GetMargin(), ::Chaos::FConvexBuilder::EBuildMethod::Default);

		return true;
	}
}
