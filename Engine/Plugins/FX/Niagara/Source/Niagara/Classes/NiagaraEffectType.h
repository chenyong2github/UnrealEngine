// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "InGamePerformanceTracker.h"
#include "RenderCommandFence.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraPerfBaseline.h"
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
	/** Controls whether we should cull systems based on how many instances with the same Effect Type are active. */
	UPROPERTY(EditAnywhere, Category = "Scalability", DisplayName = "Cull By Effect Type Instance Count", meta = (InlineEditConditionToggle))
	uint32 bCullMaxInstanceCount : 1;
	/** Controls whether we should cull systems based on how many instances of the system are active. */
	UPROPERTY(EditAnywhere, Category = "Scalability", DisplayName = "Cull By System Instance Count", meta = (InlineEditConditionToggle))
	uint32 bCullPerSystemMaxInstanceCount : 1;
	/** Controls whether visibility culling is enabled. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bCullByMaxTimeWithoutRender : 1;
	/** Controls whether global budget based culling is enabled. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bCullByGlobalBudget : 1;

	/** Effects of this type are culled beyond this distance. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bCullByDistance"))
	float MaxDistance;

	/** 
	Effects of this type will be culled when total active instances using this same EffectType exceeds this number. 
	If the effect type has a significance handler, instances are sorted by their significance and only the N most significant will be kept. The rest are culled.
	If it does not have a significance handler, instance count culling will be applied at spawn time only. New FX that would exceed the counts are not spawned/activated.
	*/
	UPROPERTY(EditAnywhere, Category = "Scalability", DisplayName = "Max Effect Type Instances", meta = (EditCondition = "bCullMaxInstanceCount"))
	int32 MaxInstances;

	/**
	Effects of this type will be culled when total active instances of the same NiagaraSystem exceeds this number. 
	If the effect type has a significance handler, instances are sorted by their significance and only the N most significant will be kept. The rest are culled.
	If it does not have a significance handler, instance count culling will be applied at spawn time only. New FX that would exceed the counts are not spawned/activated.
	*/
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bCullPerSystemMaxInstanceCount"))
	int32 MaxSystemInstances;

	//TODO:
	/** The effect is culled when it's bounds take up less that this fraction of the total screen area. Only usable with fixed bounds. */
	//float ScreenFraction;

	/** Effects will be culled if they go more than this length of time without being rendered. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bCullByMaxTimeWithoutRender"))
	float MaxTimeWithoutRender;

	/** Effects will be culled if the global budget usage exceeds this fraction. A global budget usage of 1.0 means current global FX workload has reached it's max budget. Budgets are set by CVars under FX.Budget... */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bCullByGlobalBudget"))
	float MaxGlobalBudgetUsage;
	
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
	UPROPERTY(EditAnywhere, Category = "Override", DisplayName = "Override Effect Type Instance Count Settings")
	uint32 bOverrideInstanceCountSettings : 1;
	/** Controls whether we override the per system instance count culling settings. */
	UPROPERTY(EditAnywhere, Category = "Override", DisplayName = "Override System Instance Count Settings")
	uint32 bOverridePerSystemInstanceCountSettings : 1;
	/** Controls whether we override the visibility culling settings. */
	UPROPERTY(EditAnywhere, Category = "Override")
	uint32 bOverrideTimeSinceRendererSettings : 1;
	/** Controls whether we override the global budget culling settings. */
	UPROPERTY(EditAnywhere, Category = "Override")
	uint32 bOverrideGlobalBudgetCullingSettings : 1;
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

class UNiagaraComponent;
struct FNiagaraScalabilityState;

/**
Base class for significance handlers. 
These allow Niagara's scalability system to determine the relative significance of different FX in the scene.
Some basic ones are provided but projects are free to implement their own more complex determinations of significance.
For example, FX attached to the player character could be given higher priority.
*/
UCLASS(abstract, EditInlineNew)
class NIAGARA_API UNiagaraSignificanceHandler : public UObject
{
	GENERATED_BODY()

public:
	virtual void CalculateSignificance(TArray<UNiagaraComponent*>& Components, TArray<FNiagaraScalabilityState>& OutState, TArray<int32>& OutIndices)PURE_VIRTUAL(CalculateSignificance, );
};

/** Significance is determined by the system's distance to the nearest camera. Closer systems are more significant. */
UCLASS(EditInlineNew, meta = (DisplayName = "Distance"))
class NIAGARA_API UNiagaraSignificanceHandlerDistance : public UNiagaraSignificanceHandler
{
	GENERATED_BODY()

public:
	virtual void CalculateSignificance(TArray<UNiagaraComponent*>& Components, TArray<FNiagaraScalabilityState>& OutState, TArray<int32>& OutIndices) override;
};

/** Significance is determined by the system's age. Newer systems are more significant. */
UCLASS(EditInlineNew, meta = (DisplayName = "Age"))
class NIAGARA_API UNiagaraSignificanceHandlerAge : public UNiagaraSignificanceHandler
{
	GENERATED_BODY()

public:
	virtual void CalculateSignificance(TArray<UNiagaraComponent*>& Components, TArray<FNiagaraScalabilityState>& OutState, TArray<int32>& OutIndices) override;
};

//////////////////////////////////////////////////////////////////////////

/** Contains settings and working data shared among many NiagaraSystems that share some commonality of type. For example ImpactFX vs EnvironmentalFX. */
UCLASS(config = Niagara, perObjectConfig)
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
	UPROPERTY(EditAnywhere, Category = "Scalability")
	ENiagaraScalabilityUpdateFrequency UpdateFrequency;

	/** How effects of this type react when they fail the cull checks. */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	ENiagaraCullReaction CullReaction;

	/** Used to determine the relative significance of FX in the scene which is used in other scalability systems such as instance count culling. */
	UPROPERTY(EditAnywhere, Instanced, Category = "Scalability")
	UNiagaraSignificanceHandler* SignificanceHandler;

	/** Cull settings to use at each detail level. */
	UPROPERTY()
	TArray<FNiagaraSystemScalabilitySettings> DetailLevelScalabilitySettings_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = "Scalability")
	FNiagaraSystemScalabilitySettingsArray SystemScalabilitySettings;
	
	UPROPERTY(EditAnywhere, Category = "Scalability")
	FNiagaraEmitterScalabilitySettingsArray EmitterScalabilitySettings;

	//TODO: Dynamic budgetting from perf data.
	//void ApplyDynamicBudget(float InDynamicBudget_GT, float InDynamicBudget_GT_CNC, float InDynamicBudget_RT);

	FORCEINLINE const FNiagaraSystemScalabilitySettingsArray& GetSystemScalabilitySettings()const { return SystemScalabilitySettings; }
	FORCEINLINE const FNiagaraEmitterScalabilitySettingsArray& GetEmitterScalabilitySettings()const { return EmitterScalabilitySettings; }

	const FNiagaraSystemScalabilitySettings& GetActiveSystemScalabilitySettings()const;
	const FNiagaraEmitterScalabilitySettings& GetActiveEmitterScalabilitySettings()const;

	UNiagaraSignificanceHandler* GetSignificanceHandler()const { return SignificanceHandler; }

	/** Total number of instances across all systems for this effect type. */
	int32 NumInstances;

	/** Marks that there have been new systems added for this effect type since it's last scalability manager update. Will force a manager update. */
	uint32 bNewSystemsSinceLastScalabilityUpdate : 1;

#if NIAGARA_PERF_BASELINES
	UNiagaraBaselineController* GetPerfBaselineController() { return PerformanceBaselineController; }
	FNiagaraPerfBaselineStats& GetPerfBaselineStats(){return PerfBaselineStats;}
	FORCEINLINE bool IsPerfBaselineValid(){ return PerfBaselineVersion == CurrentPerfBaselineVersion; }
	void UpdatePerfBaselineStats(FNiagaraPerfBaselineStats& NewBaselineStats);

	void InvalidatePerfBaseline();

	void SpawnBaselineActor(UWorld* World);
#endif

private:

	/** Controls generation of performance baseline data for this effect type. */
	UPROPERTY(EditAnywhere, Category="Performance", Instanced)
	UNiagaraBaselineController* PerformanceBaselineController;

	/**
	Performance data gathered from the Baseline System. 
	These give artists a good idea of the perf to aim for in their own FX.
	*/
	UPROPERTY(config)
	FNiagaraPerfBaselineStats PerfBaselineStats;

	//Version guid at the time these baseline stats were generated.
	//Allows us to invalidate perf baseline results if there are significant performance optimizations
	UPROPERTY(config)
	FGuid PerfBaselineVersion;

#if NIAGARA_PERF_BASELINES
	/** The current version for perf baselines. Regenerate this if there are significant performance improvements that would invalidate existing baseline data. */
	static const FGuid CurrentPerfBaselineVersion;

	DECLARE_DELEGATE_OneParam(FGeneratePerfBaselines, TArray<UNiagaraEffectType*>&/**BaselinesToGenerate*/);

	/** Delegate allowing us to call into editor code to generate performance baselines. */
	static FGeneratePerfBaselines GeneratePerfBaselinesDelegate;
public:
	static FGeneratePerfBaselines& OnGeneratePerfBaselines(){ return GeneratePerfBaselinesDelegate; }
	static void GeneratePerfBaselines();
#endif
};
