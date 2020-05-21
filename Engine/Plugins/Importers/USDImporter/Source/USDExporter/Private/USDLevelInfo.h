// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "USDLevelInfo.generated.h"

// This is used by the USDExporter module to dispatch the commands to the Python exporter code
UCLASS(Blueprintable)
class AUSDLevelInfo : public AActor
{
	GENERATED_UCLASS_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = USD, meta = (CallInEditor = "true"))
	void SaveUSD();

	UPROPERTY(EditAnywhere, Category = USD)
	FFilePath FilePath;

	UPROPERTY(EditAnywhere, Category = USD)
	TArray<FFilePath> SubLayerPaths;

	UPROPERTY(EditAnywhere, Category = USD)
	float FileScale;
};

