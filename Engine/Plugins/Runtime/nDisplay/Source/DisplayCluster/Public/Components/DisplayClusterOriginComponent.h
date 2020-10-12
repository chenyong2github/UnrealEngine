// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterOriginComponent.generated.h"

class UStaticMeshComponent;


/**
 * Display cluster origin component (for in-Editor visualization)
 */
UCLASS(ClassGroup = (DisplayCluster))
class DISPLAYCLUSTER_API UDisplayClusterOriginComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterOriginComponent(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY(VisibleAnywhere, Category = "DisplayCluster")
	UStaticMeshComponent* VisualizationComponent = nullptr;
};
