// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyComponent.h"
#include "WaterBodyOceanComponent.generated.h"

class UOceanCollisionComponent;
class UOceanBoxCollisionComponent;

// ----------------------------------------------------------------------------------

UCLASS(Blueprintable)
class WATER_API UWaterBodyOceanComponent : public UWaterBodyComponent
{
	GENERATED_UCLASS_BODY()

	friend class AWaterBodyOcean;
public:
	/** UWaterBodyComponent Interface */
	virtual EWaterBodyType GetWaterBodyType() const override { return EWaterBodyType::Ocean; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;
	virtual FVector GetCollisionExtents() const override { return CollisionExtents; }
	virtual void SetHeightOffset(float InHeightOffset) override;
	virtual float GetHeightOffset() const override { return HeightOffset; }
protected:
	/** UWaterBodyComponent Interface */
	virtual bool IsBodyDynamic() const override { return true; }
	virtual void BeginUpdateWaterBody() override;
	virtual void OnUpdateBody(bool bWithExclusionVolumes) override;
	virtual void Reset() override;
#if WITH_EDITOR
	virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged) override;
#endif
protected:
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UOceanBoxCollisionComponent*> CollisionBoxes;

	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UOceanCollisionComponent*> CollisionHullSets;

	UPROPERTY(Category = Collision, EditAnywhere, BlueprintReadOnly)
	FVector CollisionExtents;

	UPROPERTY(Transient)
	float HeightOffset = 0.0f;
};