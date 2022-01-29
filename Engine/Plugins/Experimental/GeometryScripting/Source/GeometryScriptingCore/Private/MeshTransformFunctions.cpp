// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryScript/MeshTransformFunctions.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/MeshTransforms.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UGeometryScriptLibrary_MeshTransformFunctions"


UDynamicMesh* UGeometryScriptLibrary_MeshTransformFunctions::TransformMesh(
	UDynamicMesh* TargetMesh,
	FTransform Transform,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("TransformMesh_InvalidInput", "TransformMesh: TargetMesh is Null"));
		return TargetMesh;
	}

	// todo: publish correct change types
	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		MeshTransforms::ApplyTransform(EditMesh, (FTransformSRT3d)Transform);

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}



UDynamicMesh* UGeometryScriptLibrary_MeshTransformFunctions::TranslateMesh(
	UDynamicMesh* TargetMesh,
	FVector Translation,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("TranslateMesh_InvalidInput", "TranslateMesh: TargetMesh is Null"));
		return TargetMesh;
	}

	// todo: publish correct change types
	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		MeshTransforms::Translate(EditMesh, (FVector3d)Translation);

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}


UDynamicMesh* UGeometryScriptLibrary_MeshTransformFunctions::ScaleMesh(
	UDynamicMesh* TargetMesh,
	FVector Scale,
	UGeometryScriptDebug* Debug)
{
	if (TargetMesh == nullptr)
	{
		UE::Geometry::AppendError(Debug, EGeometryScriptErrorType::InvalidInputs, LOCTEXT("ScaleMesh_InvalidInput", "ScaleMesh: TargetMesh is Null"));
		return TargetMesh;
	}

	// todo: publish correct change types
	TargetMesh->EditMesh([&](FDynamicMesh3& EditMesh) 
	{
		MeshTransforms::Scale(EditMesh, (FVector3d)Scale, FVector3d::Zero());

	}, EDynamicMeshChangeType::GeneralEdit, EDynamicMeshAttributeChangeFlags::Unknown, false);

	return TargetMesh;
}




#undef LOCTEXT_NAMESPACE
