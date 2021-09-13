// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshBooleanFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "UDynamicMesh.h"

#include "Operations/MeshBoolean.h"
#include "Operations/MeshSelfUnion.h"
#include "Operations/MeshPlaneCut.h"
#include "Operations/MeshMirror.h"
#include "MeshSimplification.h"
#include "MeshBoundaryLoops.h"
#include "Operations/MinimalHoleFiller.h"
#include "ConstrainedDelaunay2.h"



#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshBooleanFunctions"





UDynamicMesh* UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshBoolean(
	UDynamicMesh* TargetMesh,
	FTransform TargetTransform,
	UDynamicMesh* ToolMesh,
	FTransform ToolTransform,
	EGeometryScriptBooleanOperation Operation,
	FGeometryScriptMeshBooleanOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyMeshBoolean_InvalidInput1", "ApplyMeshBoolean: TargetMesh is Null"));
		return TargetMesh;
	}
	if (ToolMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyMeshBoolean_InvalidInput2", "ApplyMeshBoolean: ToolMesh is Null"));
		return TargetMesh;
	}


	FMeshBoolean::EBooleanOp ApplyOperation = FMeshBoolean::EBooleanOp::Union;
	switch (Operation)
	{
	case EGeometryScriptBooleanOperation::Intersection: 
		ApplyOperation = FMeshBoolean::EBooleanOp::Intersect;
		break;
	case EGeometryScriptBooleanOperation::Subtract: 
		ApplyOperation = FMeshBoolean::EBooleanOp::Difference;
		break;	
	}

	FDynamicMesh3 NewResultMesh;
	bool bSuccess = false;
	TArray<int> NewBoundaryEdges;

	// TODO: can actually compute in place (ie without making a copy) if we pass the first mesh as the ResultMesh argument...
	TargetMesh->ProcessMesh([&](const FDynamicMesh3& Mesh1)
	{
		ToolMesh->ProcessMesh([&](const FDynamicMesh3& Mesh2)
		{
			FMeshBoolean MeshBoolean(
				&Mesh1, (UE::Geometry::FTransform3d)TargetTransform,
				&Mesh2, (UE::Geometry::FTransform3d)ToolTransform,
				&NewResultMesh, ApplyOperation);
			MeshBoolean.bPutResultInInputSpace = true;
			MeshBoolean.bSimplifyAlongNewEdges = Options.bSimplifyOutput;
			bSuccess = MeshBoolean.Compute();
			NewBoundaryEdges = MoveTemp(MeshBoolean.CreatedBoundaryEdges);
		});
	});

	// bSuccess comes back false even if we only had small errors...
	bSuccess = (NewResultMesh.TriangleCount() > 0);
	 
	if (bSuccess == false)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BooleanUnion_Failed", "BooleanUnion: Boolean operation failed"));
		return TargetMesh;
	}

	// Boolean result is in the space of TargetTransform, so invert that
	MeshTransforms::ApplyTransform(NewResultMesh, (UE::Geometry::FTransform3d)TargetTransform.Inverse());

	if (NewBoundaryEdges.Num() > 0 && Options.bFillHoles)
	{
		FMeshBoundaryLoops OpenBoundary(&NewResultMesh, false);
		TSet<int> ConsiderEdges(NewBoundaryEdges);
		OpenBoundary.EdgeFilterFunc = [&ConsiderEdges](int EID)
		{
			return ConsiderEdges.Contains(EID);
		};
		OpenBoundary.Compute();

		for (FEdgeLoop& Loop : OpenBoundary.Loops)
		{
			FMinimalHoleFiller Filler(&NewResultMesh, Loop);
			Filler.Fill();
		}
	}

	TargetMesh->SetMesh(MoveTemp(NewResultMesh));

	return TargetMesh;
}





UDynamicMesh* UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshSelfUnion(
	UDynamicMesh* TargetMesh,
	FGeometryScriptMeshSelfUnionOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyMeshSelfUnion_InvalidInput", "ApplyMeshSelfUnion: TargetMesh is Null"));
		return TargetMesh;
	}

	// we emit multiple change events below...not clear how to avoid this though as
	// the hole-fill edit may not occur

	bool bSuccess = false;
	TArray<int> NewBoundaryEdges;
	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
	{
		FMeshSelfUnion Union(&EditMesh);
		Union.WindingThreshold = FMath::Clamp(Options.WindingThreshold, 0.0f, 1.0f);
		Union.bTrimFlaps = Options.bTrimFlaps;
		Union.bSimplifyAlongNewEdges = Options.bSimplifyOutput;
		Union.SimplificationAngleTolerance = Options.SimplifyPlanarTolerance;
		bSuccess = Union.Compute();
		NewBoundaryEdges = MoveTemp(Union.CreatedBoundaryEdges);
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	// currently ignoring bSuccess as it returns false in many cases
	//if (bSuccess == false)
	//{
	//	UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("BooleanUnion_Failed", "BooleanUnion: Boolean operation failed"));
	//	return TargetMesh;
	//}

	if (NewBoundaryEdges.Num() > 0 && Options.bFillHoles)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			FMeshBoundaryLoops OpenBoundary(&EditMesh, false);
			TSet<int> ConsiderEdges(NewBoundaryEdges);
			OpenBoundary.EdgeFilterFunc = [&ConsiderEdges](int EID)
			{
				return ConsiderEdges.Contains(EID);
			};
			OpenBoundary.Compute();

			for (FEdgeLoop& Loop : OpenBoundary.Loops)
			{
				FMinimalHoleFiller Filler(&EditMesh, Loop);
				Filler.Fill();
			}
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);
	}

	return TargetMesh;
}







UDynamicMesh* UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshPlaneCut(
	UDynamicMesh* TargetMesh,
	FVector CutPlaneOrigin,
	FVector CutPlaneNormal,
	FGeometryScriptMeshPlaneCutOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyMeshPlaneCut_InvalidInput", "ApplyMeshPlaneCut: TargetMesh is Null"));
		return TargetMesh;
	}

	if (Options.bFlipCutSide)
	{
		CutPlaneNormal = -CutPlaneNormal;
	}
	FVector3d UseNormal = CutPlaneNormal.GetSafeNormal();
	if (FMath::Abs(1.0 - UseNormal.Length()) > 0.1)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyMeshPlaneCut_InvalidNormal", "ApplyMeshPlaneCut: Normal vector is degenerate"));
		UseNormal = FVector3d::UnitZ();
	}

	bool bSuccess = false;
	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
	{
		FMeshPlaneCut Cut(&EditMesh, CutPlaneOrigin, UseNormal);
		bSuccess = Cut.Cut();

		if (Options.bFillHoles)
		{
			Cut.HoleFill(ConstrainedDelaunayTriangulate<double>, Options.bFillSpans);
		}

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}





UDynamicMesh* UGeometryScriptLibrary_MeshBooleanFunctions::ApplyMeshMirror(
	UDynamicMesh* TargetMesh,
	FVector MirrorPlaneOrigin,
	FVector MirrorPlaneNormal,
	FGeometryScriptMeshMirrorOptions Options,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyMeshMirror_InvalidInput", "ApplyMeshMirror: TargetMesh is Null"));
		return TargetMesh;
	}

	const double PlaneTolerance = FMathf::ZeroTolerance * 10.0;

	if (Options.bApplyPlaneCut && Options.bFlipCutSide)
	{
		MirrorPlaneNormal = -MirrorPlaneNormal;
	}
	FVector3d UseNormal = MirrorPlaneNormal.GetSafeNormal();
	if (FMath::Abs(1.0 - UseNormal.Length()) > 0.1)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ApplyMeshMirror_InvalidNormal", "ApplyMeshMirror: Normal vector is degenerate"));
		UseNormal = FVector3d::UnitZ();
	}

	bool bSuccess = false;
	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
	{
		if (Options.bApplyPlaneCut)
		{
			FMeshPlaneCut Cutter(&EditMesh, MirrorPlaneOrigin, UseNormal);
			Cutter.PlaneTolerance = PlaneTolerance;
			Cutter.Cut();
		}

		FMeshMirror Mirrorer(&EditMesh, MirrorPlaneOrigin, UseNormal);
		Mirrorer.bWeldAlongPlane = Options.bWeldAlongPlane;
		Mirrorer.bAllowBowtieVertexCreation = false;
		Mirrorer.PlaneTolerance = PlaneTolerance;

		Mirrorer.MirrorAndAppend(nullptr);

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}




#undef LOCTEXT_NAMESPACE