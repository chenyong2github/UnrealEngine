// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DisplayClusterSceneComponent.h"
#include "DisplayClusterScreenComponent.generated.h"

class UStaticMeshComponent;


/**
 * Simple projection screen component
 */
UCLASS(ClassGroup = (DisplayCluster), meta = (BlueprintSpawnableComponent))
class DISPLAYCLUSTER_API UDisplayClusterScreenComponent
	: public UDisplayClusterSceneComponent
{
	friend class FDisplayClusterProjectionSimplePolicy;

	GENERATED_BODY()

public:
	UDisplayClusterScreenComponent(const FObjectInitializer& ObjectInitializer);

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	
public:
	/** Return the screen size adjusted by its transform scale. */
	UFUNCTION(BlueprintCallable, Category = "DisplayCluster")
	FVector2D GetScreenSizeScaled() const;
	
	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get screen size"), Category = "DisplayCluster")
	FVector2D GetScreenSize() const;

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Set screen size"), Category = "DisplayCluster")
	void SetScreenSize(const FVector2D& Size);

protected:
	virtual void ApplyConfigurationData() override;
	
protected:
#if WITH_EDITORONLY_DATA
	friend class FDisplayClusterConfiguratorScreenDetailCustomization;

	/** Adjust the size of the screen. */
	UPROPERTY(EditAnywhere, Category = "DisplayCluster", meta = (DisplayName = "Size", AllowPreserveRatio))
	FVector2D SizeCm;
#endif

	/** Automatically updated in the editor by SizeCm. */
	UPROPERTY()
	FVector2D Size;

	UPROPERTY(Instanced, DuplicateTransient)
	UStaticMeshComponent* VisScreenComponent;

#if WITH_EDITOR 
public:
	virtual void SetNodeSelection(bool bSelect) override;
#endif
};
