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
#include "WaterWaves.h"
#include "WaterBodyTypes.h"
#include "WaterBodyComponent.generated.h"

class UWaterSplineComponent;
class AWaterBodyIsland;
class AWaterBodyExclusionVolume;
class ALandscapeProxy;
class UMaterialInstanceDynamic;
class FTokenizedMessage;

// ----------------------------------------------------------------------------------

USTRUCT(BlueprintType)
struct FUnderwaterPostProcessSettings
{
	GENERATED_BODY()

	FUnderwaterPostProcessSettings()
		: bEnabled(true)
		, Priority(0)
		, BlendRadius(100.f)
		, BlendWeight(1.0f)
		, UnderwaterPostProcessMaterial_DEPRECATED(nullptr)
	{}
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rendering)
	bool bEnabled;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rendering)
	float Priority;

	/** World space radius around the volume that is used for blending (only if not unbound).			*/
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rendering, meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "6000.0"))
	float BlendRadius;

	/** 0:no effect, 1:full effect */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rendering, meta = (UIMin = "0.0", UIMax = "1.0"))
	float BlendWeight;

	/** List of all post-process settings to use when underwater : note : use UnderwaterPostProcessMaterial for setting the actual post process material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Rendering)
	FPostProcessSettings PostProcessSettings;

	/** This is the parent post process material for the PostProcessSettings */
	UPROPERTY()
	UMaterialInterface* UnderwaterPostProcessMaterial_DEPRECATED;
};

// ----------------------------------------------------------------------------------

enum class EWaterBodyStatus : uint8
{
	Valid,
	MissingWaterZone,
	MissingLandscape,
	InvalidWaveData,
};

// ----------------------------------------------------------------------------------

UCLASS(Abstract, HideCategories = (Tags, Activation, Cooking, Replication, Input, AssetUserData, Mesh))
class WATER_API UWaterBodyComponent : public UPrimitiveComponent
{
	GENERATED_UCLASS_BODY()

	friend class AWaterBody;
public:
	virtual bool AffectsLandscape() const;
	virtual bool AffectsWaterMesh() const;
	virtual bool CanAffectWaterMesh() const { return false; }

	void UpdateAll(bool bShapeOrPositionChanged);

#if WITH_EDITOR
	const FWaterCurveSettings& GetWaterCurveSettings() const { return CurveSettings; }
	const FWaterBodyHeightmapSettings& GetWaterHeightmapSettings() const { return WaterHeightmapSettings; }
	const TMap<FName, FWaterBodyWeightmapSettings>& GetLayerWeightmapSettings() const { return LayerWeightmapSettings; }
	virtual ETextureRenderTargetFormat GetBrushRenderTargetFormat() const;
	virtual void GetBrushRenderDependencies(TSet<UObject*>& OutDependencies) const;
	virtual TArray<UPrimitiveComponent*> GetBrushRenderableComponents() const { return TArray<UPrimitiveComponent*>(); }
#endif //WITH_EDITOR

	/** Returns whether the body supports waves */
	virtual bool IsWaveSupported() const;

	/** Returns true if there are valid water waves */
	bool HasWaves() const;

	/** Returns whether the body is baked (false) at save-time or needs to be dynamically regenerated at runtime (true) and is therefore transient. */
	virtual bool IsBodyDynamic() const { return false; }
	
	/** Returns body's collision components */
	UFUNCTION(BlueprintCallable, Category = Collision)
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const { return TArray<UPrimitiveComponent*>(); }

	/** Retrieves the list of primitive components that this water body uses when not being rendered by the water mesh (e.g. the static mesh component used when WaterMeshOverride is specified) */
	UFUNCTION(BlueprintCallable, Category = Rendering)
	virtual TArray<UPrimitiveComponent*> GetStandardRenderableComponents() const { return TArray<UPrimitiveComponent*>(); }

	/** Returns the body's collision component bounds */
	virtual FBox GetCollisionComponentBounds() const;

	/** Returns the type of body */
	virtual EWaterBodyType GetWaterBodyType() const PURE_VIRTUAL(UWaterBodyComponent::GetWaterBodyType, return EWaterBodyType::Transition; )

	/** Returns collision extents (For internal use. Please use AWaterBodyOcean instead.) */
	virtual FVector GetCollisionExtents() const { return FVector::ZeroVector; }

	/** Sets an additional water height (For internal use. Please use AWaterBodyOcean instead.) */
	virtual void SetHeightOffset(float InHeightOffset) { check(false); }

	/** Returns the additional water height added to the body (For internal use. Please use AWaterBodyOcean instead.) */
	virtual float GetHeightOffset() const { return 0.f; }

	/** Sets a static mesh to use as a replacement for the water mesh (for water bodies that are being rendered by the water mesh) */
	void SetWaterMeshOverride(UStaticMesh* InMesh) { WaterMeshOverride = InMesh; }

	/** Returns River to lake transition material instance (For internal use. Please use AWaterBodyRiver instead.) */
	UFUNCTION(BlueprintCallable, Category = Rendering)
	virtual UMaterialInstanceDynamic* GetRiverToLakeTransitionMaterialInstance() { return nullptr; }

	/** Returns River to ocean transition material instance (For internal use. Please use AWaterBodyRiver instead.) */
	UFUNCTION(BlueprintCallable, Category = Rendering)
	virtual UMaterialInstanceDynamic* GetRiverToOceanTransitionMaterialInstance() { return nullptr; }

	/** Returns the WaterBodyActor who owns this component */
	UFUNCTION(BlueprintCallable, Category = Water)
	AWaterBody* GetWaterBodyActor() const;
	
	/** Returns water spline component */
	UFUNCTION(BlueprintCallable, Category = Water)
	UWaterSplineComponent* GetWaterSpline() const;

	UFUNCTION(BlueprintCallable, Category = Water)
	UWaterWavesBase* GetWaterWaves() const;

	/** Returns the unique id of this water body for accessing data in GPU buffers */
	int32 GetWaterBodyIndex() const { return WaterBodyIndex; }
	
	/** Returns collision profile name */
	FName GetCollisionProfileName() const { return CollisionProfileName; }

	/** Returns water mesh override */
	UStaticMesh* GetWaterMeshOverride() const { return WaterMeshOverride; }

	/** Returns water material */
	UFUNCTION(BlueprintCallable, Category = Rendering)
	UMaterialInterface* GetWaterMaterial() const { return WaterMaterial; }

	/** Sets water material */
	void SetWaterMaterial(UMaterialInterface* InMaterial);

	/** Returns water MID */
	UFUNCTION(BlueprintCallable, Category = Rendering)
	UMaterialInstanceDynamic* GetWaterMaterialInstance();

	/** Returns under water post process MID */
	UFUNCTION(BlueprintCallable, Category = Rendering)
	UMaterialInstanceDynamic* GetUnderwaterPostProcessMaterialInstance();

	/** Sets under water post process material */
	void SetUnderwaterPostProcessMaterial(UMaterialInterface* InMaterial);

	/** Returns water spline metadata */
	UWaterSplineMetadata* GetWaterSplineMetadata() { return WaterSplineMetadata; }

	/** Returns water spline metadata */
	const UWaterSplineMetadata* GetWaterSplineMetadata() const { return WaterSplineMetadata; }

	/** Is this water body rendered with the WaterMeshComponent, with the quadtree-based water renderer? */
	bool ShouldGenerateWaterMeshTile() const;

	/** Returns nav collision offset */
	FVector GetWaterNavCollisionOffset() const { return FVector(0.0f, 0.0f, -GetMaxWaveHeight()); }

	/** Returns overlap material priority */
	int32 GetOverlapMaterialPriority() const { return OverlapMaterialPriority; }

	/** Returns channel depth */
	float GetChannelDepth() const { return CurveSettings.ChannelDepth; }

	void AddIsland(AWaterBodyIsland* Island);
	void RemoveIsland(AWaterBodyIsland* Island);
	void UpdateIslands();

	/** Adds WaterBody exclusion volume */
	void AddExclusionVolume(AWaterBodyExclusionVolume* InExclusionVolume);

	/** Removes WaterBody exclusion volume */
	void RemoveExclusionVolume(AWaterBodyExclusionVolume* InExclusionVolume);

	/** Returns post process properties */
	FPostProcessVolumeProperties GetPostProcessProperties() const;

	/** Returns the requested water info closest to this world location
	- InWorldLocation: world-space location closest to which the function returns the water info
	- InQueryFlags: flags to indicate which info is to be computed
	- InSplineInputKey: (optional) location on the spline, in case it has already been computed.
	*/
	virtual FWaterBodyQueryResult QueryWaterInfoClosestToWorldLocation(const FVector& InWorldLocation, EWaterBodyQueryFlags InQueryFlags, const TOptional<float>& InSplineInputKey = TOptional<float>()) const;

	UFUNCTION(BlueprintCallable, Category = WaterBody)
	void GetWaterSurfaceInfoAtLocation(const FVector& InLocation, FVector& OutWaterSurfaceLocation, FVector& OutWaterSurfaceNormal, FVector& OutWaterVelocity, float& OutWaterDepth, bool bIncludeDepth = false) const;

	/** Spline query helper. It's faster to get the spline key once then query properties using that key, rather than querying repeatedly by location etc. */
	float FindInputKeyClosestToWorldLocation(const FVector& WorldLocation) const;

	/*
	 * Spline queries specific to metadata type
	 */
	UFUNCTION(BlueprintCallable, Category = WaterBody)
	virtual float GetWaterVelocityAtSplineInputKey(float InKey) const;
	virtual FVector GetWaterVelocityVectorAtSplineInputKey(float InKey) const;
	virtual float GetAudioIntensityAtSplineInputKey(float InKey) const;

	/**
	 * Gets the islands that influence this water body
	 */
	UFUNCTION(BlueprintCallable, Category = Water)
	TArray<AWaterBodyIsland*> GetIslands() const;

	bool ContainsIsland(TLazyObjectPtr<AWaterBodyIsland> Island) const { return Islands.Contains(Island); }

	/**
	 * Gets the exclusion volume that influence this water body
	 */
	UFUNCTION(BlueprintCallable, Category = Water)
	TArray<AWaterBodyExclusionVolume*> GetExclusionVolumes() const;

	bool ContainsExclusionVolume(TLazyObjectPtr<AWaterBodyExclusionVolume> InExclusionVolume) const { return ExclusionVolumes.Contains(InExclusionVolume); }

	/** Component interface */
	virtual void OnRegister() override;
	virtual void PostDuplicate(bool bDuplicateForPie) override;

	UFUNCTION(BlueprintCallable, Category=Water)
	void OnWaterBodyChanged(bool bShapeOrPositionChanged, bool bWeightmapSettingsChanged = false);

	/** Fills wave-related information at the given world position and for this water depth.
	 - InPosition : water surface position at which to query the wave information
	 - InWaterDepth : water depth at this location
	 - bSimpleWaves : true for the simple version (faster computation, lesser accuracy, doesn't perturb the normal)
	 - FWaveInfo : input/output : the structure's field must be initialized prior to the call (e.g. InOutWaveInfo.Normal is the unperturbed normal)
	 Returns true if waves are supported, false otherwise. */
	bool GetWaveInfoAtPosition(const FVector& InPosition, float InWaterDepth, bool bInSimpleWaves, FWaveInfo& InOutWaveInfo) const;

	/** Returns the max height that this water body's waves can hit. Can be called regardless of whether the water body supports waves or not */
	UFUNCTION(BlueprintCallable, Category = Wave)
	float GetMaxWaveHeight() const;

	/** Sets the dynamic parameters needed by the material instance for rendering. Returns true if the operation was successfull */
	virtual bool SetDynamicParametersOnMID(UMaterialInstanceDynamic* InMID);

	/** Sets the dynamic parameters needed by the underwater post process material instance for rendering. Returns true if the operation was successfull */
	virtual bool SetDynamicParametersOnUnderwaterPostProcessMID(UMaterialInstanceDynamic* InMID);

	/** Returns true if the location is within one of this water body's exclusion volumes */
	bool IsWorldLocationInExclusionVolume(const FVector& InWorldLocation) const;

	/** Updates the bVisible/bHiddenInGame flags on the component and eventually the child renderable components (e.g. custom water body)
	 - bAllowWaterMeshRebuild : if true, the function will request a rebuild of the water mesh (expensive), which is necessary to take into account visibility changes
	*/
	virtual void UpdateComponentVisibility(bool bAllowWaterMeshRebuild);

	/** Creates/Destroys/Updates necessary MIDS */
	virtual void UpdateMaterialInstances();

	/** Returns the time basis to use in waves computation (must be unique for all water bodies currently, to ensure proper transitions between water tiles) */
	virtual float GetWaveReferenceTime() const;

	/** Returns the minimum and maximum Z of the water surface, including waves */
	virtual void GetSurfaceMinMaxZ(float& OutMinZ, float& OutMaxZ) const;

	virtual ALandscapeProxy* FindLandscape() const;

	/** Returns what can be considered the single water depth of the water surface.
	Only really make sense for EWaterBodyType::Transition water bodies for which we don't really have a way to evaluate depth. */
	virtual float GetConstantDepth() const;

	/** Returns what can be considered the single water velocity of the water surface.
	Only really make sense for EWaterBodyType::Transition water bodies for which we don't really have a way to evaluate velocity. */
	virtual FVector GetConstantVelocity() const;

	/** Returns what can be considered the single base Z of the water surface.
	Doesn't really make sense for non-flat water bodies like EWaterBodyType::Transition or EWaterBodyType::River but can still be useful when using FixedZ for post-process, for example. */
	virtual float GetConstantSurfaceZ() const;

	virtual void Reset() {}

protected:
	//~ Begin USceneComponent Interface.
	virtual void OnVisibilityChanged();
	virtual void OnHiddenInGameChanged();
	//~ End USceneComponent Interface.

	/** Returns whether the body has a flat surface or not */
	virtual bool IsFlatSurface() const;

	/** Returns whether the body's spline is closed */
	virtual bool IsWaterSplineClosedLoop() const;

	/** Returns whether the body support a height offset */
	virtual bool IsHeightOffsetSupported() const;

	/** Returns whether the body affects navigation */
	virtual bool CanAffectNavigation() const { return bGenerateCollisions && bCanAffectNavigation; }

	/** Called every time UpdateAll is called on WaterBody (prior to UpdateWaterBody) */
	virtual void BeginUpdateWaterBody();

	/** Updates WaterBody (called 1st with bWithExclusionVolumes = false, then with true */
	virtual void UpdateWaterBody(bool bWithExclusionVolumes);

	virtual void OnUpdateBody(bool bWithExclusionVolumes) {}

	/** Returns navigation area class */
	TSubclassOf<UNavAreaBase> GetNavAreaClass() const { return WaterNavAreaClass; }

protected:
	/** Computes the raw wave perturbation of the water height/normal */
	virtual float GetWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime, FVector& OutNormal) const;

	/** Computes the raw wave perturbation of the water height only (simple version : faster computation) */
	virtual float GetSimpleWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime) const;

	/** Computes the attenuation factor to apply to the raw wave perturbation. Attenuates : normal/wave height/max wave height. */
	virtual float GetWaveAttenuationFactor(const FVector& InPosition, float InWaterDepth) const;

#if WITH_EDITOR
	/** Called by UWaterBodyComponent::PostEditChangeProperty. */
	virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged);

	/** Validates this component's data */
	virtual void CheckForErrors() override;

	virtual TArray<TSharedRef<FTokenizedMessage>> CheckWaterBodyStatus() const;
#endif // WITH_EDITOR

	EWaterBodyQueryFlags CheckAndAjustQueryFlags(EWaterBodyQueryFlags InQueryFlags) const;
	void UpdateSplineComponent();
	void UpdateExclusionVolumes();
	bool UpdateWaterHeight();
	void CreateOrUpdateWaterMID();
	void CreateOrUpdateUnderwaterPostProcessMID();
	void PrepareCurrentPostProcessSettings();
	void ApplyNavigationSettings();
	void RequestGPUWaveDataUpdate();
	EObjectFlags GetTransientMIDFlags() const; 

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;
	virtual bool MoveComponentImpl(const FVector& Delta, const FQuat& NewRotation, bool bSweep, FHitResult* Hit = NULL, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags, ETeleportType Teleport = ETeleportType::None) override;

#if WITH_EDITOR
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	virtual void PostEditImport() override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void OnSplineDataChanged();
	void RegisterOnUpdateWavesData(UWaterWavesBase* InWaterWaves, bool bRegister);
	void OnWavesDataUpdated(UWaterWavesBase* InWaterWaves, EPropertyChangeType::Type InChangeType);

	void OnWaterSplineMetadataChanged(UWaterSplineMetadata* InWaterSplineMetadata, FPropertyChangedEvent& PropertyChangedEvent);
	void RegisterOnChangeWaterSplineMetadata(UWaterSplineMetadata* InWaterSplineMetadata, bool bRegister);
#endif // WITH_EDITOR

public:
	// INavRelevantInterface start
	virtual void GetNavigationData(struct FNavigationRelevantData& Data) const override;
	virtual FBox GetNavigationBounds() const override;
	virtual bool IsNavigationRelevant() const override;
	// INavRelevantInterface end

	/** Public static constants : */
	static const FName WaterBodyIndexParamName;
	static const FName WaterVelocityAndHeightName;
	static const FName GlobalOceanHeightName;
	static const FName FixedZHeightName;
	static const FName FixedVelocityName;
	static const FName FixedWaterDepthName;
	static const FName WaterAreaParamName;

	UPROPERTY(EditDefaultsOnly, Category = Collision, meta = (EditCondition = "bGenerateCollisions"))
	UPhysicalMaterial* PhysicalMaterial;

	/** Water depth at which waves start being attenuated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Wave, DisplayName = "Wave Attenuation Water Depth", meta = (UIMin = 0, ClampMin = 0, UIMax = 10000.0))
	float TargetWaveMaskDepth;

	/** Offset added to the automatically calculated max wave height bounds. Use this in case the automatically calculated max height bounds don't match your waves. This can happen if the water surface is manually altered through World Position Offset or other means.*/
	UPROPERTY(EditAnywhere, Category = Wave)
	float MaxWaveHeightOffset = 0.0f;

	/** Prevent navmesh generation under the water geometry */
	UPROPERTY(EditAnywhere, Category = Navigation, meta = (EditCondition = "bGenerateCollisions"))
	bool bFillCollisionUnderWaterBodiesForNavmesh;

	/** Post process settings to apply when the camera goes underwater (only available when bGenerateCollisions is true because collisions are needed to detect if it's under water).
	Note: Underwater post process material is setup using UnderwaterPostProcessMaterial. */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bGenerateCollisions && UnderwaterPostProcessMaterial != nullptr", DisplayAfter = "UnderwaterPostProcessMaterial"))
	FUnderwaterPostProcessSettings UnderwaterPostProcessSettings;

	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	FWaterCurveSettings CurveSettings;

	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly)
	UMaterialInterface* WaterMaterial;

	/** Post process material to apply when the camera goes underwater (only available when bGenerateCollisions is true because collisions are needed to detect if it's under water). */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "bGenerateCollisions", DisplayAfter = "WaterMaterial"))
	UMaterialInterface* UnderwaterPostProcessMaterial;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	FWaterBodyHeightmapSettings WaterHeightmapSettings;

	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	TMap<FName, FWaterBodyWeightmapSettings> LayerWeightmapSettings;
#endif

	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Rendering)
	float ShapeDilation = 4096.0f;

	/** The distance above the surface of the water where collision checks should still occur. Useful if the post process effect is not activating under really high waves. */ 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, AdvancedDisplay, Category = Collision)
	float CollisionHeightOffset = 0.f;

	/** If enabled, landscape will be deformed based on this water body placed on top of it and landscape height will be considered when determining water depth at runtime */
	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	bool bAffectsLandscape;

	/** If true, one or more collision components associated with this water will be generated. Otherwise, this water body will only affect visuals. */
	UPROPERTY(Category = Collision, EditAnywhere, BlueprintReadOnly)
	bool bGenerateCollisions = true;

protected:

	/** Unique Id for accessing (wave, ... ) data in GPU buffers */
	UPROPERTY(Transient, DuplicateTransient, NonTransactional, VisibleAnywhere, BlueprintReadOnly, Category = Water)
	int32 WaterBodyIndex = INDEX_NONE;

	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly)
	UStaticMesh* WaterMeshOverride;

	/** Higher number is higher priority. If two water bodies overlap and they don't have a transition material specified, this will be used to determine which water body to use the material from. Valid range is -8192 to 8191 */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "-8192", ClampMax = "8191"))
	int32 OverlapMaterialPriority = 0;

	UPROPERTY(Category = Collision, EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "bGenerateCollisions"))
	FName CollisionProfileName;

	UPROPERTY(Transient)
	UWaterSplineMetadata* WaterSplineMetadata;

	UPROPERTY(Category = Debug, VisibleInstanceOnly, Transient, NonPIEDuplicateTransient, TextExportTransient, meta = (DisplayAfter = "WaterMaterial"))
	UMaterialInstanceDynamic* WaterMID;

	UPROPERTY(Category = Debug, VisibleInstanceOnly, Transient, NonPIEDuplicateTransient, TextExportTransient, meta = (DisplayAfter = "UnderwaterPostProcessMaterial"))
	UMaterialInstanceDynamic* UnderwaterPostProcessMID;

	/** Islands in this water body*/
	UPROPERTY(Category = Water, EditInstanceOnly, AdvancedDisplay)
	TArray<TLazyObjectPtr<AWaterBodyIsland>> Islands;

	UPROPERTY(Category = Water, EditInstanceOnly, AdvancedDisplay)
	TArray<TLazyObjectPtr<AWaterBodyExclusionVolume>> ExclusionVolumes;

	UPROPERTY(Transient)
	mutable TWeakObjectPtr<ALandscapeProxy> Landscape;

	UPROPERTY(Transient)
	FPostProcessSettings CurrentPostProcessSettings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Navigation, meta = (EditCondition = "bGenerateCollisions"))
	bool bCanAffectNavigation;

	// The navigation area class that will be generated on nav mesh
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Navigation, meta = (EditCondition = "bCanAffectNavigation && bGenerateCollisions"))
	TSubclassOf<UNavAreaBase> WaterNavAreaClass;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	bool bOverrideWaterMesh_DEPRECATED;
#endif
};
