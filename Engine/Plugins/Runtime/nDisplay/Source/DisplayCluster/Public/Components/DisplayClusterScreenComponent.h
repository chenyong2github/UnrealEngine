// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterScreenComponent.generated.h"

class UStaticMeshComponent;


/**
 * Simple projection screen component
 */
UCLASS(ClassGroup = (DisplayCluster))
class DISPLAYCLUSTER_API UDisplayClusterScreenComponent
	: public UDisplayClusterSceneComponent
{
	friend class FDisplayClusterProjectionSimplePolicy;

	GENERATED_BODY()

public:
	UDisplayClusterScreenComponent(const FObjectInitializer& ObjectInitializer);

public:
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get screen size"), Category = "DisplayCluster")
	FVector2D GetScreenSize() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set screen size"), Category = "DisplayCluster")
	void SetScreenSize(const FVector2D& Size);

protected:
	virtual void ApplyConfigurationData();

protected:
	UPROPERTY(EditAnywhere, Category = "DisplayCluster")
	FVector2D Size;

	UPROPERTY(VisibleAnywhere, Category = "DisplayCluster")
	UStaticMeshComponent* VisScreenComponent = nullptr;

#if WITH_EDITOR 
public:
	virtual void SetNodeSelection(bool bSelect) override;
#endif
};
