// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterXformComponent.generated.h"


/**
 * Xform component
 */
UCLASS( ClassGroup=(Custom), Blueprintable, meta = (BlueprintSpawnableComponent) )
class DISPLAYCLUSTER_API UDisplayClusterXformComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterXformComponent(const FObjectInitializer& ObjectInitializer);

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
