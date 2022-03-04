// Copyright Epic Games, Inc. All Rights Reserved.

#include "Units/Control/RigUnit_Control_StaticMesh.h"

FRigUnit_Control_StaticMesh::FRigUnit_Control_StaticMesh()
{
}

FRigUnit_Control_StaticMesh_Execute()
{
	FRigUnit_Control::StaticExecute(RigVMExecuteContext, Transform, Base, InitTransform, Result, Filter, Context);
}

FRigVMStructUpgradeInfo FRigUnit_Control_StaticMesh::GetUpgradeInfo() const
{
	// this node is no longer supported
	return FRigVMStructUpgradeInfo();
}