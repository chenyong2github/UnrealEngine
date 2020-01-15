// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Control/RigUnit_Control_StaticMesh.h"

FRigUnit_Control_StaticMesh::FRigUnit_Control_StaticMesh()
#if WITH_EDITORONLY_DATA
	: StaticMesh(nullptr)
	, MeshTransform(FTransform::Identity)
#endif
{
}
