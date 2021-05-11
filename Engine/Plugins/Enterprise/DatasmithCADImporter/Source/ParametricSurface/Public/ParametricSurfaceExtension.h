// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithAdditionalData.h"
#include "DatasmithCustomAction.h"
#include "DatasmithImportOptions.h"
#include "DatasmithUtils.h"

#include "ParametricSurfaceExtension.generated.h"


USTRUCT(BlueprintType)
struct PARAMETRICSURFACE_API FParametricSceneParameters
{
	GENERATED_BODY()

	// value from FDatasmithUtils::EModelCoordSystem
	UPROPERTY()
	uint8 ModelCoordSys = (uint8)FDatasmithUtils::EModelCoordSystem::ZUp_LeftHanded;

	UPROPERTY()
	float MetricUnit = 0.01f;

	UPROPERTY()
	float ScaleFactor = 1.0f;
};

USTRUCT()
struct PARAMETRICSURFACE_API FParametricMeshParameters
{
	GENERATED_BODY()

	UPROPERTY()
	bool bNeedSwapOrientation = false;

	UPROPERTY()
	bool bIsSymmetric = false;

	UPROPERTY()
	FVector SymmetricOrigin = FVector::ZeroVector;

	UPROPERTY()
	FVector SymmetricNormal = FVector::ZeroVector;
};

UCLASS(meta = (DisplayName = "Parametric Surface Data"))
class PARAMETRICSURFACE_API UParametricSurfaceData : public UDatasmithAdditionalData
{
	GENERATED_BODY()

public:

	UPROPERTY()
	FParametricSceneParameters SceneParameters;

	UPROPERTY()
	FParametricMeshParameters MeshParameters;

	UPROPERTY(EditAnywhere, Category=NURBS)
	FDatasmithTessellationOptions LastTessellationOptions;

	virtual bool IsValid()
	{
		return false;
	}

	virtual bool Tessellate(UStaticMesh& StaticMesh, const FDatasmithRetessellationOptions& RetessellateOptions)
	{
		return false;
	}

};

