// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/VoxelBlendMeshesOp.h"

#include "CleaningOps/EditNormalsOp.h"

#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshEditor.h"
#include "MeshTransforms.h"
#include "MeshSimplification.h"
#include "MeshNormals.h"
#include "Operations/ExtrudeMesh.h"
#include "Operations/RemoveOccludedTriangles.h"

#include "Implicit/Solidify.h"
#include "Implicit/Blend.h"


void FVoxelBlendMeshesOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FVoxelBlendMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}

	if (!ensure(Transforms.Num() == Meshes.Num()))
	{
		return;
	}

	TImplicitBlend<FDynamicMesh3> ImplicitBlend;

	TArray<FDynamicMesh3> TransformedMeshes; TransformedMeshes.Reserve(Meshes.Num());
	FAxisAlignedBox3d CombinedBounds = FAxisAlignedBox3d::Empty();
	for (int MeshIdx = 0; MeshIdx < Meshes.Num(); MeshIdx++)
	{
		TransformedMeshes.Emplace(*Meshes[MeshIdx]);
		if (TransformedMeshes[MeshIdx].TriangleCount() == 0)
		{
			continue;
		}
		
		if (Transforms[MeshIdx].GetDeterminant() < 0)
		{
			TransformedMeshes[MeshIdx].ReverseOrientation(false);
		}
		MeshTransforms::ApplyTransform(TransformedMeshes[MeshIdx], (FTransform3d)Transforms[MeshIdx]);

		if (bSolidifyInput)
		{
			if (OffsetSolidifySurface > 0)
			{
				// positive offsets should be at least a cell wide so we don't end up deleting a bunch of the input surface
				double CellSize = TransformedMeshes[MeshIdx].GetCachedBounds().MaxDim() / InputVoxelCount;
				double SafeOffset = FMathd::Max(CellSize * 2, OffsetSolidifySurface);

				FMeshNormals::QuickComputeVertexNormals(TransformedMeshes[MeshIdx]);
				FExtrudeMesh Extrude(&TransformedMeshes[MeshIdx]);
				Extrude.DefaultExtrudeDistance = -SafeOffset;
				Extrude.IsPositiveOffset = false;
				Extrude.Apply();
			}

			FDynamicMeshAABBTree3 Spatial(&TransformedMeshes[MeshIdx]);
			TFastWindingTree<FDynamicMesh3> Winding(&Spatial);
			TImplicitSolidify<FDynamicMesh3> Solidify(&TransformedMeshes[MeshIdx], &Spatial, &Winding);
			Solidify.SetCellSizeAndExtendBounds(Spatial.GetBoundingBox(), 0, InputVoxelCount);
			TransformedMeshes[MeshIdx].Copy(&Solidify.Generate());

			if (bRemoveInternalsAfterSolidify)
			{
				UE::MeshAutoRepair::RemoveInternalTriangles(TransformedMeshes[MeshIdx], true, EOcclusionTriangleSampling::Centroids, EOcclusionCalculationMode::FastWindingNumber);
			}
		}

		if (TransformedMeshes[MeshIdx].TriangleCount() == 0)
		{
			continue;
		}
		ImplicitBlend.Sources.Add(&TransformedMeshes[MeshIdx]);
		FAxisAlignedBox3d& SourceBounds = ImplicitBlend.SourceBounds.Add_GetRef(TransformedMeshes[MeshIdx].GetCachedBounds());
		CombinedBounds.Contain(SourceBounds);
	}

	if (ImplicitBlend.Sources.Num() == 0)
	{
		return;
	}

	ImplicitBlend.SetCellSizesAndFalloff(CombinedBounds, BlendFalloff, InputVoxelCount, OutputVoxelCount);
	ImplicitBlend.BlendPower = BlendPower;

	ImplicitBlend.CancelF = [&Progress]()
	{
		return Progress->Cancelled();
	};

	if (Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(&ImplicitBlend.Generate());
	
	PostProcessResult(Progress, ImplicitBlend.MeshCellSize);
}