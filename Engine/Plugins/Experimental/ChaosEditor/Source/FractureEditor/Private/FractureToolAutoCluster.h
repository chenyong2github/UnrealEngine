// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"
#include "AutoClusterFracture.h"

#include "FractureToolAutoCluster.generated.h"


UCLASS()
class UFractureAutoClusterSettings
	: public UObject
{
	GENERATED_BODY()

public:

	UFractureAutoClusterSettings()
		: AutoClusterMode(EFractureAutoClusterMode::BoundingBox)
		, SiteCount(10)
	{}

	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Mode"))
	EFractureAutoClusterMode AutoClusterMode;

	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Cluster Sites"))// , UIMin = "1", UIMax = "5000", ClampMin = "1"))
	uint32 SiteCount;

};


UCLASS(DisplayName="AutoCluster", Category="FractureTools")
class UFractureToolAutoCluster: public UFractureTool
{
public:
	GENERATED_BODY()

	UFractureToolAutoCluster(const FObjectInitializer& ObjInit);

	// UFractureTool Interface
	virtual FText GetDisplayText() const override;
	virtual FText GetTooltipText() const override;
	virtual FText GetApplyText() const override; 
	virtual FSlateIcon GetToolIcon() const override;
	virtual TArray<UObject*> GetSettingsObjects() const override;

	virtual void RegisterUICommand( FFractureEditorCommands* BindingContext );

	virtual void ExecuteFracture(const FFractureContext& Context) override;

	UPROPERTY(EditAnywhere, Category = AutoCluster)
	UFractureAutoClusterSettings* Settings;

};