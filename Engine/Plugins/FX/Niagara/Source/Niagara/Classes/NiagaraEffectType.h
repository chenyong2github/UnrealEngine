// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InGamePerformanceTracker.h"
#include "RenderCommandFence.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraEffectType.generated.h"

#define DEBUG_SCALABILITY_STATE (!UE_BUILD_SHIPPING)

/** Controls what action is taken by a Niagara system that fails it's cull checks. */
UENUM()
enum class ENiagaraCullReaction
{
	/** The system instance will be deactivated. Particles will be allowed to die naturally. It will not be reactivated automatically by the scalability system. */
	Deactivate UMETA(DisplayName = "Kill"),
	/** The system instance will be deactivated and particles killed immediately. It will not be reactivated automatically by the scalability system. */
	DeactivateImmediate UMETA(DisplayName = "Kill and Clear"),
	/** The system instance will be deactivated. Particles will be allowed to die naturally. Will reactivate when it passes cull tests again. */
	DeactivateResume UMETA(DisplayName = "Asleep"),
	/** The system instance will be deactivated and particles killed immediately. Will reactivate when it passes cull tests again. */
	DeactivateImmediateResume UMETA(DisplayName = "Asleep and Clear"),
	/** The system instance will be paused but will resume ticking when it passes cull tests again. */
	//PauseResume,
};

/** Controls how often should we update scalability states for these effects. */
UENUM()
enum class ENiagaraScalabilityUpdateFrequency
{
	/** Scalability will be checked only on spawn. */
	SpawnOnly,
	/** Scalability will be checked infrequently.*/
	Low,
	/** Scalability will be occasionally. */
	Medium,
	/** Scalability will be checked regularly. */
	High,
	/** Scalability will be checked every frame. */
	Continuous,
};

//////////////////////////////////////////////////////////////////////////

/** Scalability settings for Niagara Systems for a particular platform set (unless overridden). */
USTRUCT()
struct FNiagaraSystemScalabilitySettings
{
	GENERATED_USTRUCT_BODY()

	/** The platforms on which these settings are active (unless overridden). */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	FNiagaraPlatformSet Platforms;

	/** Controls whether distance culling is enabled. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bCullByDistance : 1;
	/** Controls whether instance count culling is enabled. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bCullMaxInstanceCount : 1;
	/** Controls whether visibility culling is enabled. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bCullByMaxTimeWithoutRender : 1;

	/** Effects of this type are culled beyond this distance. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bCullByDistance"))
	float MaxDistance;

	/** Effects of this type will fail to spawn when total active instance count exceeds this number. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bCullMaxInstanceCount"))
	float MaxInstances;
	
	//TODO:
	/** The effect is culled when it's bounds take up less that this fraction of the total screen area. Only usable with fixed bounds. */
	//float ScreenFraction;

	/** Effects will be culled if they go more than this length of time without being rendered. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bCullByMaxTimeWithoutRender"))
	float MaxTimeWithoutRender;
	
	FNiagaraSystemScalabilitySettings();

	void Clear();
};

/** Container struct for an array of system scalability settings. Enables details customization and data validation. */
USTRUCT()
struct FNiagaraSystemScalabilitySettingsArray
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Scalability")
	TArray<FNiagaraSystemScalabilitySettings> Settings;
};

USTRUCT()
struct FNiagaraSystemScalabilityOverride : public FNiagaraSystemScalabilitySettings
{
	GENERATED_USTRUCT_BODY()

	FNiagaraSystemScalabilityOverride();

	/** Controls whether we override the distance culling settings. */
	UPROPERTY(EditAnywhere, Category = "Override")
	uint32 bOverrideDistanceSettings : 1;
	/** Controls whether we override the instance count culling settings. */
	UPROPERTY(EditAnywhere, Category = "Override")
	uint32 bOverrideInstanceCountSettings : 1;
	/** Controls whether we override the visibility culling settings. */
	UPROPERTY(EditAnywhere, Category = "Override")
	uint32 bOverrideTimeSinceRendererSettings : 1;
};

/** Container struct for an array of system scalability overrides. Enables details customization and data validation. */
USTRUCT()
struct FNiagaraSystemScalabilityOverrides
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Override")
	TArray<FNiagaraSystemScalabilityOverride> Overrides;
};

/** Scalability settings for Niagara Emitters on a particular platform set. */
USTRUCT()
struct FNiagaraEmitterScalabilitySettings
{
	GENERATED_USTRUCT_BODY()

	/** The platforms on which these settings are active (unless overridden). */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	FNiagaraPlatformSet Platforms;

	/** Enable spawn count scaling */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bScaleSpawnCount : 1;

	/** Scale factor applied to spawn counts for this emitter. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bScaleSpawnCount"))
	float SpawnCountScale;

	FNiagaraEmitterScalabilitySettings();
	void Clear();
};

/** Container struct for an array of emitter scalability settings. Enables details customization and data validation. */
USTRUCT()
struct FNiagaraEmitterScalabilitySettingsArray
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Scalability")
	TArray<FNiagaraEmitterScalabilitySettings> Settings;
};

USTRUCT()
struct FNiagaraEmitterScalabilityOverride : public FNiagaraEmitterScalabilitySettings
{
	GENERATED_USTRUCT_BODY()

	FNiagaraEmitterScalabilityOverride();

	//Controls whether spawn count scale should be overridden.
	UPROPERTY(EditAnywhere, Category = "Override")
	uint32 bOverrideSpawnCountScale : 1;
};

/** Container struct for an array of emitter scalability overrides. Enables details customization and data validation. */
USTRUCT()
struct FNiagaraEmitterScalabilityOverrides
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY(EditAnywhere, Category = "Override")
	TArray<FNiagaraEmitterScalabilityOverride> Overrides;
};

//////////////////////////////////////////////////////////////////////////

/** Contains settings and working data shared among many NiagaraSystems that share some commonality of type. For example ImpactFX vs EnvironmentalFX. */
UCLASS()
class NIAGARA_API UNiagaraEffectType : public UObject
{
	GENERATED_UCLASS_BODY()

	//UObject Interface
	virtual void BeginDestroy()override;
	virtual bool IsReadyForFinishDestroy()override;
	virtual void Serialize(FArchive& Ar)override;
	virtual void PostLoad()override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent);
#endif
	//UObject Interface END

	/** How regularly effects of this type are checked for scalability. */
	UPROPERTY(EditAnywhere, Category = "Effect Type")
	ENiagaraScalabilityUpdateFrequency UpdateFrequency;

	/** How effects of this type react when they fail the cull checks. */
	UPROPERTY(EditAnywhere, Category = "Effect Type")
	ENiagaraCullReaction CullReaction;

	/** Cull settings to use at each detail level. */
	UPROPERTY()
	TArray<FNiagaraSystemScalabilitySettings> DetailLevelScalabilitySettings_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = "Effect Type")
	FNiagaraSystemScalabilitySettingsArray SystemScalabilitySettings;
	
	UPROPERTY(EditAnywhere, Category = "Effect Type")
	FNiagaraEmitterScalabilitySettingsArray EmitterScalabilitySettings;

	FORCEINLINE int32* GetCycleCounter(bool bGameThread, bool bConcurrent);
	void ProcessLastFrameCycleCounts();

	//TODO: Dynamic budgetting from perf data.
	//void ApplyDynamicBudget(float InDynamicBudget_GT, float InDynamicBudget_GT_CNC, float InDynamicBudget_RT);

	FORCEINLINE const FNiagaraSystemScalabilitySettingsArray& GetSystemScalabilitySettings()const { return SystemScalabilitySettings; }
	FORCEINLINE const FNiagaraEmitterScalabilitySettingsArray& GetEmitterScalabilitySettings()const { return EmitterScalabilitySettings; }

	const FNiagaraSystemScalabilitySettings& GetActiveSystemScalabilitySettings()const;
	const FNiagaraEmitterScalabilitySettings& GetActiveEmitterScalabilitySettings()const;

	float GetAverageFrameTime_GT() { return AvgTimeMS_GT; }
	float GetAverageFrameTime_GT_CNC() { return AvgTimeMS_GT_CNC; }
	float GetAverageFrameTime_RT() { return AvgTimeMS_RT; }

	/** Total number of instances across all systems for this effect type. */
	uint32 NumInstances;

private:
	float AvgTimeMS_GT;
	float AvgTimeMS_GT_CNC;
	float AvgTimeMS_RT;

	//TODO: Budgets from runtime perf.
	//The result of runtime perf calcs and dynamic budget is a bias to the minimum significance required for FX of ths type.
	//float MinSignificanceFromPerf;

	FInGameCycleHistory CycleHistory_GT;
	FInGameCycleHistory CycleHistory_GT_CNC;
	FInGameCycleHistory CycleHistory_RT;

	//Number of frames since we last sampled perf. We need not sample runtime perf every frame to get usable data.
	int32 FramesSincePerfSampled;
	bool bSampleRunTimePerfThisFrame;

	/** Fence used to guarantee that the RT is finished using our cycle counters in the case we're gathering RT cycle counts. */
	FRenderCommandFence ReleaseFence;
};

FORCEINLINE int32* UNiagaraEffectType::GetCycleCounter(bool bGameThread, bool bConcurrent)
{
	if (bSampleRunTimePerfThisFrame)
	{
		if (bGameThread)
		{
			return bConcurrent ? &CycleHistory_GT_CNC.CurrFrameCycles : &CycleHistory_GT.CurrFrameCycles;
		}
		else
		{
			//Just use the one for RT. Can split later if we'd like. We currently don't have any RT task work anyway.s
			return &CycleHistory_RT.CurrFrameCycles;
		}
	}
	return nullptr;
}