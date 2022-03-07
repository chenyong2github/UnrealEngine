// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "PCGDebug.generated.h"

class UStaticMesh;
class UMaterialInterface;

UENUM()
enum class EPCGDebugVisScaleMethod : uint8
{
	Relative,
	Absolute
};

USTRUCT(BlueprintType)
struct PCG_API FPCGDebugVisualizationSettings
{
	GENERATED_BODY()

public:
	FPCGDebugVisualizationSettings();

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings, meta=(ClampMin="0"))
	float PointScale = 1.0f;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	EPCGDebugVisScaleMethod ScaleMethod = EPCGDebugVisScaleMethod::Relative;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSoftObjectPtr<UStaticMesh> PointMesh;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = Settings)
	TSoftObjectPtr<UMaterialInterface> MaterialOverride;

	TSoftObjectPtr<UMaterialInterface> GetMaterial() const;
};
