// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "ModelingOperators.h"
#include "OrientedBoxTypes.h"

class FProgressCancel;

namespace UE {
namespace Geometry {

/**
 * Operator meant to be used with UCubeGridTool that adds or subtracts a box to/from
 * an input mesh, with options to weld the box's corners to make ramps/corners
 */
class MODELINGOPERATORS_API FCubeGridBooleanOp : public FDynamicMeshOperator
{

public:

	virtual ~FCubeGridBooleanOp() {}

	// Inputs:

	// The mesh to add to or subtract from
	TSharedPtr<const FDynamicMesh3, ESPMode::ThreadSafe> InputMesh;
	FTransformSRT3d InputTransform = FTransformSRT3d::Identity();

	// Box from which to generate the second mesh
	FOrientedBox3d WorldBox;

	// Optional: information to weld corners along the Z axis to create ramps/pyramids/pyramid cutaways
	struct FCornerInfo
	{
		// "Base" refers to the 0-3 indexed corners in FOrientedBox3d, and the flags here are
		// true if the vertex is welded to its Z axis neighbor. Note that we have a choice of
		// which of the two resulting sides of the box volume form our operator mesh. In the
		// case of addition, we choose the lower side on the Z axis (i.e., non-welded verts
		// stick up), while in subtraction, we choose the higher side on the Z axis (i.e.
		// welded verts stick down and therefore cut away the input mesh). This lines up with
		// how a user uses this operation- they select some corners, and either pull them out
		// or push them in.
		bool WeldedAtBase[4] = { false, false, false, false };
	};
	TSharedPtr<FCornerInfo> CornerInfo;

	// Determines whether the box is added or subtracted from the mesh. Also has an effect on
	// how we treat operators with welded corners, see above.
	bool bSubtract = false;

	// When true, mesh is constructed such that ResultTransform is InputTransform. When false,
	// ResultTransform is based off of the centroid of the result.
	bool bKeepInputTransform = false;

	// Only relevant when CornerInfo is used. When true, diagonal will generally prefer to 
	// lie flat across the non-planar top. This determines, for instance, whether a pulled
	// corner results in a pyramid with three faces (if true) or four (if false).
	bool bCrosswiseDiagonal = false;

	// Outputs:

	// Will be reset to a container storing tids whose positions or connectivity changed. Only filled
	// if bTrackChangedTids is true.
	TSharedPtr<TArray<int32>, ESPMode::ThreadSafe> ChangedTids;
	bool bTrackChangedTids = false;

	virtual void CalculateResult(FProgressCancel* Progress) override;
};


}} // end namespace UE::Geometry
