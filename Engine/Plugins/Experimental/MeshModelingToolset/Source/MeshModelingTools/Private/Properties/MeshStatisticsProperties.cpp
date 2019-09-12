// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Properties/MeshStatisticsProperties.h"

#include "DynamicMesh3.h"
#include "DynamicMeshAttributeSet.h"
#include "MeshNormals.h"

#include "SimpleDynamicMeshComponent.h"

#define LOCTEXT_NAMESPACE "UMeshStatisticsProperites"


void UMeshStatisticsProperties::Update(const FDynamicMesh3& Mesh)
{
	TriangleCount = Mesh.TriangleCount();
	VertexCount = Mesh.VertexCount();
}



#undef LOCTEXT_NAMESPACE
