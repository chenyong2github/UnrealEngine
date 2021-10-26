// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/Scene.h"
#include "TerrainCarvingSettings.h"
#include "WaterBrushActorInterface.h"
#include "WaterBodyWeightmapSettings.h"
#include "WaterBodyHeightmapSettings.h"
#include "WaterCurveSettings.h"
#include "WaterSplineMetadata.h"
#include "NavAreas/NavArea.h"
#include "AI/Navigation/NavRelevantInterface.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "WaterBodyTypes.h"
#include "WaterWaves.h"
#include "WaterBodyComponent.h"
#include "WaterBodyActor.generated.h"

class UWaterSplineComponent;
class AWaterBodyIsland;
class AWaterBodyExclusionVolume;
class ALandscapeProxy;
class UMaterialInstanceDynamic;

// ----------------------------------------------------------------------------------

// For internal use.
UCLASS(Abstract, Deprecated, Within = WaterBody)
class WATER_API UDEPRECATED_WaterBodyGenerator : public UObject
{
	GENERATED_UCLASS_BODY()
};

// ----------------------------------------------------------------------------------

//@todo_water: Remove Blueprintable
UCLASS(Blueprintable, Abstract, HideCategories = (Tags, Activation, Cooking, Replication, Input, Actor, AssetUserData))
class WATER_API AWaterBody : public AActor, public IWaterBrushActorInterface
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin IWaterBrushActorInterface interface
	virtual bool AffectsLandscape() const override { return WaterBodyComponent->AffectsLandscape(); }
	virtual bool AffectsWaterMesh() const override { return WaterBodyComponent->AffectsWaterMesh(); }
	virtual bool CanAffectWaterMesh() const override { return WaterBodyComponent->CanAffectWaterMesh(); }

#if WITH_EDITOR
	virtual const FWaterCurveSettings& GetWaterCurveSettings() const override { return WaterBodyComponent->GetWaterCurveSettings(); }
	virtual const FWaterBodyHeightmapSettings& GetWaterHeightmapSettings() const override { return WaterBodyComponent->GetWaterHeightmapSettings(); }
	virtual const TMap<FName, FWaterBodyWeightmapSettings>& GetLayerWeightmapSettings() const override { return WaterBodyComponent->GetLayerWeightmapSettings(); }
	virtual ETextureRenderTargetFormat GetBrushRenderTargetFormat() const override { return WaterBodyComponent->GetBrushRenderTargetFormat(); }
	virtual void GetBrushRenderDependencies(TSet<UObject*>& OutDependencies) const override { return WaterBodyComponent->GetBrushRenderDependencies(OutDependencies); }
	virtual TArray<UPrimitiveComponent*> GetBrushRenderableComponents() const override { return WaterBodyComponent->GetBrushRenderableComponents(); }
#endif //WITH_EDITOR
	//~ End IWaterBrushActorInterface interface

	/** Actor Interface */
	virtual void NotifyActorBeginOverlap(AActor* OtherActor) override;
	virtual void NotifyActorEndOverlap(AActor* OtherActor) override;
	virtual void PreRegisterAllComponents() override;
	virtual void UnregisterAllComponents(bool bForReregister = false) override;
	virtual void PreInitializeComponents() override;
	virtual void PostInitProperties() override;
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;

#if WITH_EDITOR
	virtual void SetActorHiddenInGame(bool bNewHidden) override;
	virtual void SetIsTemporarilyHiddenInEditor(bool bIsHidden) override;
	virtual bool SetIsHiddenEdLayer(bool bIsHiddenEdLayer) override;
#endif // WITH_EDITOR
	
	/** Returns the type of body */
	UFUNCTION(BlueprintCallable, Category=Water)
	virtual EWaterBodyType GetWaterBodyType() const { return IsTemplate() ? WaterBodyType : GetClass()->GetDefaultObject<AWaterBody>()->WaterBodyType; }
	
	/** Returns water spline component */
	UFUNCTION(BlueprintCallable, Category=Water)
	UWaterSplineComponent* GetWaterSpline() const { return SplineComp; }

	UWaterSplineMetadata* GetWaterSplineMetadata() { return WaterSplineMetadata; }

	const UWaterSplineMetadata* GetWaterSplineMetadata() const { return WaterSplineMetadata; }

	UFUNCTION(BlueprintCallable, Category = Wave)
	void SetWaterWaves(UWaterWavesBase* InWaterWaves);
	UWaterWavesBase* GetWaterWaves() const { return WaterWaves; }

	/** Returns the water body component */
	UFUNCTION(BlueprintCallable, Category=Water)
	UWaterBodyComponent* GetWaterBodyComponent() const { return WaterBodyComponent; }

#if WITH_EDITOR
	// #todo_water: all icon stuff can be moved to the component
	void UpdateActorIcon();
#endif // WITH_EDITOR
protected:
	/** Initializes the water body by creating the respective component for this water body type. */
	virtual void InitializeBody();

	virtual void DeprecateData();

	/** The spline data attached to this water type. */
	UPROPERTY(Category = Water, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UWaterSplineComponent* SplineComp;

	UPROPERTY(Instanced)
	UWaterSplineMetadata* WaterSplineMetadata;

	UPROPERTY(Category = Water, VisibleAnywhere, BlueprintReadOnly, meta = (ExposeFunctionCategories = "Water,Wave,Rendering,Terrain,Navigation,Physics,Collision,Debug", AllowPrivateAccess = "true"))
	UWaterBodyComponent* WaterBodyComponent;

	/** Unique Id for accessing (wave, ... ) data in GPU buffers */
	UPROPERTY(Transient, DuplicateTransient, NonTransactional, BlueprintReadOnly, Category = Water, meta = (AllowPrivateAccess = "true"))
	int32 WaterBodyIndex = INDEX_NONE;
	
	UPROPERTY(Category = Water, EditDefaultsOnly, meta = (AllowPrivateAccess = "true"))
	EWaterBodyType WaterBodyType;

	// #todo_water: This should be moved to the component when component subobjects are supported
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = Wave, DisplayName = "Waves Source")
	UWaterWavesBase* WaterWaves = nullptr;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	UBillboardComponent* ActorIcon;
#endif

#if WITH_EDITOR
	virtual void PostEditMove(bool bFinished) override;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	/** Returns whether icon billboard is visible. */
	virtual bool IsIconVisible() const;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
#endif // WITH_EDITOR

	void SetWaterWavesInternal(UWaterWavesBase* InWaterWaves);

// ----------------------------------------------------------------------------------
// Deprecated

public:
	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual bool HasWaves() const final { return WaterBodyComponent->HasWaves(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual bool IsWaveSupported() const final { return WaterBodyComponent->IsWaveSupported(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const final { return WaterBodyComponent->GetCollisionComponents(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual FVector GetCollisionExtents() const final { return WaterBodyComponent->GetCollisionExtents(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual void SetHeightOffset(float InHeightOffset) final { WaterBodyComponent->SetHeightOffset(InHeightOffset); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual float GetHeightOffset() const final { return WaterBodyComponent->GetHeightOffset(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	void SetWaterMeshOverride(UStaticMesh* InMesh) { WaterBodyComponent->SetWaterMeshOverride(InMesh); }

	UFUNCTION(BlueprintCallable, Category = Rendering, meta = (DeprecatedFunction))
	virtual UMaterialInstanceDynamic* GetRiverToLakeTransitionMaterialInstance() final { return WaterBodyComponent->GetRiverToLakeTransitionMaterialInstance(); }

	UFUNCTION(BlueprintCallable, Category = Rendering, meta = (DeprecatedFunction))
	virtual UMaterialInstanceDynamic* GetRiverToOceanTransitionMaterialInstance() final { return WaterBodyComponent->GetRiverToOceanTransitionMaterialInstance(); }

	UFUNCTION(BlueprintCallable, Category = Rendering, meta = (DeprecatedFunction))
	void SetWaterMaterial(UMaterialInterface* InMaterial) { WaterBodyComponent->SetWaterMaterial(InMaterial); }

	UFUNCTION(BlueprintCallable, Category = Rendering, meta = (DeprecatedFunction))
	UMaterialInstanceDynamic* GetWaterMaterialInstance() { return WaterBodyComponent->GetWaterMaterialInstance(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	void SetUnderwaterPostProcessMaterial(UMaterialInterface* InMaterial) { WaterBodyComponent->SetUnderwaterPostProcessMaterial(InMaterial); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	bool ShouldGenerateWaterMeshTile() const { return WaterBodyComponent->ShouldGenerateWaterMeshTile(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	FVector GetWaterNavCollisionOffset() const { return WaterBodyComponent->GetWaterNavCollisionOffset(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	int32 GetOverlapMaterialPriority() const { return WaterBodyComponent->OverlapMaterialPriority; }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	float GetChannelDepth() const { return WaterBodyComponent->CurveSettings.ChannelDepth; }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	void AddExclusionVolume(AWaterBodyExclusionVolume* InExclusionVolume) { WaterBodyComponent->AddExclusionVolume(InExclusionVolume); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	void RemoveExclusionVolume(AWaterBodyExclusionVolume* InExclusionVolume) { WaterBodyComponent->RemoveExclusionVolume(InExclusionVolume); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	FPostProcessVolumeProperties GetPostProcessProperties() const { return WaterBodyComponent->GetPostProcessProperties(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual FWaterBodyQueryResult QueryWaterInfoClosestToWorldLocation(const FVector& InWorldLocation, EWaterBodyQueryFlags InQueryFlags, const TOptional<float>& InSplineInputKey = TOptional<float>()) const
	{
		return WaterBodyComponent->QueryWaterInfoClosestToWorldLocation(InWorldLocation, InQueryFlags, InSplineInputKey);
	}

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	float FindInputKeyClosestToWorldLocation(const FVector& WorldLocation) const { return WaterBodyComponent->FindInputKeyClosestToWorldLocation(WorldLocation); }

	UFUNCTION(BlueprintCallable, Category = WaterBody, meta = (DeprecatedFunction))
	virtual float GetWaterVelocityAtSplineInputKey(float InKey) const { return WaterBodyComponent->GetWaterVelocityAtSplineInputKey(InKey); }
	
	UFUNCTION(BlueprintCallable, Category = WaterBody, meta = (DeprecatedFunction))
	virtual FVector GetWaterVelocityVectorAtSplineInputKey(float InKey) const { return WaterBodyComponent->GetWaterVelocityVectorAtSplineInputKey(InKey); }
	
	UFUNCTION(BlueprintCallable, Category = WaterBody, meta = (DeprecatedFunction))
	virtual float GetAudioIntensityAtSplineInputKey(float InKey) const { return WaterBodyComponent->GetAudioIntensityAtSplineInputKey(InKey); }

	UFUNCTION(BlueprintCallable, Category = Water, meta = (DeprecatedFunction))
	TArray<AWaterBodyIsland*> GetIslands() const { return WaterBodyComponent->GetIslands(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	bool ContainsIsland(TLazyObjectPtr<AWaterBodyIsland> Island) const { return WaterBodyComponent->ContainsIsland(Island); }

	UFUNCTION(BlueprintCallable, Category = Water, meta = (DeprecatedFunction))
	TArray<AWaterBodyExclusionVolume*> GetExclusionVolumes() const { return WaterBodyComponent->GetExclusionVolumes(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	bool ContainsExclusionVolume(TLazyObjectPtr<AWaterBodyExclusionVolume> InExclusionVolume) const { return WaterBodyComponent->ContainsExclusionVolume(InExclusionVolume); }

	UFUNCTION(BlueprintCallable, Category=Water, meta = (DeprecatedFunction))
	void OnWaterBodyChanged(bool bShapeOrPositionChanged, bool bWeightmapSettingsChanged = false) { return WaterBodyComponent->OnWaterBodyChanged(bShapeOrPositionChanged, bWeightmapSettingsChanged); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	bool GetWaveInfoAtPosition(const FVector& InPosition, float InWaterDepth, bool bInSimpleWaves, FWaveInfo& InOutWaveInfo) const
	{
		return WaterBodyComponent->GetWaveInfoAtPosition(InPosition, InWaterDepth, bInSimpleWaves, InOutWaveInfo);
	}

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual void SetDynamicParametersOnMID(UMaterialInstanceDynamic* InMID) final { WaterBodyComponent->SetDynamicParametersOnMID(InMID); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	bool IsWorldLocationInExclusionVolume(const FVector& InWorldLocation) const { return WaterBodyComponent->IsWorldLocationInExclusionVolume(InWorldLocation); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual void UpdateMaterialInstances() final {}

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual float GetWaveReferenceTime() const final { return WaterBodyComponent->GetWaveReferenceTime(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual ALandscapeProxy* FindLandscape() const final { return WaterBodyComponent->FindLandscape(); }

protected:
	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual bool IsBodyDynamic() const { return WaterBodyComponent->IsBodyDynamic(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual bool IsFlatSurface() const { return WaterBodyComponent->IsFlatSurface(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual bool IsWaterSplineClosedLoop() const final { return WaterBodyComponent->IsWaterSplineClosedLoop(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual bool IsHeightOffsetSupported() const final { return WaterBodyComponent->IsHeightOffsetSupported(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual bool CanAffectNavigation() const final { return WaterBodyComponent->CanAffectNavigation(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual void BeginUpdateWaterBody() final {}

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual void UpdateWaterBody(bool bWithExclusionVolumes) final { WaterBodyComponent->UpdateWaterBody(bWithExclusionVolumes); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual float GetConstantSurfaceZ() const final { return WaterBodyComponent->GetConstantSurfaceZ(); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual float GetConstantDepth() const final { return WaterBodyComponent->GetConstantDepth(); };

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual void GetSurfaceMinMaxZ(float& OutMinZ, float& OutMaxZ) const final { return WaterBodyComponent->GetSurfaceMinMaxZ(OutMinZ, OutMaxZ); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual float GetWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime, FVector& OutNormal) const final { return WaterBodyComponent->GetWaveHeightAtPosition(InPosition, InWaterDepth, InTime, OutNormal); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual float GetSimpleWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime) const final { return WaterBodyComponent->GetSimpleWaveHeightAtPosition(InPosition, InWaterDepth, InTime); }

	UE_DEPRECATED(4.27, "Moved to WaterBodyComponent")
	virtual float GetWaveAttenuationFactor(const FVector& InPosition, float InWaterDepth) const final { return WaterBodyComponent->GetWaveAttenuationFactor(InPosition, InWaterDepth); }

	friend class UWaterBodyComponent;
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UPhysicalMaterial* PhysicalMaterial_DEPRECATED;

	UPROPERTY()
	float TargetWaveMaskDepth_DEPRECATED;

	UPROPERTY()
	float MaxWaveHeightOffset_DEPRECATED = 0.f;

	UPROPERTY()
	bool bFillCollisionUnderWaterBodiesForNavmesh_DEPRECATED;

	UPROPERTY()
	FUnderwaterPostProcessSettings UnderwaterPostProcessSettings_DEPRECATED;

	UPROPERTY()
	FWaterCurveSettings CurveSettings_DEPRECATED;

	UPROPERTY()
	UMaterialInterface* WaterMaterial_DEPRECATED;

	UPROPERTY()
	UMaterialInterface* UnderwaterPostProcessMaterial_DEPRECATED;

	UPROPERTY()
	FLandmassTerrainCarvingSettings TerrainCarvingSettings_DEPRECATED;

	UPROPERTY()
	FWaterBodyHeightmapSettings WaterHeightmapSettings_DEPRECATED;

	UPROPERTY()
	TMap<FName, FWaterBodyWeightmapSettings> LayerWeightmapSettings_DEPRECATED;

	UPROPERTY()
	bool bAffectsLandscape_DEPRECATED;
	
	UPROPERTY()
	bool bGenerateCollisions_DEPRECATED = true;

	UPROPERTY()
	bool bOverrideWaterMesh_DEPRECATED;

	UPROPERTY()
	UStaticMesh* WaterMeshOverride_DEPRECATED;

	UPROPERTY()
	int32 OverlapMaterialPriority_DEPRECATED = 0;

	UPROPERTY()
	FName CollisionProfileName_DEPRECATED;

	UPROPERTY()
	UMaterialInstanceDynamic* WaterMID_DEPRECATED;

	UPROPERTY()
	UMaterialInstanceDynamic* UnderwaterPostProcessMID_DEPRECATED;

	UPROPERTY()
	TArray<TLazyObjectPtr<AWaterBodyIsland>> Islands_DEPRECATED;

	UPROPERTY()
	TArray<TLazyObjectPtr<AWaterBodyExclusionVolume>> ExclusionVolumes_DEPRECATED;

	UPROPERTY()
	bool bCanAffectNavigation_DEPRECATED;

	UPROPERTY()
	TSubclassOf<UNavAreaBase> WaterNavAreaClass_DEPRECATED;

	UPROPERTY()
	float ShapeDilation_DEPRECATED = 4096.0f;
#endif
};
