// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/EngineTypes.h"
#include "Engine/Public/Tickable.h"
#include "Interfaces/Interface_PostProcessVolume.h"
#include "WaterBodyManager.h"
#include "WaterSubsystem.generated.h"

DECLARE_STATS_GROUP(TEXT("Water"), STATGROUP_Water, STATCAT_Advanced);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCameraUnderwaterStateChanged, bool, bIsUnderWater, float, DepthUnderwater);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWaterScalabilityChanged);

class AWaterMeshActor;
class AWaterBody;
class UMaterialParameterCollection;
class UWaterRuntimeSettings;
class FSceneView;
class UTexture2D;
struct FUnderwaterPostProcessDebugInfo;
enum class EWaterBodyQueryFlags;

bool IsWaterEnabled(bool bIsRenderThread);

struct FUnderwaterPostProcessVolume : public IInterface_PostProcessVolume
{
	FUnderwaterPostProcessVolume()
		: PostProcessProperties()
	{}

	virtual bool EncompassesPoint(FVector Point, float SphereRadius/*=0.f*/, float* OutDistanceToPoint) override
	{
		// For underwater, the distance to point is 0 for now because underwater doesn't look correct if it is blended with other post process due to the wave masking
		if (OutDistanceToPoint)
		{
			*OutDistanceToPoint = 0;
		}

		// If post process properties are enabled and valid return true.  We already computed if it encompasses the water volume earlier
		return PostProcessProperties.bIsEnabled && PostProcessProperties.Settings;
	}

	virtual FPostProcessVolumeProperties GetProperties() const override
	{
		return PostProcessProperties;
	}

	FPostProcessVolumeProperties PostProcessProperties;
};

/**
 * This is the API used to get information about water at runtime
 */
UCLASS(BlueprintType, Transient)
class WATER_API UWaterSubsystem : public UWorldSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

	UWaterSubsystem();

	virtual void Tick(float DeltaTime) override;

	/** return the stat id to use for this tickable **/
	virtual TStatId GetStatId() const override;

	/** Override to support water subsystems in editor preview worlds */
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
public:

	/** Static helper function to get a water subsystem from a world, returns nullptr if world or subsystem don't exist */
	static UWaterSubsystem* GetWaterSubsystem(const UWorld* InWorld);

	/** Static helper function to get a waterbody manager from a world, returns nullptr if world or manager don't exist */
	static FWaterBodyManager* GetWaterBodyManager(UWorld* InWorld);

	virtual bool IsTickableInEditor() const override { return true; }

	// Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// End USubsystem

	FWaterBodyManager WaterBodyManager;

	AWaterMeshActor* GetWaterMeshActor() const;

	TWeakObjectPtr<AWaterBody> GetOceanActor() { return OceanActor; }
	void SetOceanActor(TWeakObjectPtr<AWaterBody> InOceanActor) { OceanActor = InOceanActor; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category=Water)
	bool IsShallowWaterSimulationEnabled() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = Water)
	bool IsUnderwaterPostProcessEnabled() const;

	UFUNCTION(BlueprintCallable, Category=Water)
	static int32 GetShallowWaterMaxDynamicForces();

	UFUNCTION(BlueprintCallable, Category = Water)
	static int32 GetShallowWaterMaxImpulseForces();

	UFUNCTION(BlueprintCallable, Category = Water)
	static int32 GetShallowWaterSimulationRenderTargetSize();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = Water)
	bool IsWaterRenderingEnabled() const;

	UFUNCTION(BlueprintCallable, Category = Water)
	float GetWaterTimeSeconds() const;

	UFUNCTION(BlueprintCallable, Category = Water)
	float GetSmoothedWorldTimeSeconds() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = Water)
	float GetCameraUnderwaterDepth() const { return CachedDepthUnderwater; }

	UFUNCTION(BlueprintCallable, Category = Water)
	void PrintToWaterLog(const FString& Message, bool bWarning);

	/** Returns the base height of the ocean. This should correspond to its world Z position */
	UFUNCTION(BlueprintCallable, Category = Water)
	float GetOceanBaseHeight() const;

	/** Returns the relative flood height */
	UFUNCTION(BlueprintCallable, Category = Water)
	float GetOceanFloodHeight() const { return FloodHeight; }

	/** Returns the total height of the ocean. This should correspond to the base height plus any additional height, like flood for example */
	UFUNCTION(BlueprintCallable, Category = Water)
	float GetOceanTotalHeight() const { return GetOceanBaseHeight() + GetOceanFloodHeight(); }

	UFUNCTION(BlueprintCallable, Category = Water)
	void SetOceanFloodHeight(float InFloodHeight);

	void SetSmoothedWorldTimeSeconds(float InTime);
	
	void SetOverrideSmoothedWorldTimeSeconds(float InTime);
	float GetOverrideSmoothedWorldTimeSeconds() const { return OverrideWorldTimeSeconds; }
	
	void SetShouldOverrideSmoothedWorldTimeSeconds(bool bOverride);
	bool GetShouldOverrideSmoothedWorldTimeSeconds() const { return bUsingOverrideWorldTimeSeconds; }

	void SetShouldPauseWaveTime(bool bInPauseWaveTime);

	UMaterialParameterCollection* GetMaterialParameterCollection() const {	return MaterialParameterCollection; }
	
	void MarkAllWaterMeshesForRebuild();

#if WITH_EDITOR
	void RegisterWaterActorClassSprite(UClass* Class, UTexture2D* Sprite);
	UTexture2D* GetWaterActorSprite(UClass* Class) const;
#endif // WITH_EDITOR

public:
	DECLARE_EVENT_OneParam(UWaterSubsystem, FOnWaterSubsystemInitialized, UWaterSubsystem*)
	static FOnWaterSubsystemInitialized OnWaterSubsystemInitialized;

	UPROPERTY(BlueprintAssignable, Category = Water)
	FOnCameraUnderwaterStateChanged OnCameraUnderwaterStateChanged;

	UPROPERTY(BlueprintAssignable, Category = Water)
	FOnWaterScalabilityChanged OnWaterScalabilityChanged;

	UPROPERTY()
	UStaticMesh* DefaultRiverMesh;
	
	UPROPERTY()
	UStaticMesh* DefaultLakeMesh;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TMap<UClass*, UTexture2D*> WaterActorSprites;

	UPROPERTY()
	UTexture2D* DefaultWaterActorSprite;

	UPROPERTY()
	UTexture2D* ErrorSprite;
#endif // WITH_EDITORONLY_DATA

private:
	void NotifyWaterScalabilityChangedInternal(IConsoleVariable* CVar);
	void NotifyWaterEnabledChangedInternal(IConsoleVariable* CVar);
	void ComputeUnderwaterPostProcess(FVector ViewLocation, FSceneView* SceneView);
	void SetMPCTime(float Time, float PrevTime);
	void AdjustUnderwaterWaterInfoQueryFlags(EWaterBodyQueryFlags& InOutFlags);
	void OnLoadProfileConfig(class UCollisionProfile* CollisionProfile);
	void AddWaterCollisionProfile();
	void ApplyRuntimeSettings(const UWaterRuntimeSettings* Settings, EPropertyChangeType::Type ChangeType);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	void ShowOnScreenDebugInfo(const FUnderwaterPostProcessDebugInfo& InDebugInfo);
#endif // !(UE_BUILD_SHIPPING || UE_BUILD_TEST)

private:

	UPROPERTY()
	mutable AWaterMeshActor* WaterMeshActor;

	TWeakObjectPtr<AWaterBody> OceanActor;

	ECollisionChannel UnderwaterTraceChannel;

	float CachedDepthUnderwater;
	float SmoothedWorldTimeSeconds;
	float NonSmoothedWorldTimeSeconds;
	float PrevWorldTimeSeconds;
	float OverrideWorldTimeSeconds;
	float FloodHeight = 0.0f;
	bool bUsingSmoothedTime;
	bool bUsingOverrideWorldTimeSeconds;
	bool bUnderWaterForAudio;
	bool bPauseWaveTime;

	/** The parameter collection asset that holds the global parameters that are updated by this actor */
	UPROPERTY()
	UMaterialParameterCollection* MaterialParameterCollection;

	FUnderwaterPostProcessVolume UnderwaterPostProcessVolume;
};
