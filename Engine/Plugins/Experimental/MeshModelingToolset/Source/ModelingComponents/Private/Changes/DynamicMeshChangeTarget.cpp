// Copyright Epic Games, Inc. All Rights Reserved.

#include "Changes/DynamicMeshChangeTarget.h"
#include "DynamicMesh3.h"


void UDynamicMeshReplacementChangeTarget::ApplyChange(const FMeshReplacementChange* Change, bool bRevert)
{
	Mesh = Change->GetMesh(bRevert);
	OnMeshChanged.Broadcast();
}

TUniquePtr<FMeshReplacementChange> UDynamicMeshReplacementChangeTarget::ReplaceMesh(const TSharedPtr<const FDynamicMesh3>& UpdateMesh)
{
	TUniquePtr<FMeshReplacementChange> Change = MakeUnique<FMeshReplacementChange>(Mesh, UpdateMesh);
	Mesh = UpdateMesh;
	return Change;
}