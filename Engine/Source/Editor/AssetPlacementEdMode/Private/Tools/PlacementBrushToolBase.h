// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseTools/BaseBrushTool.h"
#include "Elements/Framework/TypedElementHandle.h"

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

protected:
	virtual UPlacementBrushToolBase* FactoryToolInstance(UObject* Outer) const PURE_VIRTUAL(UPlacementToolBuilderBase::FactoryToolInstance, return nullptr; );
};

UCLASS(Abstract, MinimalAPI)
class UPlacementBrushToolBase : public UBaseBrushTool
{
	GENERATED_BODY()

	friend class UPlacementToolBuilderBase;
	
public:
	virtual bool HitTest(const FRay& Ray, FHitResult& OutHit) override;
	virtual bool AreAllTargetsValid() const override;

protected:
	virtual double EstimateMaximumTargetDimension() override;
	bool FindHitResultWithStartAndEndTraceVectors(FHitResult& OutHit, const FVector& TraceStart, const FVector& TraceEnd, float TraceRadius = 0.0f);
	FTransform GetFinalTransformFromHitLocationAndNormal(const FVector& InLocation, const FVector& InNormal);

	// Gets a random rotation, and aligns it, based on the placement settings.
	FRotator GetFinalRotation(const FTransform& InTransform);

	// Updates the last generated rotation to realign with the current brush position and normal.
	FQuat UpdateRotationAlignedToBrushNormal(EAxis::Type InAlignmentAxis, bool bInvertAxis);
	TArray<FTypedElementHandle> GetElementsInBrushRadius() const;

	FQuat LastGeneratedRotation;
	FQuat LastAlignRotation;
};
