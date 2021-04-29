// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterXformComponent.generated.h"

class UStaticMeshComponent;


/**
 * Xform component
 */
UCLASS(ClassGroup = (DisplayCluster), meta = (BlueprintSpawnableComponent))
class DISPLAYCLUSTER_API UDisplayClusterXformComponent
	: public UDisplayClusterSceneComponent
{
	GENERATED_BODY()

public:
	UDisplayClusterXformComponent(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY(transient)
	UStaticMeshComponent* VisXformComponent = nullptr;
	
public:
	virtual void PostInitProperties() override;

#if WITH_EDITOR 
public:
	void SetVisXformScale(float InScale);
	void SetVisXformVisibility(bool bIsVisible);

	virtual void SetNodeSelection(bool bSelect) override;
#endif
};
