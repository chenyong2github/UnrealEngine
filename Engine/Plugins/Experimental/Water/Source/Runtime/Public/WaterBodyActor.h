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
#include "WaterBodyActor.generated.h"

class UWaterSplineComponent;
class AWaterBodyIsland;
class AWaterBodyExclusionVolume;
class ALandscapeProxy;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class EWaterBodyType : uint8
{
	/** Rivers defined by a spline down the middle */
	River,
	/** Lakes defined by a close loop spline around the shore with water in the middle*/
	Lake,
	/* Ocean defined by a shoreline spline and rendered out to a far distance */
	Ocean,
	/** A custom water body that can be used for gameplay reasons.  Uses a spline down the middle to encode gameplay data. Requires a custom mesh to render, doesn't affect landscape */
	Transition UMETA(DisplayName = "Custom"),
	/** Max value */	
	Num UMETA(Hidden),
};

enum class EWaterBodyQueryFlags
{
	None = 0,
	/** Computes the location on the water plane (+ optionally the location on the water surface if IncludeWaves is passed) */
	ComputeLocation = (1 << 0), 
	/** Computes the normal on the water plane (+ optionally the normal on the water surface if IncludeWaves is passed) */
	ComputeNormal = (1 << 1), 
	/** Computes the water velocity */
	ComputeVelocity = (1 << 2), 
	/** Compute the water depth */
	ComputeDepth = (1 << 3), 
	/** Computes the how much under the water surface the point is (implies ComputeLocation) : > 0 if underwater */
	ComputeImmersionDepth = (1 << 4), 
	/** Includes waves in water depth computation (requires ComputeLocation or ComputeNormal or ComputeDepth or ComputeImmersionDepth) */
	IncludeWaves = (1 << 5), 
	/** Use simple version of waves computation (requires IncludeWaves) */
	SimpleWaves = (1 << 6), 
	/** Ignore the water exclusion volumes (it is expected to check for IsInExclusionVolume() when not using this flag as the returned information will be invalid) */
	IgnoreExclusionVolumes = (1 << 7),
};
ENUM_CLASS_FLAGS(EWaterBodyQueryFlags);

// ----------------------------------------------------------------------------------
/** Struct holding wave computation result : */
struct FWaveInfo
{
	/** Time that has been used in the wave computation */
	float ReferenceTime = 0.0f;
	/** Factor between 0 and 1 to dampen the waves (e.g. near the shore, based on a mask, etc.) */
	float AttenuationFactor = 1.0f;
	/** Current wave amplitude at this location (includes AttenuationFactor dampening) */ 
	float Height = 0.0f;
	/** Maximum wave amplitude at this location (includes AttenuationFactor dampening) */
	float MaxHeight = 0.0f;
	/** Current wave normal at this location (includes AttenuationFactor dampening) */
	FVector Normal = FVector::UpVector;
};

// ----------------------------------------------------------------------------------

/** Struct holding the result from water queries :  */
struct FWaterBodyQueryResult
{
	FWaterBodyQueryResult(const TOptional<float>& InSplineInputKey = TOptional<float>())
		: SplineInputKey(InSplineInputKey)
	{
	}

	EWaterBodyQueryFlags GetQueryFlags() const { return QueryFlags; }
	const FVector& GetWaterPlaneLocation() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeLocation); return WaterPlaneLocation; }
	const FVector& GetWaterSurfaceLocation() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeLocation); return WaterSurfaceLocation; }
	const FVector& GetWaterPlaneNormal() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeNormal); return WaterPlaneNormal; }
	const FVector& GetWaterSurfaceNormal() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeNormal); return WaterSurfaceNormal; }
	float GetWaterPlaneDepth() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeDepth); return WaterPlaneDepth; }
	float GetWaterSurfaceDepth() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeDepth); return WaterSurfaceDepth; }
	const FWaveInfo& GetWaveInfo() const { check(EnumHasAnyFlags(QueryFlags, EWaterBodyQueryFlags::IncludeWaves)); return WaveInfo; }
	float GetImmersionDepth() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeImmersionDepth); return ImmersionDepth; }
	bool IsInWater() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeImmersionDepth); return (ImmersionDepth > 0.f); }
	const FVector& GetVelocity() const { check(QueryFlags & EWaterBodyQueryFlags::ComputeVelocity); return Velocity; }
	const TOptional<float>& GetSplineInputKey() const { return SplineInputKey; }
	bool IsInExclusionVolume() const { return bIsInExclusionVolume; }

	void SetQueryFlags(EWaterBodyQueryFlags InFlags) { QueryFlags = InFlags; }
	float LazilyComputeSplineKey(const AWaterBody& InWaterBody, const FVector& InWorldLocation);
	void SetWaterPlaneLocation(const FVector& InValue) { check(QueryFlags & EWaterBodyQueryFlags::ComputeLocation); WaterPlaneLocation = InValue; }
	void SetWaterSurfaceLocation(const FVector& InValue) { check(QueryFlags & EWaterBodyQueryFlags::ComputeLocation); WaterSurfaceLocation = InValue; }
	void SetWaterPlaneNormal(const FVector& InValue) { check(QueryFlags & EWaterBodyQueryFlags::ComputeNormal); WaterPlaneNormal = InValue; }
	void SetWaterSurfaceNormal(const FVector& InValue) { check(QueryFlags & EWaterBodyQueryFlags::ComputeNormal); WaterSurfaceNormal = InValue; }
	void SetWaterPlaneDepth(float InValue) { check(QueryFlags & EWaterBodyQueryFlags::ComputeDepth); WaterPlaneDepth = InValue; }
	void SetWaterSurfaceDepth(float InValue) { check(QueryFlags & EWaterBodyQueryFlags::ComputeDepth); WaterSurfaceDepth = InValue; }
	void SetWaveInfo(const FWaveInfo& InValue) { check(EnumHasAllFlags(QueryFlags, EWaterBodyQueryFlags::IncludeWaves)); WaveInfo = InValue; }
	void SetImmersionDepth(float InValue) { check(QueryFlags & EWaterBodyQueryFlags::ComputeImmersionDepth); ImmersionDepth = InValue; }
	void SetVelocity(const FVector& InValue) { check(QueryFlags & EWaterBodyQueryFlags::ComputeVelocity); Velocity = InValue; }
	void SetIsInExclusionVolume(bool bInValue) { bIsInExclusionVolume = bInValue; }

private : 
	// ----------------------------------------------------------------------------------
	// Always set :
	EWaterBodyQueryFlags QueryFlags = EWaterBodyQueryFlags::None;

	// ----------------------------------------------------------------------------------
	// Optionally set :
	// Sample point on the spline used in the query (may or may not be computed depending on the query/water body type) :
	TOptional<float> SplineInputKey; 
	// Location of the water plane (i.e. the water surface, excluding waves) : 
	FVector WaterPlaneLocation = FVector::ZeroVector; 
	// (Optionally) includes wave perturbation (if IncludeWaves is passed), otherwise, same as WaterPlaneLocation : 
	FVector WaterSurfaceLocation = FVector::ZeroVector; 
	// Normal of the water plane (i.e. the water surface, excluding waves) :
	FVector WaterPlaneNormal = FVector::UpVector;
	// (Optionally) includes wave perturbation (if IncludeWaves is passed), otherwise, same as WaterPlaneNormal :
	FVector WaterSurfaceNormal = FVector::UpVector; 
	// Depth of the water plane (i.e. the water surface, excluding waves) : 
	float WaterPlaneDepth = 0.0f;
	// (Optionally) includes wave perturbation (if IncludeWaves is passed), otherwise, same as WaterPlaneDepth :
	float WaterSurfaceDepth = 0.0f; 
	// Wave perturbation information (if IncludeWaves is passed) :
	FWaveInfo WaveInfo; 
	// Difference between the query location depth and the water surface depth, (optionally) includes wave perturbation (if IncludeWaves is passed)
	float ImmersionDepth = 0.0f; 
	// Velocity of the water at this location
	FVector Velocity = FVector::ZeroVector;
	// Whether the requested location is within an exclusion volme (optionally ignored)
	bool bIsInExclusionVolume = false;
};

// ----------------------------------------------------------------------------------

// For internal use.
UCLASS(Abstract, Within = WaterBody)
class WATER_API UWaterBodyGenerator : public UObject
{
	GENERATED_UCLASS_BODY()

public:
	void UpdateBody(bool bWithExclusionVolumes);

	virtual void Reset() {}
	virtual void OnUpdateBody(bool bWithExclusionVolumes) {}
	/** Indicates whether the body is baked (false) at save-time or needs to be dynamically regenerated at runtime (true) and is therefore transient. */
	virtual bool IsDynamicBody() const { return false; }

	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const PURE_VIRTUAL(UWaterBodyGenerator::GetCollisionComponents, return TArray<UPrimitiveComponent*>(); );
};

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

//@todo_water: Remove Blueprintable
UCLASS(Blueprintable, Abstract, HideCategories = (Tags, Activation, Cooking, Replication, Input, Actor, AssetUserData))
class WATER_API AWaterBody : public AActor, public INavRelevantInterface, public IWaterBrushActorInterface
{
	GENERATED_UCLASS_BODY()

public:
	//~ Begin IWaterBrushActorInterface interface
	virtual bool AffectsLandscape() const override;
	virtual bool AffectsWaterMesh() const override;
	virtual bool CanAffectWaterMesh() const override { return false; }

#if WITH_EDITOR
	virtual const FWaterCurveSettings& GetWaterCurveSettings() const { return CurveSettings; }
	virtual const FWaterBodyHeightmapSettings& GetWaterHeightmapSettings() const override { return WaterHeightmapSettings; }
	virtual const TMap<FName, FWaterBodyWeightmapSettings>& GetLayerWeightmapSettings() const override { return LayerWeightmapSettings; }
	virtual ETextureRenderTargetFormat GetBrushRenderTargetFormat() const override;
	virtual void GetBrushRenderDependencies(TSet<UObject*>& OutDependencies) const override;
#endif //WITH_EDITOR
	//~ End IWaterBrushActorInterface interface

	/** Actor Interface */
	virtual void NotifyActorBeginOverlap(AActor* OtherActor);
	virtual void NotifyActorEndOverlap(AActor* OtherActor);

	/** Returns whether the body supports waves */
	virtual bool IsWaveSupported() const;

	/** Returns true if there are valid water waves */
	bool HasWaves() const;

	/** Returns body's collision components */
	virtual TArray<UPrimitiveComponent*> GetCollisionComponents() const { return TArray<UPrimitiveComponent*>(); }

	/** Returns the type of body */
	virtual EWaterBodyType GetWaterBodyType() const { return WaterBodyType; }

	/** Returns collision extents (For internal use. Please use AWaterBodyOcean instead.) */
	virtual FVector GetCollisionExtents() const { return FVector::ZeroVector; }

	/** Sets an additional water height (For internal use. Please use AWaterBodyOcean instead.) */
	virtual void SetHeightOffset(float InHeightOffset) { check(false); }

	/** Returns the additional water height added to the body (For internal use. Please use AWaterBodyOcean instead.) */
	virtual float GetHeightOffset() const { return 0.f; }

	/** Sets the water mesh (when bOverrideWaterMesh is true of the for custom water body actors) */
	void SetWaterMeshOverride(UStaticMesh* InMesh) { WaterMeshOverride = InMesh; }

	/** Returns River to lake transition material instance (For internal use. Please use AWaterBodyRiver instead.) */
	UFUNCTION(BlueprintCallable, Category = Rendering)
	virtual UMaterialInstanceDynamic* GetRiverToLakeTransitionMaterialInstance() { return nullptr; }

	/** Returns River to ocean transition material instance (For internal use. Please use AWaterBodyRiver instead.) */
	UFUNCTION(BlueprintCallable, Category = Rendering)
	virtual UMaterialInstanceDynamic* GetRiverToOceanTransitionMaterialInstance() { return nullptr; }

	/** Returns water spline component */
	UFUNCTION(BlueprintCallable, Category=Water)
	UWaterSplineComponent* GetWaterSpline() const { return SplineComp; }

	/** Returns collision profile name */
	FName GetCollisionProfileName() const { return CollisionProfileName; }

	/** Returns water mesh override */
	UStaticMesh* GetWaterMeshOverride() const { return (bOverrideWaterMesh || GetWaterBodyType() == EWaterBodyType::Transition) ? WaterMeshOverride : nullptr; }

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

	/** Spline query helper. It's faster to get the spline key once then query properties using that key, rather than querying repeatedly by location etc. */
	float FindInputKeyClosestToWorldLocation(const FVector& WorldLocation) const;

	/*
	 * Spline queries specific to metadata type
	 */
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

	UFUNCTION(BlueprintCallable, Category = Wave)
	void SetWaterWaves(UWaterWavesBase* InWaterWaves);
	const UWaterWavesBase* GetWaterWaves() const { return WaterWaves; }

	/** AActor interface */
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PreInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void PostDuplicate(bool bDuplicateForPie) override;

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
	static const FName OverriddenWaterDepthName;

	UPROPERTY(EditDefaultsOnly, Category = Collision, meta = (EditCondition = "bGenerateCollisions"))
	UPhysicalMaterial* PhysicalMaterial;

	/** Water depth at which waves start being attenuated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = Wave, DisplayName = "Wave Attenuation Water Depth", meta = (UIMin = 0, ClampMin = 0, UIMax = 10000.0))
	float TargetWaveMaskDepth;

	/** Offset added to the automatically calculated max wave height bounds. Use this in case the automatically calculated max height bounds don't match your waves. This can happen if the water surface is manually altered through World Position Offset or other means.*/
	UPROPERTY(EditAnywhere, Category = Wave)
	float MaxWaveHeightOffset = 0.0f;

	/** Unique Id for accessing (wave, ... ) data in GPU buffers */
	UPROPERTY(Transient, DuplicateTransient, NonTransactional, VisibleAnywhere, BlueprintReadOnly, Category = Water)
	int32 WaterBodyIndex = INDEX_NONE;

	/** Prevent navmesh generation under the water geometry */
	UPROPERTY(EditAnywhere, Category = Navigation, meta = (EditCondition = "bGenerateCollisions"))
	bool bFillCollisionUnderWaterBodiesForNavmesh;

	/** Post process settings to apply when the camera goes underwater (only available when bGenerateCollisions is true because collisions are needed to detect if it's under water).
	Note: Underwater post process material is setup using UnderwaterPostProcessMaterial. */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bGenerateCollisions && UnderwaterPostProcessMaterial != nullptr", DisplayAfter = "UnderwaterPostProcessMaterial"))
	FUnderwaterPostProcessSettings UnderwaterPostProcessSettings;

	// @todo_water: Remove and always use GetWaterBodyType()
	UPROPERTY(Category = Water, EditAnywhere, BlueprintReadWrite, meta = (ExposeOnSpawn))
	EWaterBodyType WaterBodyType;

	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	FWaterCurveSettings CurveSettings;

	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly)
	UMaterialInterface* WaterMaterial;

	/** Post process material to apply when the camera goes underwater (only available when bGenerateCollisions is true because collisions are needed to detect if it's under water). */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "bGenerateCollisions", DisplayAfter = "WaterMaterial"))
	UMaterialInterface* UnderwaterPostProcessMaterial;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FLandmassTerrainCarvingSettings TerrainCarvingSettings_DEPRECATED;

	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	FWaterBodyHeightmapSettings WaterHeightmapSettings;

	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	TMap<FName, FWaterBodyWeightmapSettings> LayerWeightmapSettings;
#endif

	/** If enabled, landscape will be deformed based on this water body placed on top of it and landscape height will be considered when determining water depth at runtime */
	UPROPERTY(Category = Terrain, EditAnywhere, BlueprintReadWrite)
	bool bAffectsLandscape;
	
	/** If true, one or more collision components associated with this water will be generated. Otherwise, this water body will only affect visuals. */
	UPROPERTY(Category = Collision, EditAnywhere, BlueprintReadOnly)
	bool bGenerateCollisions = true;

protected:

	// TODO [jonathan.bard] : make sure override water mesh works for all types and remove the bool (WaterMeshOverride is already a pointer
	UPROPERTY(Category = Rendering, BlueprintReadOnly)
	bool bOverrideWaterMesh;

	UPROPERTY(Category = Rendering, EditAnywhere, AdvancedDisplay, BlueprintReadOnly)
	UStaticMesh* WaterMeshOverride;

	/** Higher number is higher priority. If two water bodies overlap and they don't have a transition material specified, this will be used to determine which water body to use the material from. Valid range is -8192 to 8191 */
	UPROPERTY(Category = Rendering, EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "-8192", ClampMax = "8191"))
	int32 OverlapMaterialPriority = 0;

	UPROPERTY(Category = Collision, EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "bGenerateCollisions"))
	FName CollisionProfileName;

	/**
	 * The spline data attached to this water type.
	 */
	UPROPERTY(Category = Water, VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	UWaterSplineComponent* SplineComp;

	UPROPERTY(Instanced)
	UWaterSplineMetadata* WaterSplineMetadata;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	UBillboardComponent* ActorIcon;
#endif

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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = Wave, DisplayName = "Waves Source")
	UWaterWavesBase* WaterWaves = nullptr;

	UPROPERTY()
	bool bHasWaveSpectrumSettings_DEPRECATED = true;

	UPROPERTY()
	bool bHasTerrainCarvingSettingsSettings_DEPRECATED = true;

public:

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

	/** Sets the dynamic parameters needed by the material instance for rendering */
	virtual void SetDynamicParametersOnMID(UMaterialInstanceDynamic* InMID);

	/** Sets the dynamic parameters needed by the underwater post process material instance for rendering */
	virtual void SetDynamicParametersOnUnderwaterPostProcessMID(UMaterialInstanceDynamic* InMID);

	/** Returns true if the location is within one of this water body's exclusion volumes */
	bool IsWorldLocationInExclusionVolume(const FVector& InWorldLocation) const;

	void UpdateWaterComponentVisibility();

	/** Creates/Destroys/Updates necessary MIDS */
	virtual void UpdateMaterialInstances();

#if WITH_EDITOR
	void UpdateActorIcon();
#endif // WITH_EDITOR

protected:

	/** Initializes the water body */
	virtual void InitializeBody() {}

	/** Returns whether the body was initialized */
	virtual bool IsBodyInitialized() const { return true; }

	/** Returns whether the body is baked (false) at save-time or needs to be dynamically regenerated at runtime (true) and is therefore transient. */
	virtual bool IsBodyDynamic() const { return false; }

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
	virtual void UpdateWaterBody(bool bWithExclusionVolumes) {}

	/** Returns what can be considered the single base Z of the water surface.
	Doesn't really make sense for non-flat water bodies like EWaterBodyType::Transition or EWaterBodyType::River but can still be useful when using FixedZ for post-process, for example. */
	virtual float GetConstantSurfaceZ() const;

	/** Returns what can be considered the single water depth of the water surface.
	Only really make sense for EWaterBodyType::Transition water bodies for which we don't really have a way to evaluate depth. */
	virtual float GetConstantDepth() const;

	/** Returns the minimum and maximum Z of the water surface, including waves */
	virtual void GetSurfaceMinMaxZ(float& OutMinZ, float& OutMaxZ) const;

	/** Returns navigation area class */
	TSubclassOf<UNavAreaBase> GetNavAreaClass() const { return WaterNavAreaClass; }

protected:
	
	/** Returns the time basis to use in waves computation (must be unique for all water bodies currently, to ensure proper transitions between water tiles) */
	virtual float GetWaveReferenceTime() const;

	/** Computes the raw wave perturbation of the water height/normal */
	virtual float GetWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime, FVector& OutNormal) const;

	/** Computes the raw wave perturbation of the water height only (simple version : faster computation) */
	virtual float GetSimpleWaveHeightAtPosition(const FVector& InPosition, float InWaterDepth, float InTime) const;

	/** Computes the attenuation factor to apply to the raw wave perturbation. Attenuates : normal/wave height/max wave height. */
	virtual float GetWaveAttenuationFactor(const FVector& InPosition, float InWaterDepth) const;

#if WITH_EDITOR
	/** Returns whether icon billboard is visible. */
	virtual bool IsIconVisible() const;

	/** For internal use. */
	virtual bool IsWaterBodyTypeReadOnly() const { return true; }

	/** For internal use. */
	virtual void FixupOnPostRegisterAllComponents() {}

	/** Called by AWaterBodyActor::PostEditChangeProperty. */
	virtual void OnPostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent, bool& bShapeOrPositionChanged, bool& bWeightmapSettingsChanged);

	/** Validates this actor's data */
	virtual void CheckForErrors() override;
#endif // WITH_EDITOR

	EWaterBodyQueryFlags CheckAndAjustQueryFlags(EWaterBodyQueryFlags InQueryFlags) const;
	void UpdateAll(bool bShapeOrPositionChanged);
	void UpdateSplineComponent();
	void UpdateExclusionVolumes();
	bool UpdateWaterHeight();
	void CreateOrUpdateWaterMID();
	void CreateOrUpdateUnderwaterPostProcessMID();
	ALandscapeProxy* FindLandscape() const;
	void SetOceanOnWaterSubsystem();
	void PrepareCurrentPostProcessSettings();
	void ApplyNavigationSettings() const;
	void RequestGPUWaveDataUpdate();
	void SetWaterWavesInternal(UWaterWavesBase* InWaterWaves, bool bTriggerWaterBodyChanged);
	EObjectFlags GetTransientMIDFlags() const; 

	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void Destroyed() override;

#if WITH_EDITOR
	virtual bool CanEditChange(const FProperty* InProperty) const override;
	virtual void PostEditMove(bool bFinished) override;
	virtual void PreEditUndo() override;
	virtual void PostEditUndo() override;
	virtual void PostEditImport() override;
	virtual void PreEditChange(FProperty* PropertyAboutToChange) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	void OnSplineDataChanged();
	void RegisterOnUpdateWavesData(UWaterWavesBase* InWaterWaves, bool bRegister);
	void OnWavesDataUpdated(UWaterWavesBase* InWaterWaves, EPropertyChangeType::Type InChangeType);

	void OnWaterSplineMetadataChanged(UWaterSplineMetadata* InWaterSplineMetadata, FPropertyChangedEvent& PropertyChangedEvent);
	void RegisterOnChangeWaterSplineMetadata(UWaterSplineMetadata* InWaterSplineMetadata, bool bRegister);
#endif // WITH_EDITOR
};
