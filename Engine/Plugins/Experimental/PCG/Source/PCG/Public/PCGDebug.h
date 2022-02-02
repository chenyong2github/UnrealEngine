// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGDebug.generated.h"

class UStaticMesh;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FPCGDebugVisualizationSettings
{
	GENERATED_BODY()

public:
	FPCGDebugVisualizationSettings();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSoftObjectPtr<UStaticMesh> PointMesh;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSoftObjectPtr<UMaterialInterface> Material;
};