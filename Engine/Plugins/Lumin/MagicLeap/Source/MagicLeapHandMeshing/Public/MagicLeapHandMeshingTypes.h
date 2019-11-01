// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/Engine.h"
#include "MagicLeapHandMeshingTypes.generated.h"

/** Stores a hand mesh's vertices and indices. */
USTRUCT(BlueprintType)
struct FMagicLeapHandMeshBlock
{
	GENERATED_USTRUCT_BODY()

	/** The number of indices in index buffer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HandMeshing|MagicLeap")
	int32 IndexCount;

	/** The number of vertices in vertex buffer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HandMeshing|MagicLeap")
	int32 VertexCount;

	/** Pointer to the vertex buffer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HandMeshing|MagicLeap")
	TArray<FVector> Vertex;

	/** Pointer to the index buffer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HandMeshing|MagicLeap")
	TArray<int32> Index;
};

/** Stores MLHandMeshBlock data. */
USTRUCT(BlueprintType)
struct FMagicLeapHandMesh
{
	GENERATED_USTRUCT_BODY()

	/** Version of this structure. */
	UPROPERTY()
	int32 Version;

	/** Number of meshes available in data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HandMeshing|MagicLeap")
	int32 DataCount;

	/** The mesh data */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HandMeshing|MagicLeap")
	TArray<FMagicLeapHandMeshBlock> Data;
};
