// Copyright Epic Games, Inc. All Rights Reserved.

#include "CleaningOps/RemeshMeshOp.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAABBTree3.h"
#include "DynamicMeshAttributeSet.h"
#include "Remesher.h"
#include "MeshConstraintsUtil.h"
#include "ProjectionTargets.h"
#include "MeshNormals.h"

void FRemeshMeshOp::SetTransform(const FTransform& Transform)
{
	ResultTransform = (FTransform3d)Transform;
}

void FRemeshMeshOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}

	bool bDiscardAttributesImmediately = bDiscardAttributes && !bPreserveSharpEdges;
	ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributesImmediately);

	if (Progress->Cancelled())
	{
		return;
	}

	FDynamicMesh3* TargetMesh = ResultMesh.Get();

	FRemesher Remesher(TargetMesh);
	Remesher.bEnableSplits = bSplits;
	Remesher.bEnableFlips = bFlips;
	Remesher.bEnableCollapses = bCollapses;

	Remesher.SetTargetEdgeLength(TargetEdgeLength);

	Remesher.ProjectionMode = (bReproject) ? 
		FRemesher::ETargetProjectionMode::AfterRefinement : FRemesher::ETargetProjectionMode::NoProjection;

	Remesher.bEnableSmoothing = (SmoothingSpeed > 0);
	Remesher.SmoothSpeedT = SmoothingSpeed;
	// convert smooth type from UI enum to (currently 1:1) FRemesher enum
	Remesher.SmoothType = FRemesher::ESmoothTypes::Uniform;
	if (!bDiscardAttributes)
	{
		switch (SmoothingType)
		{
		case ERemeshSmoothingType::Uniform:
			Remesher.SmoothType = FRemesher::ESmoothTypes::Uniform;
			break;
		case ERemeshSmoothingType::Cotangent:
			Remesher.SmoothType = FRemesher::ESmoothTypes::Cotan;
			break;
		case ERemeshSmoothingType::MeanValue:
			Remesher.SmoothType = FRemesher::ESmoothTypes::MeanValue;
			break;
		default:
			ensure(false);
		}
	}
	bool bIsUniformSmooth = (Remesher.SmoothType == FRemesher::ESmoothTypes::Uniform);

	Remesher.bPreventNormalFlips = bPreventNormalFlips;

	Remesher.DEBUG_CHECK_LEVEL = 0;

	FMeshConstraints constraints;
	FMeshConstraintsUtil::ConstrainAllSeams(constraints, *TargetMesh, true, !bPreserveSharpEdges);
	Remesher.SetExternalConstraints(&constraints);

	FMeshProjectionTarget ProjTarget(OriginalMesh.Get(), OriginalMeshSpatial.Get());
	Remesher.SetProjectionTarget(&ProjTarget);

	Remesher.Progress = Progress;

	if (bDiscardAttributes && !bDiscardAttributesImmediately)
	{
		TargetMesh->DiscardAttributes();
	}

	// run the remesh iterations
	for (int k = 0; k < RemeshIterations; ++k)
	{
		// If we are not uniform smoothing, then flips seem to often make things worse.
		// Possibly this is because without the tangential flow, we won't get to the nice tris.
		// In this case we are better off basically not flipping, and just letting collapses resolve things
		// regular-valence polygons - things stay "stuck". 
		// @todo try implementing edge-length flip criteria instead of valence-flip
		if (bIsUniformSmooth == false)
		{
			bool bUseFlipsThisPass = (k % 2 == 0 && k < RemeshIterations/2);
			Remesher.bEnableFlips = bUseFlipsThisPass && bFlips;
		}

		Remesher.BasicRemeshPass();
	}

	if (!TargetMesh->HasAttributes())
	{
		FMeshNormals::QuickComputeVertexNormals(*TargetMesh);
	}

}