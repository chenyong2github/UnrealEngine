// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/BaseBrushTool.h"

#include "PlacementBrushToolBase.generated.h"

class FRay;
struct FHitResult;
class UAssetPlacementSettings;

UCLASS(Abstract, MinimalAPI)
class UPlacementToolBuilderBase : public UInteractiveToolBuilder
{
	GENERATED_BODY()

public:
	virtual bool CanBuildTool(const FToolBuilderState& SceneState) const override;
	virtual UInteractiveTool* BuildTool(const FToolBuilderState& SceneState) const override;

	UPROPERTY()
	UAssetPlacementSettings* PlacementSettings;

protected:
	virtual UPlacementBrushToolBase* FactoryToolInstance(UObject* Outer) const { check(false); return nullptr; }
};

UCLASS(Abstract, MinimalAPI)
class UPlacementBrushToolBase : public UBaseBrushTool
{
	GENERATED_BODY()

	friend class UPlacementToolBuilderBase;
	
public:
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;

protected:
	virtual double EstimateMaximumTargetDimension() override;

	TWeakObjectPtr<UAssetPlacementSettings> PlacementSettings;
};
