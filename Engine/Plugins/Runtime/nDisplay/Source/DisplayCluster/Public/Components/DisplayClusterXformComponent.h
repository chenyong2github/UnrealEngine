// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterXformComponent.generated.h"

class UStaticMeshComponent;


/**
 * Xform component
 */
UCLASS(ClassGroup = (DisplayCluster))
class DISPLAYCLUSTER_API UDisplayClusterXformComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterXformComponent(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY(VisibleAnywhere, Category = "DisplayCluster")
	UStaticMeshComponent* VisXformComponent = nullptr;

#if WITH_EDITOR 
public:
	virtual void SetNodeSelection(bool bSelect) override;
#endif
};
