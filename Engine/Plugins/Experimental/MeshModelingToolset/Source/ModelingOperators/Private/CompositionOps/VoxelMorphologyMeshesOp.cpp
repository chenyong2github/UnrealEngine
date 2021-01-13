// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/VoxelMorphologyMeshesOp.h"

#include "CleaningOps/EditNormalsOp.h"

#include "DynamicMeshEditor.h"

#include "DynamicMeshAABBTree3.h"
#include "MeshTransforms.h"
#include "MeshSimplification.h"
#include "Operations/RemoveOccludedTriangles.h"
#include "Operations/ExtrudeMesh.h"

#include "Generators/MarchingCubes.h"
#include "Implicit/Morphology.h"
#include "Implicit/Solidify.h"


void FVoxelMorphologyMeshesOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FVoxelMorphologyMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}

	TImplicitMorphology<FDynamicMesh3> ImplicitMorphology;
	switch (Operation)
	{
	case EMorphologyOperation::Dilate:
		ImplicitMorphology.MorphologyOp = TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Dilate;
		break;

	case EMorphologyOperation::Contract:
		ImplicitMorphology.MorphologyOp = TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Contract;
		break;

	case EMorphologyOperation::Open:
		ImplicitMorphology.MorphologyOp = TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Open;
		break;

	case EMorphologyOperation::Close:
		ImplicitMorphology.MorphologyOp = TImplicitMorphology<FDynamicMesh3>::EMorphologyOp::Close;
		break;

	default:
		check(false);
	}

	if (!ensure(Transforms.Num() == Meshes.Num()))
	{
		return;
	}

	FDynamicMesh3 CombinedMesh;

	// append all meshes (transformed but without attributes)
	FDynamicMeshEditor AppendEditor(&CombinedMesh);
	for (int MeshIdx = 0; MeshIdx < Meshes.Num(); MeshIdx++)
	{
		FTransform3d MeshTransform = (FTransform3d)Transforms[MeshIdx];
		bool bReverseOrientation = MeshTransform.GetDeterminant() < 0;
		FMeshIndexMappings IndexMaps;
		AppendEditor.AppendMesh(Meshes[MeshIdx].Get(), IndexMaps,
			[MeshTransform](int VID, const FVector3d& Pos)
			{
				return MeshTransform.TransformPosition(Pos);
			}, nullptr
			);
		if (bReverseOrientation)
		{
			for (int TID : Meshes[MeshIdx]->TriangleIndicesItr())
			{
				CombinedMesh.ReverseTriOrientation(IndexMaps.GetNewTriangle(TID));
			}
		}
	}

	if (CombinedMesh.TriangleCount() == 0)
	{
		return;
	}

	if (bSolidifyInput && OffsetSolidifySurface > 0)
	{
		// positive offsets should be at least a cell wide so we don't end up deleting a bunch of the input surface
		double CellSize = CombinedMesh.GetCachedBounds().MaxDim() / InputVoxelCount;
		double SafeOffset = FMathd::Max(CellSize * 2, OffsetSolidifySurface);

		FMeshNormals::QuickComputeVertexNormals(CombinedMesh);
		FExtrudeMesh Extrude(&CombinedMesh);
		Extrude.DefaultExtrudeDistance = -SafeOffset;
		Extrude.IsPositiveOffset = false;
		Extrude.Apply();
	}

	ImplicitMorphology.Source = &CombinedMesh;
	FDynamicMeshAABBTree3 Spatial(&CombinedMesh, true);

	if (bSolidifyInput)
	{
		TFastWindingTree<FDynamicMesh3> Winding(&Spatial);
		TImplicitSolidify<FDynamicMesh3> Solidify(&CombinedMesh, &Spatial, &Winding);
		Solidify.SetCellSizeAndExtendBounds(Spatial.GetBoundingBox(), 0, InputVoxelCount);
		CombinedMesh.Copy(&Solidify.Generate());

		if (bRemoveInternalsAfterSolidify)
		{
			UE::MeshAutoRepair::RemoveInternalTriangles(CombinedMesh, true, EOcclusionTriangleSampling::Centroids, EOcclusionCalculationMode::FastWindingNumber);
		}

		Spatial.Build(); // rebuild w/ updated mesh
	}

	if (CombinedMesh.TriangleCount() == 0)
	{
		return;
	}

	ImplicitMorphology.SourceSpatial = &Spatial;
	ImplicitMorphology.SetCellSizesAndDistance(CombinedMesh.GetCachedBounds(), Distance, InputVoxelCount, OutputVoxelCount);
	
	ImplicitMorphology.CancelF = [&Progress]()
	{
		return Progress->Cancelled();
	};

	;

	if (Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(&ImplicitMorphology.Generate());

	PostProcessResult(Progress, ImplicitMorphology.MeshCellSize);
}