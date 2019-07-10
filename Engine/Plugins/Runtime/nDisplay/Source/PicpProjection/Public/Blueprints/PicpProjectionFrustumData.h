// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PicpProjectionFrustumData.generated.h"


//////////////////////////////////////////////////////////////////////////////////////////////
// Cluster event
//////////////////////////////////////////////////////////////////////////////////////////////
USTRUCT(BlueprintType)
struct FPicpProjectionFrustumData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FString Id;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FRotator ViewRotation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FVector ViewLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PICP")
	FMatrix PrjMatrix;

};
