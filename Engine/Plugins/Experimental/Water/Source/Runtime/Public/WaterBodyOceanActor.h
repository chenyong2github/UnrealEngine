// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WaterBodyActor.h"
#include "WaterBodyOceanActor.generated.h"

class UOceanCollisionComponent;
class UOceanBoxCollisionComponent;

// ----------------------------------------------------------------------------------

UCLASS(MinimalAPI)
class UOceanGenerator : public UWaterBodyGenerator
{
	GENERATED_UCLASS_BODY()

public:
	virtual void Reset() override;
	virtual void OnUpdateBody(bool bWithExclusionVolumes) override;
	/** The collision components are dynamic as the water height can change at runtime : */
	virtual bool IsDynamicBody() const override { return true; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;

private:
	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UOceanBoxCollisionComponent*> CollisionBoxes;

	UPROPERTY(NonPIEDuplicateTransient)
	TArray<UOceanCollisionComponent*> CollisionHullSets;
};

// ----------------------------------------------------------------------------------

UCLASS(Blueprintable)
class WATER_API AWaterBodyOcean : public AWaterBody
{
	GENERATED_UCLASS_BODY()

public:
	/** AWaterBody Interface */
	virtual EWaterBodyType GetWaterBodyType() const override { return EWaterBodyType::Ocean; }
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const override;
	virtual FVector GetCollisionExtents() const override { return CollisionExtents; }
	virtual void SetHeightOffset(float InHeightOffset) override;
	virtual float GetHeightOffset() const override { return HeightOffset; }

protected:
	/** AWaterBody Interface */
	virtual void InitializeBody() override;
	virtual bool IsBodyInitialized() const override { return !!OceanGenerator; }
	virtual bool IsBodyDynamic() const override { check(!OceanGenerator || OceanGenerator->IsDynamicBody()); return true; }
	virtual void BeginUpdateWaterBody() override;
	virtual void UpdateWaterBody(bool bWithExclusionVolumes) override;
#if WITH_EDITOR
	virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged) override;
#endif

	UPROPERTY(NonPIEDuplicateTransient)
	UOceanGenerator* OceanGenerator;

	UPROPERTY(Category = Collision, EditAnywhere, BlueprintReadOnly)
	FVector CollisionExtents;

	UPROPERTY(Transient)
	float HeightOffset = 0.0f;
};