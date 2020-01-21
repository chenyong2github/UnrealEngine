// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DatasmithCustomAction.h"
#include "DatasmithAdditionalData.h"
#include "DatasmithImportOptions.h"

#include "CoreTechParametricSurfaceExtension.generated.h"


USTRUCT(BlueprintType)
struct DATASMITHCORETECHPARAMETRICSURFACEDATA_API FCoreTechSceneParameters
{
	GENERATED_BODY()

	// value from FDatasmithUtils::EModelCoordSystem
	UPROPERTY()
	uint8 ModelCoordSys;

	UPROPERTY()
	float MetricUnit;

	UPROPERTY()
	float ScaleFactor;
};

USTRUCT()
struct DATASMITHCORETECHPARAMETRICSURFACEDATA_API FCoreTechMeshParameters
{
	GENERATED_BODY()

	UPROPERTY()
	bool bNeedSwapOrientation;

	UPROPERTY()
	bool bIsSymmetric;

	UPROPERTY()
	FVector SymmetricOrigin;

	UPROPERTY()
	FVector SymmetricNormal;
};


UCLASS()
class DATASMITHCORETECHPARAMETRICSURFACEDATA_API UCoreTechParametricSurfaceData : public UDatasmithAdditionalData
{
	GENERATED_BODY()

public:
	UPROPERTY()
	FString SourceFile;

	UPROPERTY()
	TArray<uint8> RawData;

	UPROPERTY()
	FCoreTechSceneParameters SceneParameters;

	UPROPERTY()
	FCoreTechMeshParameters MeshParameters;

	UPROPERTY(EditAnywhere, Category=NURBS)
	FDatasmithTessellationOptions LastTessellationOptions;
};