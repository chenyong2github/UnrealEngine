// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CleaningOps/SimplifyMeshOp.h"

#include "DynamicMeshAttributeSet.h"
#include "MeshDescriptionToDynamicMesh.h"


#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshSimplification.h"
#include "MeshConstraintsUtil.h"
#include "ProjectionTargets.h"

#include "Operations/MergeCoincidentMeshEdges.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
#include "OverlappingCorners.h"
#include "IMeshReductionInterfaces.h"

#include "MeshNormals.h"


template <typename SimplificationType>
void ComputeSimplify(FDynamicMesh3* TargetMesh, const bool bReproject, int OriginalTriCount, FDynamicMesh3& OriginalMesh, FDynamicMeshAABBTree3& OriginalMeshSpatial, const ESimplifyTargetType TargetMode, const float TargetPercentage, const int TargetCount, const float TargetEdgeLength)
{
	SimplificationType Reducer(TargetMesh);

	Reducer.ProjectionMode = (bReproject) ? 
		SimplificationType::ETargetProjectionMode::AfterRefinement : SimplificationType::ETargetProjectionMode::NoProjection;

	Reducer.DEBUG_CHECK_LEVEL = 0;

	FMeshConstraints constraints;
	FMeshConstraintsUtil::ConstrainAllSeams(constraints, *TargetMesh, true, false);
	//FMeshConstraintsUtil::ConstrainAllSeamJunctions(constraints, *TargetMesh, true, false);
	Reducer.SetExternalConstraints(&constraints);

	FMeshProjectionTarget ProjTarget(&OriginalMesh, &OriginalMeshSpatial);
	Reducer.SetProjectionTarget(&ProjTarget);

	if (TargetMode == ESimplifyTargetType::Percentage)
	{
		double Ratio = (double)TargetPercentage / 100.0;
		int UseTarget = FMath::Max(4, (int)(Ratio * (double)OriginalTriCount));
		Reducer.SimplifyToTriangleCount(UseTarget);
	} 
	else if (TargetMode == ESimplifyTargetType::TriangleCount)
	{
		Reducer.SimplifyToTriangleCount(TargetCount);
	}
	else if (TargetMode == ESimplifyTargetType::EdgeLength)
	{
		Reducer.SimplifyToEdgeLength(TargetEdgeLength);
	}
}



void FSimplifyMeshOp::CalculateResult(FProgressCancel* Progress)
{
	if (Progress->Cancelled())
	{
		return;
	}

	// Need access to the source mesh:
	const FMeshDescription* MeshDescription = OriginalMeshDescription.Get();
	FDynamicMesh3* TargetMesh = ResultMesh.Get();

	if (Progress->Cancelled())
	{
		return;
	}

	int OriginalTriCount = OriginalMesh->TriangleCount();
	if (SimplifierType != ESimplifyType::UE4Standard)
	{
		// GeometryProcessing-specific methods that operate directly on a DynamicMesh3
		ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributes);

		if (SimplifierType == ESimplifyType::QEM)
		{
			ComputeSimplify<FQEMSimplification>(TargetMesh, bReproject, OriginalTriCount, *OriginalMesh, *OriginalMeshSpatial, 
				TargetMode, TargetPercentage, TargetCount, TargetEdgeLength);
		}
		else if (SimplifierType == ESimplifyType::Attribute)
		{
			ComputeSimplify<FAttrMeshSimplification>(TargetMesh, bReproject, OriginalTriCount, *OriginalMesh, *OriginalMeshSpatial,
				TargetMode, TargetPercentage, TargetCount, TargetEdgeLength);
		}
	}
	else // SimplifierType == ESimplifyType::UE4Standard
	{
		const FMeshDescription* SrcMeshDescription = OriginalMeshDescription.Get();
		FMeshDescription DstMeshDescription(*SrcMeshDescription);

		if (Progress->Cancelled())
		{
			return;
		}

		FOverlappingCorners OverlappingCorners;
		FMeshDescriptionOperations::FindOverlappingCorners(OverlappingCorners, *SrcMeshDescription, 1.e-5);

		if (Progress->Cancelled())
		{
			return;
		}

		FMeshReductionSettings ReductionSettings;
		if (TargetMode == ESimplifyTargetType::Percentage)
		{
			ReductionSettings.PercentTriangles = FMath::Max(TargetPercentage / 100., .001);  // Only support triangle percentage and count, but not edge length
		}
		else if (TargetMode == ESimplifyTargetType::TriangleCount)
		{
			int32 NumTris = SrcMeshDescription->Polygons().Num();
			ReductionSettings.PercentTriangles = (float)TargetCount / (float)NumTris;
		}

		float Error;
		{
			if (!MeshReduction)
			{
				// no reduction possible, failed to load the required interface
				Error = 0.f;
				ResultMesh->Copy(*OriginalMesh, true, true, true, !bDiscardAttributes);
				return;
			}

			Error = ReductionSettings.MaxDeviation;
			MeshReduction->ReduceMeshDescription(DstMeshDescription, Error, *SrcMeshDescription, OverlappingCorners, ReductionSettings);
		}

		if (Progress->Cancelled())
		{
			return;
		}

		// Put the reduced mesh into the target...
		FMeshDescriptionToDynamicMesh Converter;
		Converter.Convert(&DstMeshDescription, *ResultMesh);
		if (bDiscardAttributes)
		{
			ResultMesh->DiscardAttributes();
		}


		bool bFailedModifyNeedsRegen = false;
		// The UE4 tool will split the UV boundaries.  Need to weld this.
		{
			FDynamicMesh3* ComponentMesh = ResultMesh.Get();

			FMergeCoincidentMeshEdges Merger(ComponentMesh);
			Merger.MergeSearchTolerance = 10.0f * FMathf::ZeroTolerance;
			Merger.OnlyUniquePairs = false;
			if (Merger.Apply() == false)
			{
				bFailedModifyNeedsRegen = true;
			}

			if (Progress->Cancelled())
			{
				return;
			}

			if (ResultMesh->CheckValidity(true, EValidityCheckFailMode::ReturnOnly) == false)
			{
				bFailedModifyNeedsRegen = true;
			}

			if (Progress->Cancelled())
			{
				return;
			}

			// in the fallback case where merge edges failed, give up and reset it to what it was before the attempted merger (w/ split UV boundaries everywhere, oh well)
			if (bFailedModifyNeedsRegen)
			{
				ResultMesh->Clear();
				Converter.Convert(&DstMeshDescription, *ResultMesh);
				if (bDiscardAttributes)
				{
					ResultMesh->DiscardAttributes();
				}
			}
		}
	}


	if (Progress->Cancelled())
	{
		return;
	}

	if (!ResultMesh->HasAttributes())
	{
		FMeshNormals::QuickComputeVertexNormals(*ResultMesh);
	}

}