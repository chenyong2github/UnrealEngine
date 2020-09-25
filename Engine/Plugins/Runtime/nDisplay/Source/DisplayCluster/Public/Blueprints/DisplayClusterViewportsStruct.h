// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DisplayClusterViewportsStruct.generated.h"

USTRUCT(BlueprintType)
struct DISPLAYCLUSTER_API FDisplayClusterViewportsStruct
{
	GENERATED_BODY()

public:

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DisplayCluster|Cluster")
	TArray<FString> ViewportIds;
};

