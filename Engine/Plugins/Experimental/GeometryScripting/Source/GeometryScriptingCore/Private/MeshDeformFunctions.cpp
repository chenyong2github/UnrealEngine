// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshDeformFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "SpaceDeformerOps/BendMeshOp.h"
#include "SpaceDeformerOps/TwistMeshOp.h"
#include "SpaceDeformerOps/FlareMeshOp.h"
#include "UDynamicMesh.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshDeformFunctions"


UDynamicMesh* UGeometryScriptLibrary_MeshDeformFunctions::ApplyBendWarpToMesh(
	UDynamicMesh* TargetMesh,
	FGeometryScriptBendWarpOptions Options,
	FTransform BendOrientation,
	float BendAngle,
	float BendExtent,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyBendWarpToMesh_InvalidInput", "ApplyBendWarpToMesh: TargetMesh is Null"));
		return TargetMesh;
	}

	FFrame3d WarpFrame = FFrame3d(BendOrientation);

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		// todo extract bend warp into standalone math code

		TSharedPtr<FDynamicMesh3> TmpMeshPtr = MakeShared<FDynamicMesh3>();
		*TmpMeshPtr = MoveTemp(EditMesh);

		FBendMeshOp BendOp;
		BendOp.OriginalMesh = TmpMeshPtr;
		BendOp.GizmoFrame = WarpFrame;
		BendOp.LowerBoundsInterval = (Options.bSymmetricExtents) ? -BendExtent : -Options.LowerExtent;
		BendOp.UpperBoundsInterval = BendExtent;
		BendOp.BendDegrees = BendAngle;
		BendOp.bLockBottom = !Options.bBidirectional;

		BendOp.CalculateResult(nullptr);

		TUniquePtr<FDynamicMesh3> NewResultMesh = BendOp.ExtractResult();
		FDynamicMesh3* NewResultMeshPtr = NewResultMesh.Release();
		EditMesh = MoveTemp(*NewResultMeshPtr);

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}





UDynamicMesh* UGeometryScriptLibrary_MeshDeformFunctions::ApplyTwistWarpToMesh(
	UDynamicMesh* TargetMesh,
	FGeometryScriptTwistWarpOptions Options,
	FTransform TwistOrientation,
	float TwistAngle,
	float TwistExtent,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyTwistWarpToMesh_InvalidInput", "ApplyTwistWarpToMesh: TargetMesh is Null"));
		return TargetMesh;
	}

	FFrame3d WarpFrame = FFrame3d(TwistOrientation);

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		// todo extract Twist warp into standalone math code
		TSharedPtr<FDynamicMesh3> TmpMeshPtr = MakeShared<FDynamicMesh3>();
		*TmpMeshPtr = MoveTemp(EditMesh);

		FTwistMeshOp TwistOp;
		TwistOp.OriginalMesh = TmpMeshPtr;
		TwistOp.GizmoFrame = WarpFrame;
		TwistOp.LowerBoundsInterval = (Options.bSymmetricExtents) ? -TwistExtent : -Options.LowerExtent;
		TwistOp.UpperBoundsInterval = TwistExtent;
		TwistOp.TwistDegrees = TwistAngle;
		TwistOp.bLockBottom = !Options.bBidirectional;

		TwistOp.CalculateResult(nullptr);

		TUniquePtr<FDynamicMesh3> NewResultMesh = TwistOp.ExtractResult();
		FDynamicMesh3* NewResultMeshPtr = NewResultMesh.Release();
		EditMesh = MoveTemp(*NewResultMeshPtr);

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshDeformFunctions::ApplyFlareWarpToMesh(
	UDynamicMesh* TargetMesh,
	FGeometryScriptFlareWarpOptions Options,
	FTransform FlareOrientation,
	float FlarePercentX,
	float FlarePercentY,
	float FlareExtent,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyFlareWarpToMesh_InvalidInput", "ApplyFlareWarpToMesh: TargetMesh is Null"));
		return TargetMesh;
	}

	FFrame3d WarpFrame = FFrame3d(FlareOrientation);

	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		// todo extract Flare warp into standalone math code
		TSharedPtr<FDynamicMesh3> TmpMeshPtr = MakeShared<FDynamicMesh3>();
		*TmpMeshPtr = MoveTemp(EditMesh);

		FFlareMeshOp FlareOp;
		FlareOp.OriginalMesh = TmpMeshPtr;
		FlareOp.GizmoFrame = WarpFrame;
		FlareOp.LowerBoundsInterval = (Options.bSymmetricExtents) ? -FlareExtent : -Options.LowerExtent;
		FlareOp.UpperBoundsInterval = FlareExtent;
		FlareOp.FlarePercentX = FlarePercentX;
		FlareOp.FlarePercentY = FlarePercentY;
		FlareOp.bSmoothEnds = Options.bSmoothEnds;

		FlareOp.CalculateResult(nullptr);

		TUniquePtr<FDynamicMesh3> NewResultMesh = FlareOp.ExtractResult();
		FDynamicMesh3* NewResultMeshPtr = NewResultMesh.Release();
		EditMesh = MoveTemp(*NewResultMeshPtr);

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}





#undef LOCTEXT_NAMESPACE