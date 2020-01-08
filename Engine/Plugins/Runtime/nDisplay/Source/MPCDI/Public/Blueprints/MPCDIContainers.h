// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "TextureResource.h"

#include "MPCDIContainers.generated.h"

USTRUCT(BlueprintType, Category = "MPCDI")
struct FMPCDIGeometryImportData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	int Width;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	int Height;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	TArray<FVector>   Vertices;
};


USTRUCT(BlueprintType, Category = "MPCDI")
struct FMPCDIGeometryExportData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	TArray<FVector>   Vertices;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	TArray<FVector>   Normal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	TArray<FVector2D> UV;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	TArray<int32>   Triangles;

	void PostAddFace(int f0, int f1, int f2);
};


//////////////////////////////////////////////////////////////////////////////////////////////
// Blend inputs
//////////////////////////////////////////////////////////////////////////////////////////////
/*
USTRUCT(BlueprintType)
struct FMPCDIRegionGeometry
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	TArray<FVector> Points;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	TArray<FVector> UV;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	TArray<> UV;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MPCDI")
	ECameraOverlayRenderMode OverlayBlendMode;
};
*/