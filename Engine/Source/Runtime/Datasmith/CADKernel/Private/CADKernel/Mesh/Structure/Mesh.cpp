// Copyright Epic Games, Inc. All Rights Reserved.

#include "CADKernel/Mesh/Structure/Mesh.h"

#include "CADKernel/Mesh/Structure/ModelMesh.h"
#include "CADKernel/Topo/TopologicalEntity.h"

int32 CADKernel::FMesh::RegisterCoordinates()
{
	ModelMesh.RegisterCoordinates(NodeCoordinates, StartNodeId, MeshModelIndex);
	LastNodeIndex = StartNodeId + (int32)NodeCoordinates.Num();
	return StartNodeId;
}


#ifdef CADKERNEL_DEV
CADKernel::FInfoEntity& CADKernel::FMesh::GetInfo(FInfoEntity& Info) const
{
	return FEntityGeom::GetInfo(Info)
		.Add(TEXT("Geometric Entity"), (FEntity&) GetGeometricEntity())
		.Add(TEXT("Mesh model"), (FEntity&) GetMeshModel())
		.Add(TEXT("Node Num"), (int32) NodeCoordinates.Num());
}
#endif
