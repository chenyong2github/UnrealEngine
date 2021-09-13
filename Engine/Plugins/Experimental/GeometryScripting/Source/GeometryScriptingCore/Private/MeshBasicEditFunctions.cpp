// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshBasicEditFunctions.h"
#include "UDynamicMesh.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "DynamicMesh/MeshTransforms.h"
#include "DynamicMeshEditor.h"

#include "ExplicitUseGeometryMathTypes.h"		// using UE::Geometry::(math types)
using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshBasicEditFunctions"


UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::DiscardMeshAttributes(UDynamicMesh* TargetMesh, bool bDeferChangeNotifications)
{
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			EditMesh.DiscardAttributes();
			EditMesh.DiscardVertexNormals();

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);
	}
	return TargetMesh;
}





UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::SetVertexPosition(UDynamicMesh* TargetMesh, int32 VertexID, FVector NewPosition, bool& bIsValidVertex, bool bDeferChangeNotifications)
{
	bIsValidVertex = false;
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			if (EditMesh.IsVertex(VertexID))
			{
				bIsValidVertex = true;
				EditMesh.SetVertex(VertexID, (FVector3d)NewPosition);
			}
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);
	}
	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::AddVertexToMesh(
	UDynamicMesh* TargetMesh,
	FVector NewPosition,
	int32& NewVertexIndex,
	bool bDeferChangeNotifications)
{
	NewVertexIndex = INDEX_NONE;
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			NewVertexIndex = EditMesh.AppendVertex(NewPosition);

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);
	}
	return TargetMesh;
}

UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::AddVerticesToMesh(
	UDynamicMesh* TargetMesh,
	const TArray<FVector>& NewPositions, 
	TArray<int>& NewIndices,
	bool bDeferChangeNotifications)
{
	NewIndices.Reset();
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			for (FVector Position : NewPositions)
			{
				int32 NewVertexIndex = EditMesh.AppendVertex(Position);
				NewIndices.Add(NewVertexIndex);
			}
		
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);
	}
	return TargetMesh;
}


UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteVertexFromMesh(
	UDynamicMesh* TargetMesh,
	int VertexID,
	bool& bWasVertexDeleted,
	bool bDeferChangeNotifications)
{
	bWasVertexDeleted = false;
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			EMeshResult Result = EditMesh.RemoveVertex(VertexID);
			bWasVertexDeleted = (Result == EMeshResult::Ok);

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);
	}
	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::AddTriangleToMesh(
	UDynamicMesh* TargetMesh,
	FIntVector NewTriangle,
	int32& NewTriangleIndex,
	int32 NewTriangleGroupID,
	bool bDeferChangeNotifications,
	UGeometryScriptDebug* Debug)
{
	NewTriangleIndex = INDEX_NONE;
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			NewTriangleIndex = EditMesh.AppendTriangle((FIndex3i)NewTriangle, NewTriangleGroupID);

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);

		if (NewTriangleIndex < 0)
		{
			if (NewTriangleIndex == FDynamicMesh3::NonManifoldID)
			{
				UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AddTriangleToMesh_NonManifold", "AddTriangleToMesh: Triangle cannot be added because it would create invalid Non-Manifold Mesh Topology"));
			}
			else if (NewTriangleIndex == FDynamicMesh3::DuplicateTriangleID)
			{
				UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AddTriangleToMesh_Duplicate", "AddTriangleToMesh: Triangle cannot be added because it is a duplicate of an existing Triangle"));
			}
			else
			{
				UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("AddTriangleToMesh_Unknown", "AddTriangleToMesh: adding Triangle Failed"));
			}
			NewTriangleIndex = INDEX_NONE;
		}
	}
	else
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AddTriangleToMesh_InvalidMesh", "AddTriangleToMesh: TargetMesh is Null"));
	}
	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::AddTrianglesToMesh(
	UDynamicMesh* TargetMesh,
	const TArray<FIntVector>& NewTriangles,
	TArray<int>& NewIndices,
	int32 NewTriangleGroupID,
	bool bDeferChangeNotifications,
	UGeometryScriptDebug* Debug)
{
	NewIndices.Reset();
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			for (FIntVector Triangle : NewTriangles)
			{
				int32 NewTriangleIndex = EditMesh.AppendTriangle((FIndex3i)Triangle, NewTriangleGroupID);
				NewIndices.Add(NewTriangleIndex);
			}

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);

		for (int32& NewTriangleIndex : NewIndices)
		{
			if (NewTriangleIndex < 0)
			{
				if (NewTriangleIndex == FDynamicMesh3::NonManifoldID)
				{
					UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AddTrianglesToMesh_NonManifold", "AddTrianglesToMesh: Triangle cannot be added because it would create invalid Non-Manifold Mesh Topology"));
				}
				else if (NewTriangleIndex == FDynamicMesh3::DuplicateTriangleID)
				{
					UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AddTrianglesToMesh_Duplicate", "AddTrianglesToMesh: Triangle cannot be added because it is a duplicate of an existing Triangle"));
				}
				else
				{
					UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::OperationFailed, LOCTEXT("AddTrianglesToMesh_Unknown", "AddTrianglesToMesh: adding Triangle Failed"));
				}
				NewTriangleIndex = INDEX_NONE;
			}
		}
	}
	else
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AddTrianglesToMesh_InvalidMesh", "AddTriangleToMesh: TargetMesh is Null"));
	}
	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::DeleteTriangleFromMesh(
	UDynamicMesh* TargetMesh,
	int TriangleID,
	bool& bWasTriangleDeleted,
	bool bDeferChangeNotifications)
{
	bWasTriangleDeleted = false;
	if (TargetMesh)
	{
		TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh)
		{
			EMeshResult Result = EditMesh.RemoveTriangle(TriangleID);
			bWasTriangleDeleted = (Result == EMeshResult::Ok);

		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);
	}
	return TargetMesh;
}




UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMesh(
	UDynamicMesh* TargetMesh,
	UDynamicMesh* AppendMesh,
	FTransform AppendTransform,
	bool bDeferChangeNotifications,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendMesh_InvalidInput1", "AppendMesh: TargetMesh is Null"));
		return TargetMesh;
	}
	if (AppendMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendMesh_InvalidInput2", "AppendMesh: AppendMesh is Null"));
		return TargetMesh;
	}

	TargetMesh->EditMesh([&](FDynamicMesh3& AppendToMesh)
	{
		AppendMesh->ProcessMesh([&](const FDynamicMesh3& OtherMesh)
		{
			UE::Geometry::FTransform3d XForm(AppendTransform);
			FMeshIndexMappings TmpMappings;
			FDynamicMeshEditor Editor(&AppendToMesh);
			Editor.AppendMesh(&OtherMesh, TmpMappings,
				[&](int, const FVector3d& Position) { return XForm.TransformPosition(Position); },
				[&](int, const FVector3d& Normal) { return XForm.TransformNormal(Normal); });
		});
	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshBasicEditFunctions::AppendMeshRepeated(
	UDynamicMesh* TargetMesh,
	UDynamicMesh* AppendMesh,
	FTransform AppendTransform,
	int32 RepeatCount,
	bool bApplyTransformToFirstInstance,
	bool bDeferChangeNotifications,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendMeshRepeated_InvalidInput1", "AppendMeshRepeated: TargetMesh is Null"));
		return TargetMesh;
	}
	if (AppendMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("AppendMeshRepeated_InvalidInput2", "AppendMeshRepeated: AppendMesh is Null"));
		return TargetMesh;
	}
	if (RepeatCount > 0)
	{
		UE::Geometry::FTransform3d XForm(AppendTransform);
		FDynamicMesh3 TmpMesh;
		AppendMesh->ProcessMesh([&](const FDynamicMesh3& OtherMesh) { TmpMesh.Copy(OtherMesh); });
		if (bApplyTransformToFirstInstance)
		{
			MeshTransforms::ApplyTransform(TmpMesh, XForm);
		}
		TargetMesh->EditMesh([&](FDynamicMesh3& AppendToMesh)
		{
			FMeshIndexMappings TmpMappings;
			FDynamicMeshEditor Editor(&AppendToMesh);
			for (int32 k = 0; k < RepeatCount; ++k)
			{
				Editor.AppendMesh(&TmpMesh, TmpMappings);
				if (k < RepeatCount)
				{
					MeshTransforms::ApplyTransform(TmpMesh, XForm);
					TmpMappings.Reset();
				}
			}
		}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, bDeferChangeNotifications);
	}

	return TargetMesh;
}






#undef LOCTEXT_NAMESPACE
