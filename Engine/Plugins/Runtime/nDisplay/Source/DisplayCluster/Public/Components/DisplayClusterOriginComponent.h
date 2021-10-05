// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterOriginComponent.generated.h"

class UStaticMeshComponent;


/**
 * Display cluster origin component (for in-Editor visualization)
 */
UCLASS(ClassGroup = (DisplayCluster), meta = (BlueprintSpawnableComponent, DisplayName="NDisplay Origin"))
class DISPLAYCLUSTER_API UDisplayClusterOriginComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterOriginComponent(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY()
	UStaticMeshComponent* VisualizationComponent = nullptr;
};
