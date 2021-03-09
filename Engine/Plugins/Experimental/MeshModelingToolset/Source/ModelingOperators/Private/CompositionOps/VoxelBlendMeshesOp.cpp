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

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

void FVoxelBlendMeshesOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FVoxelBlendMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		return;
	}

	if (!ensure(Transforms.Num() == Meshes.Num()))
	{
		return;
	}

	TImplicitBlend<FDynamicMesh3> ImplicitBlend;
	ImplicitBlend.bSubtract = bSubtract;

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

		if (bVoxWrap)
		{
			if (ThickenShells > 0 && !TransformedMeshes[MeshIdx].IsClosed())
			{
				// thickness should be at least a cell wide so we don't end up deleting a bunch of the input surface
				double CellSize = TransformedMeshes[MeshIdx].GetCachedBounds().MaxDim() / InputVoxelCount;
				double SafeThickness = FMathd::Max(CellSize * 2, ThickenShells);

				FMeshNormals::QuickComputeVertexNormals(TransformedMeshes[MeshIdx]);
				FExtrudeMesh Extrude(&TransformedMeshes[MeshIdx]);
				Extrude.bSkipClosedComponents = true;
				Extrude.DefaultExtrudeDistance = -SafeThickness;
				Extrude.IsPositiveOffset = false;
				Extrude.Apply();
			}

			FDynamicMeshAABBTree3 Spatial(&TransformedMeshes[MeshIdx]);
			TFastWindingTree<FDynamicMesh3> Winding(&Spatial);
			TImplicitSolidify<FDynamicMesh3> Solidify(&TransformedMeshes[MeshIdx], &Spatial, &Winding);
			Solidify.SetCellSizeAndExtendBounds(Spatial.GetBoundingBox(), 0, InputVoxelCount);
			TransformedMeshes[MeshIdx].Copy(&Solidify.Generate());

			if (bRemoveInternalsAfterVoxWrap)
			{
				UE::MeshAutoRepair::RemoveInternalTriangles(TransformedMeshes[MeshIdx], true, EOcclusionTriangleSampling::Centroids, EOcclusionCalculationMode::FastWindingNumber, 0, .5, true);
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
		return Progress && Progress->Cancelled();
	};

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(&ImplicitBlend.Generate());
	
	PostProcessResult(Progress, ImplicitBlend.MeshCellSize);
}