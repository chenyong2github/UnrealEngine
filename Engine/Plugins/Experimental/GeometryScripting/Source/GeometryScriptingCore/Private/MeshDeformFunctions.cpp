// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshDeformFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "SpaceDeformerOps/BendMeshOp.h"
#include "SpaceDeformerOps/TwistMeshOp.h"
#include "UDynamicMesh.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshDeformFunctions"


UDynamicMesh* UGeometryScriptLibrary_MeshDeformFunctions::ApplyBendWarpToMesh(
	UDynamicMesh* TargetMesh,
	FGeometryScriptBendWarpOptions Options,
	float BendAngle,
	float BendExtent,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyBendWarpToMesh_InvalidInput", "ApplyBendWarpToMesh: TargetMesh is Null"));
		return TargetMesh;
	}

	FVector3d UseAxis = Options.BendAxis.GetSafeNormal();
	if (FMath::Abs(1.0 - UseAxis.Length()) > 0.1)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyBendWarpToMesh_InvalidAxis", "ApplyBendWarpToMesh: BendAxis is degenerate"));
		UseAxis = FVector3d::UnitZ();
	}
	FFrame3d WarpFrame = FFrame3d((FVector3d)Options.BendOrigin, (FVector3d)UseAxis);

	if (Options.TowardAxis.Length() > 0)
	{
		FVector3d UseTowardAxis = (FVector3d)Options.TowardAxis.GetSafeNormal();
		if ( WarpFrame.Z().Dot(UseTowardAxis) < 0.95 )
		{
			WarpFrame.ConstrainedAlignAxis(1, UseTowardAxis, WarpFrame.Z());
		}
	}

	if (Options.BendAxisRotation != 0)
	{
		WarpFrame.Rotate(FQuaterniond( WarpFrame.Z(), Options.BendAxisRotation, true));
	}

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
	float TwistAngle,
	float TwistExtent,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyTwistWarpToMesh_InvalidInput", "ApplyTwistWarpToMesh: TargetMesh is Null"));
		return TargetMesh;
	}

	FVector3d UseAxis = Options.TwistAxis.GetSafeNormal();
	if (FMath::Abs(1.0 - UseAxis.Length()) > 0.1)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyTwistWarpToMesh_InvalidAxis", "ApplyTwistWarpToMesh: TwistAxis is degenerate"));
		UseAxis = FVector3d::UnitZ();
	}
	FFrame3d WarpFrame = FFrame3d((FVector3d)Options.TwistOrigin, UseAxis);

	if (Options.TowardAxis.Length() > 0)
	{
		FVector3d UseTowardAxis = (FVector3d)Options.TowardAxis.GetSafeNormal();
		if ( WarpFrame.Z().Dot(UseTowardAxis) < 0.95 )
		{
			WarpFrame.ConstrainedAlignAxis(1, UseTowardAxis, WarpFrame.Z());
		}
	}

	if (Options.TwistAxisRotation != 0)
	{
		WarpFrame.Rotate(FQuaterniond( WarpFrame.Z(), Options.TwistAxisRotation, true));
	}

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




#undef LOCTEXT_NAMESPACE