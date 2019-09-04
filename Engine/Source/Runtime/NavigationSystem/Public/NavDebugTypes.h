// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"


#if !UE_BUILD_SHIPPING
//#include "DynamicMeshBuilder.h" // for FDynamicMeshVertex
#include "DebugRenderSceneProxy.h"

typedef FDebugRenderSceneProxy::FMesh FNavDebugMeshData;
//struct FNavDebugMeshData
//{
//	TArray<FDynamicMeshVertex> Vertices;
//	TArray<uint32> Indices;
//	FColor Color;
//};
#endif //!UE_BUILD_SHIPPING