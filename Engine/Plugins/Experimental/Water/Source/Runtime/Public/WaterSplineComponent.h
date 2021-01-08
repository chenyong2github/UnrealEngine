// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/SplineComponent.h"
#include "WaterSplineMetadata.h"
#include "WaterSplineComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class WATER_API UWaterSplineComponent : public USplineComponent
{
	GENERATED_UCLASS_BODY()
public:
	/**
	 * Defaults which are used to propagate values to spline points on instances of this in the world
	 */
	UPROPERTY(Category = Water, EditDefaultsOnly)
	FWaterSplineCurveDefaults WaterSplineDefaults;

	/** 
	 * This stores the last defaults propagated to spline points on an instance of this component 
	 *  Used to determine if spline points were modifed by users or if they exist at a current default value
	 */
	UPROPERTY()
	FWaterSplineCurveDefaults PreviousWaterSplineDefaults;
public:
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPie) override;

	/** Spline component interface */
	virtual USplineMetadata* GetSplinePointsMetadata() override;
	virtual const USplineMetadata* GetSplinePointsMetadata() const override;

	virtual TArray<ESplinePointType::Type> GetEnabledSplinePointTypes() const override;

	virtual bool AllowsSplinePointScaleEditing() const override { return false; }

#if WITH_EDITOR
	DECLARE_EVENT(UWaterSplineComponent, FOnSplineDataChanged);
	FOnSplineDataChanged& OnSplineDataChanged() { return SplineDataChangedEvent; }

	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditImport() override;
	
	void ResetSpline(const TArray<FVector>& Points);
	bool SynchronizeWaterProperties();
#endif

	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;
	virtual void Serialize(FArchive& Ar) override;

private:
#if WITH_EDITOR
	FOnSplineDataChanged SplineDataChangedEvent;
#endif
};