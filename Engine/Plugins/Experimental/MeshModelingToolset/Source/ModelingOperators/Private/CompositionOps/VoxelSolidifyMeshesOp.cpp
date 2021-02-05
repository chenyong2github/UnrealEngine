// Copyright Epic Games, Inc. All Rights Reserved.

#include "CompositionOps/VoxelSolidifyMeshesOp.h"

#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshEditor.h"
#include "MeshTransforms.h"
#include "MeshSimplification.h"
#include "Operations/ExtrudeMesh.h"

#include "Spatial/FastWinding.h"
#include "Generators/MarchingCubes.h"
#include "MeshNormals.h"

#include "Implicit/Solidify.h"


void FVoxelSolidifyMeshesOp::SetTransform(const FTransform& Transform) {
	ResultTransform = (FTransform3d)Transform;
}

void FVoxelSolidifyMeshesOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress && Progress->Cancelled())
	{
		return;
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

	if (bApplyThickenShells)
	{
		// thickness should be at least a cell wide so we don't end up deleting a bunch of the input surface
		double CellSize = CombinedMesh.GetCachedBounds().MaxDim() / InputVoxelCount;
		double SafeThickness = FMathd::Max(CellSize * 2, ThickenShells);

		FMeshNormals::QuickComputeVertexNormals(CombinedMesh);
		FExtrudeMesh Extrude(&CombinedMesh);
		Extrude.bSkipClosedComponents = true;
		Extrude.DefaultExtrudeDistance = -SafeThickness;
		Extrude.IsPositiveOffset = false;
		Extrude.Apply();
	}

	FDynamicMeshAABBTree3 Spatial(&CombinedMesh);
	TFastWindingTree<FDynamicMesh3> FastWinding(&Spatial);


	TImplicitSolidify<FDynamicMesh3> Solidify(&CombinedMesh, &Spatial, &FastWinding);
	Solidify.SetCellSizeAndExtendBounds(Spatial.GetBoundingBox(), ExtendBounds, OutputVoxelCount);
	Solidify.WindingThreshold = WindingThreshold;
	Solidify.SurfaceSearchSteps = SurfaceSearchSteps;
	Solidify.bSolidAtBoundaries = bSolidAtBoundaries;
	Solidify.ExtendBounds = ExtendBounds;

	if (Progress && Progress->Cancelled())
	{
		return;
	}

	ResultMesh->Copy(&Solidify.Generate());
	
	PostProcessResult(Progress, Solidify.MeshCellSize);
}