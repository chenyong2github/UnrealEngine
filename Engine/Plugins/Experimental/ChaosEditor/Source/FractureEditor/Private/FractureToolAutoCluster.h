// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "FractureTool.h"
#include "AutoClusterFracture.h"

#include "FractureToolAutoCluster.generated.h"


UCLASS(DisplayName = "Auto Cluster", Category = "FractureTools")
class UFractureAutoClusterSettings : public UFractureToolSettings
{
public:
	GENERATED_BODY()

	UFractureAutoClusterSettings(const FObjectInitializer& ObjInit)
		: Super(ObjInit)
		, AutoClusterMode(EFractureAutoClusterMode::BoundingBox)
		, SiteCount(10)
	{}

	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Mode"))
	EFractureAutoClusterMode AutoClusterMode;

	UPROPERTY(EditAnywhere, Category = AutoCluster, meta = (DisplayName = "Cluster Sites"))// , UIMin = "1", UIMax = "5000", ClampMin = "1"))
	uint32 SiteCount;
};


UCLASS(DisplayName="AutoCluster", Category="FractureTools")
class UFractureToolAutoCluster: public UFractureModalTool
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

	virtual void Execute(TWeakPtr<FFractureEditorModeToolkit> InToolkit) override;

	UPROPERTY(EditAnywhere, Category = AutoCluster)
	UFractureAutoClusterSettings* AutoClusterSettings;
};