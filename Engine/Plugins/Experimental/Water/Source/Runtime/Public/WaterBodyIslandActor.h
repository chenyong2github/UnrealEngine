// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Scene.h"
#include "WaterBrushActorInterface.h"
#include "WaterBodyHeightmapSettings.h"
#include "WaterBodyWeightmapSettings.h"
#include "WaterCurveSettings.h"
#include "WaterBodyIslandActor.generated.h"

class UWaterSplineComponent;
class USplineMeshComponent;
class UBillboardComponent;
class AWaterBody;

UCLASS(Blueprintable)
class WATER_API AWaterBodyIsland : public AActor, public IWaterBrushActorInterface
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin IWaterBrushActorInterface interface
	virtual bool AffectsLandscape() const override { return true; }
	virtual bool AffectsWaterMesh() const override { return false; }
	virtual bool CanAffectWaterMesh() const override { return false; }

#if WITH_EDITOR
	virtual const FWaterCurveSettings& GetWaterCurveSettings() const { return WaterCurveSettings; }
	virtual const FWaterBodyHeightmapSettings& GetWaterHeightmapSettings() const override { return WaterHeightmapSettings; }
	virtual const TMap<FName, FWaterBodyWeightmapSettings>& GetLayerWeightmapSettings() const override { return WaterWeightmapSettings; }
	virtual ETextureRenderTargetFormat GetBrushRenderTargetFormat() const override;
	virtual void GetBrushRenderDependencies(TSet<UObject*>& OutDependencies) const override;
#endif //WITH_EDITOR
	//~ End IWaterBrushActorInterface interface

	UFUNCTION(BlueprintCallable, Category=Water)
	UWaterSplineComponent* GetWaterSpline() const { return SplineComp; }

	void UpdateHeight();

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	
#if WITH_EDITOR
	void UpdateOverlappingWaterBodies();
	void UpdateActorIcon();
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	FWaterCurveSettings WaterCurveSettings;

	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	FWaterBodyHeightmapSettings WaterHeightmapSettings;

	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	TMap<FName, FWaterBodyWeightmapSettings> WaterWeightmapSettings;

	UPROPERTY(Transient)
	UBillboardComponent* ActorIcon;
#endif

protected:
	virtual void Destroyed() override;

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PostEditUndo() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostEditImport() override;
	void UpdateAll();
	void OnSplineDataChanged();
	void OnWaterBodyIslandChanged(bool bShapeOrPositionChanged, bool bWeightmapSettingsChanged);
#endif

protected:
	/**
	 * The spline data attached to this water type.
	 */
	UPROPERTY(VisibleAnywhere, Category = Water, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UWaterSplineComponent* SplineComp;
};
