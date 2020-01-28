// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "NiagaraScript.h"
#include "NiagaraCollision.h"
#include "INiagaraMergeManager.h"
#include "NiagaraSystemFastPath.h"
#include "NiagaraEmitter.generated.h"

class UMaterial;
class UNiagaraEmitter;
class UNiagaraEventReceiverEmitterAction;
class UNiagaraRendererProperties;
class UNiagaraShaderStageBase;

//TODO: Event action that spawns other whole Systems?
//One that calls a BP exposed delegate?

USTRUCT()
struct FNiagaraEventReceiverProperties
{
	GENERATED_BODY()

	FNiagaraEventReceiverProperties()
	: Name(NAME_None)
	, SourceEventGenerator(NAME_None)
	, SourceEmitter(NAME_None)
	{

	}

	FNiagaraEventReceiverProperties(FName InName, FName InEventGenerator, FName InSourceEmitter)
		: Name(InName)
		, SourceEventGenerator(InEventGenerator)
		, SourceEmitter(InSourceEmitter)
	{

	}

	/** The name of this receiver. */
	UPROPERTY(EditAnywhere, Category = "Event Receiver")
	FName Name;

	/** The name of the EventGenerator to bind to. */
	UPROPERTY(EditAnywhere, Category = "Event Receiver")
	FName SourceEventGenerator;

	/** The name of the emitter from which the Event Generator is taken. */
	UPROPERTY(EditAnywhere, Category = "Event Receiver")
	FName SourceEmitter;

	//UPROPERTY(EditAnywhere, Category = "Event Receiver")
	//TArray<UNiagaraEventReceiverEmitterAction*> EmitterActions;
};

USTRUCT()
struct FNiagaraEventGeneratorProperties
{
	GENERATED_BODY()

	FNiagaraEventGeneratorProperties()
	: MaxEventsPerFrame(64)
	{

	}

	FNiagaraEventGeneratorProperties(FNiagaraDataSetProperties &Props, FName InEventGenerator)
		: ID(Props.ID.Name)
	{
		DataSetCompiledData.Variables = Props.Variables;
		DataSetCompiledData.ID = Props.ID;
		DataSetCompiledData.SimTarget = ENiagaraSimTarget::CPUSim;
		DataSetCompiledData.BuildLayout();
	}

	/** Max Number of Events that can be generated per frame. */
	UPROPERTY(EditAnywhere, Category = "Event Receiver")
	int32 MaxEventsPerFrame; //TODO - More complex allocation so that we can grow dynamically if more space is needed ?

	UPROPERTY()
	FName ID;

	UPROPERTY()
	FNiagaraDataSetCompiledData DataSetCompiledData;
};


UENUM()
enum class EScriptExecutionMode : uint8
{
	/** The event script is run on every existing particle in the emitter.*/
	EveryParticle = 0,
	/** The event script is run only on the particles that were spawned in response to the current event in the emitter.*/
	SpawnedParticles,
	/** The event script is run only on the particle whose int32 ParticleIndex is specified in the event payload.*/
	SingleParticle UMETA(Hidden)
};

UENUM()
enum class EParticleAllocationMode : uint8
{
	/** This mode tries to estimate the max particle count at runtime by using previous simulations as reference.*/
	AutomaticEstimate = 0,
	/** This mode is useful if the particle count can vary wildly at runtime (e.g. due to user parameters) and a lot of reallocations happen.*/
	ManualEstimate
};

USTRUCT()
struct FNiagaraEmitterScriptProperties
{
	FNiagaraEmitterScriptProperties() : Script(nullptr)
	{

	}

	GENERATED_BODY()
	
	UPROPERTY()
	UNiagaraScript *Script;

	UPROPERTY()
	TArray<FNiagaraEventReceiverProperties> EventReceivers;

	UPROPERTY()
	TArray<FNiagaraEventGeneratorProperties> EventGenerators;

	NIAGARA_API void InitDataSetAccess();

	NIAGARA_API bool DataSetAccessSynchronized() const;
};

USTRUCT()
struct FNiagaraEventScriptProperties : public FNiagaraEmitterScriptProperties
{
	GENERATED_BODY()
			
	FNiagaraEventScriptProperties() : FNiagaraEmitterScriptProperties()
	{
		ExecutionMode = EScriptExecutionMode::EveryParticle;
		SpawnNumber = 0;
		MaxEventsPerFrame = 0;
		bRandomSpawnNumber = false;
		MinSpawnNumber = 0;
	}
	
	/** Controls which particles have the event script run on them.*/
	UPROPERTY(EditAnywhere, Category="Event Handler Options")
	EScriptExecutionMode ExecutionMode;

	/** Controls whether or not particles are spawned as a result of handling the event. Only valid for EScriptExecutionMode::SpawnedParticles. If Random Spawn Number is used, this will act as the maximum spawn range. */
	UPROPERTY(EditAnywhere, Category="Event Handler Options")
	uint32 SpawnNumber;

	/** Controls how many events are consumed by this event handler. If there are more events generated than this value, they will be ignored.*/
	UPROPERTY(EditAnywhere, Category="Event Handler Options")
	uint32 MaxEventsPerFrame;

	/** Id of the Emitter Handle that generated the event. If all zeroes, the event generator is assumed to be this emitter.*/
	UPROPERTY(EditAnywhere, Category="Event Handler Options")
	FGuid SourceEmitterID;

	/** The name of the event generated. This will be "Collision" for collision events and the Event Name field on the DataSetWrite node in the module graph for others.*/
	UPROPERTY(EditAnywhere, Category="Event Handler Options")
	FName SourceEventName;

	/** Whether using a random spawn number. */
	UPROPERTY(EditAnywhere, Category = "Event Handler Options")
	bool bRandomSpawnNumber;

	/** The minimum spawn number when random spawn is used. Spawn Number is used as the maximum range. */
	UPROPERTY(EditAnywhere, Category = "Event Handler Options")
	uint32 MinSpawnNumber;
};

USTRUCT()
struct FNiagaraDetailsLevelScaleOverrides 
{
	GENERATED_BODY()

	FNiagaraDetailsLevelScaleOverrides();

	/** The Scalability spawn count scale used when Effects Quality level is set to Low instead of fx.NiagaraGlobalSpawnCountScale. */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	float Low;

	/** The Scalability spawn count scale used when Effects Quality level is set to Medium instead of fx.NiagaraGlobalSpawnCountScale. */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	float Medium;

	/** The Scalability spawn count scale used when Effects Quality level is set to High instead of fx.NiagaraGlobalSpawnCountScale. */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	float High;

	/** The Scalability spawn count scale used when Effects Quality level is set to Epic instead of fx.NiagaraGlobalSpawnCountScale. */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	float Epic;

	/** The Scalability spawn count scale used when Effects Quality level is set to Cine instead of fx.NiagaraGlobalSpawnCountScale. */
	UPROPERTY(EditAnywhere, Category = "Scalability")
	float Cine;
};

struct MemoryRuntimeEstimation
{
	TMap<uint64, int32> RuntimeAllocations;
	bool IsEstimationDirty = false;
	int32 AllocationEstimate = 0;
};

/** 
 *	UNiagaraEmitter stores the attributes of an FNiagaraEmitterInstance
 *	that need to be serialized and are used for its initialization 
 */
UCLASS(MinimalAPI)
class UNiagaraEmitter : public UObject
{
	GENERATED_UCLASS_BODY()

	friend struct FNiagaraEmitterHandle;

public:
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE(FOnPropertiesChanged);
	DECLARE_MULTICAST_DELEGATE(FOnRenderersChanged);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnEmitterCompiled, UNiagaraEmitter*);

	struct NIAGARA_API PrivateMemberNames
	{
		static const FName EventHandlerScriptProps;
	};
#endif

public:
#if WITH_EDITOR
	/** Creates a new emitter with the supplied emitter as a parent emitter and the supplied system as its owner. */
	NIAGARA_API static UNiagaraEmitter* CreateWithParentAndOwner(UNiagaraEmitter& InParentEmitter, UObject* InOwner, FName InName, EObjectFlags FlagMask);

	/** Creates a new emitter by duplicating an existing emitter. The new emitter will reference the same parent emitter if one is available. */
	static UNiagaraEmitter* CreateAsDuplicate(const UNiagaraEmitter& InEmitterToDuplicate, FName InDuplicateName, UNiagaraSystem& InDuplicateOwnerSystem);

	//Begin UObject Interface
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	NIAGARA_API FOnPropertiesChanged& OnPropertiesChanged();
	NIAGARA_API FOnRenderersChanged& OnRenderersChanged();
#endif
	void Serialize(FArchive& Ar)override;
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual void PostDuplicate(bool bDuplicateForPIE) override;
	//End UObject Interface

	/** Toggles whether or not the particles within this emitter are relative to the emitter origin or in global space. */ 
	UPROPERTY(EditAnywhere, Category = "Emitter")
	bool bLocalSpace;

	/** Toggles whether to globally make the random number generator be deterministic or non-deterministic. Any random calculation that is set to the emitter defaults will inherit this value. It is still possible to tweak individual random to be deterministic or not. In this case deterministic means that it will return the same results for the same configuration of the emitter as long as delta time is not variable. Any changes to the emitter's individual scripts will adjust the results. */
	UPROPERTY(EditAnywhere, Category = "Emitter")
	bool bDeterminism;

	/** An emitter-based seed for the deterministic random number generator. */
	UPROPERTY(EditAnywhere, Category = "Emitter", meta = (EditCondition = "bDeterminism"))
	int32 RandomSeed;

	/**
	The emitter needs to allocate memory for the particles each tick.
	To prevent reallocations, the emitter should allocate as much memory as is needed for the max particle count.
	This setting controls if the allocation size should be automatically determined or manually entered.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter")
	EParticleAllocationMode AllocationMode;
	
	/** 
	The emitter will allocate at least this many particles on it's first tick.
	This can aid performance by avoiding many allocations as an emitter ramps up to it's max size.
	*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter", meta = (EditCondition = "AllocationMode == EParticleAllocationMode::ManualEstimate"))
	int32 PreAllocationCount;

	UPROPERTY()
	FNiagaraEmitterScriptProperties UpdateScriptProps;

	UPROPERTY()
	FNiagaraEmitterScriptProperties SpawnScriptProps;

	UPROPERTY()
	FNiagaraEmitterScriptProperties EmitterSpawnScriptProps;

	UPROPERTY()
	FNiagaraEmitterScriptProperties EmitterUpdateScriptProps;


	UPROPERTY(EditAnywhere, Category = "Emitter")
	ENiagaraSimTarget SimTarget;
	
	/** The fixed bounding box value. bFixedBounds is the condition whether the fixed bounds can be edited. */
	UPROPERTY(EditAnywhere, Category = "Emitter", meta = (EditCondition = "bFixedBounds"))
	FBox FixedBounds;
	
	/** If the current engine detail level is below MinDetailLevel then this emitter is disabled. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta=(EditCondition = "bUseMinDetailLevel"))
	int32 MinDetailLevel;

	/** If the current engine detail level is above MaxDetailLevel then this emitter is disabled. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta=(EditCondition = "bUseMaxDetailLevel"))
	int32 MaxDetailLevel;

	/** Custom Scalability spawn count scales to override the ones defined by fx.NiagaraGlobalSpawnCountScale in the GScalabilityIni config hierarchy. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition = "bOverrideGlobalSpawnCountScale"))
	FNiagaraDetailsLevelScaleOverrides GlobalSpawnCountScaleOverrides;

	/** When enabled, this will spawn using interpolated parameter values and perform a partial update at spawn time. This adds significant additional cost for spawning but will produce much smoother spawning for high spawn rates, erratic frame rates and fast moving emitters. */
	UPROPERTY(EditAnywhere, Category = "Emitter")
	uint32 bInterpolatedSpawning : 1;

	/** Whether or not fixed bounds are enabled. */
	UPROPERTY(EditAnywhere, Category = "Emitter", meta = (InlineEditConditionToggle))
	uint32 bFixedBounds : 1;

	/** Whether to use the min detail or not. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bUseMinDetailLevel : 1;
	
	/** Whether to use the min detail or not. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bUseMaxDetailLevel : 1;

	/** Whether or not to override the global spawn count scale based on Effects Quality levels, as defined by fx.NiagaraGlobalSpawnCountScale in the GScalabilityIni config hierarchy. */
	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (InlineEditConditionToggle))
	uint32 bOverrideGlobalSpawnCountScale : 1;

	/** Do particles in this emitter require a persistent ID? */
	UPROPERTY(EditAnywhere, Category = "Emitter")
	uint32 bRequiresPersistentIDs : 1;

	/** Limits the delta time per tick to prevent simulation spikes due to frame lags. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter", meta = (EditCondition = "bLimitDeltaTime"))
	float MaxDeltaTimePerTick;

	/** Get the max number of iteration that the CS is going to be launched. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter")
	uint32 DefaultShaderStageIndex;

	/** Get the max number of iteration that the CS is going to be launched. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter")
	uint32 MaxUpdateIterations;

	/** Get the max number of iteration that the CS is going to be launched. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter")
	TSet<uint32> SpawnStages;

	/** Whether to limit the max tick delta time or not. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter", meta = (InlineEditConditionToggle))
	uint32 bLimitDeltaTime : 1;

	void NIAGARA_API GetScripts(TArray<UNiagaraScript*>& OutScripts, bool bCompilableOnly = true) const;

	NIAGARA_API UNiagaraScript* GetScript(ENiagaraScriptUsage Usage, FGuid UsageId);

	NIAGARA_API UNiagaraScript* GetGPUComputeScript() { return GPUComputeScript; }

#if WITH_EDITORONLY_DATA
	/** 'Source' data/graphs for the scripts used by this emitter. */
	UPROPERTY()
	class UNiagaraScriptSourceBase*	GraphSource;

	/** Should we enable rapid iteration removal if the system is also set to remove rapid iteration parameters on compile? This value defaults to true.*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter")
	uint32 bBakeOutRapidIteration : 1;

	bool NIAGARA_API AreAllScriptAndSourcesSynchronized() const;
	void NIAGARA_API OnPostCompile();

	UNiagaraEmitter* MakeRecursiveDeepCopy(UObject* DestOuter) const;
	UNiagaraEmitter* MakeRecursiveDeepCopy(UObject* DestOuter, TMap<const UObject*, UObject*>& ExistingConversions) const;
	void NIAGARA_API InvalidateCompileResults();

	/* Gets a Guid which is updated any time data in this emitter is changed. */
	FGuid NIAGARA_API GetChangeId() const;
	
	/** Data used by the editor to maintain UI state etc.. */
	UPROPERTY()
	UObject* EditorData;

	/** Internal: The thumbnail image.*/
	UPROPERTY()
	class UTexture2D* ThumbnailImage;

	/** Internal: Indicates the thumbnail image is out of date.*/
	UPROPERTY()
	uint32 ThumbnailImageOutOfDate : 1;

	/* If this emitter is exposed to the library. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable)
	bool bExposeToLibrary;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable)
	bool bIsTemplateAsset;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable)
	FText TemplateAssetDescription;
	
	/** Callback issued whenever a VM compilation successfully happened (even if the results are a script that cannot be executed due to errors)*/
	NIAGARA_API FOnEmitterCompiled& OnEmitterVMCompiled();

	NIAGARA_API static bool GetForceCompileOnLoad();

	/** Whether or not this emitter is synchronized with its parent emitter. */
	NIAGARA_API bool IsSynchronizedWithParent() const;

	/** Merges in any changes from the parent emitter into this emitter. */
	NIAGARA_API INiagaraMergeManager::FMergeEmitterResults MergeChangesFromParent();

	/** Whether or not this emitter uses the supplied emitter */
	bool UsesEmitter(const UNiagaraEmitter& InEmitter) const;

	/** Duplicates this emitter, but prevents the duplicate from merging in changes from the parent emitter.  The resulting duplicate will have no parent information. */
	NIAGARA_API UNiagaraEmitter* DuplicateWithoutMerging(UObject* InOuter);
#endif

	float GetSpawnCountScale(int EffectsQuality = -1); 

	/** Is this emitter allowed to be enabled by the current system detail level. */
	NIAGARA_API bool IsAllowedByDetailLevel(int32 DetailLevel)const;
	NIAGARA_API bool RequiresPersistantIDs()const;

	NIAGARA_API bool IsValid()const;
	NIAGARA_API bool IsReadyToRun() const;
	bool UsesScript(const UNiagaraScript* Script)const;
	//bool UsesDataInterface(UNiagaraDataInterface* Interface);
	bool UsesCollection(const class UNiagaraParameterCollection* Collection)const;

	FString NIAGARA_API GetUniqueEmitterName()const;
	bool NIAGARA_API SetUniqueEmitterName(const FString& InName);

	const TArray<UNiagaraRendererProperties*>& GetRenderers() const { return RendererProperties; }
	TArray<UNiagaraRendererProperties*> GetEnabledRenderers() const;

	void NIAGARA_API AddRenderer(UNiagaraRendererProperties* Renderer);

	void NIAGARA_API RemoveRenderer(UNiagaraRendererProperties* Renderer);

	FORCEINLINE const TArray<FNiagaraEventScriptProperties>& GetEventHandlers() const { return EventHandlerScriptProps; }

	/* Gets a pointer to an event handler by script usage id.  This method is potentially unsafe because modifications to
	   the event handler array can make this pointer become invalid without warning. */
	NIAGARA_API FNiagaraEventScriptProperties* GetEventHandlerByIdUnsafe(FGuid ScriptUsageId);

	void NIAGARA_API AddEventHandler(FNiagaraEventScriptProperties EventHandler);

	void NIAGARA_API RemoveEventHandlerByUsageId(FGuid EventHandlerUsageId);

	NIAGARA_API const TArray<UNiagaraShaderStageBase*>& GetShaderStages() const { return ShaderStages; }

	NIAGARA_API UNiagaraShaderStageBase* GetShaderStageById(FGuid ScriptUsageId) const;

	void NIAGARA_API AddShaderStage(UNiagaraShaderStageBase* ShaderStage);

	void NIAGARA_API RemoveShaderStage(UNiagaraShaderStageBase* ShaderStage);

	void NIAGARA_API MoveShaderStageToIndex(UNiagaraShaderStageBase* ShaderStage, int32 TargetIndex);

	/* Gets whether or not the supplied event generator id matches an event generator which is shared between the particle spawn and update scrips. */
	bool IsEventGeneratorShared(FName EventGeneratorId) const;

	TStatId GetStatID(bool bGameThread, bool bConcurrent) const;

	/* This is used by the emitter instances to report runtime allocations to reduce reallocation in future simulation runs. */
	int32 AddRuntimeAllocation(uint64 ReporterHandle, int32 AllocationCount);

	/* Returns the number of max expected particles for memory allocations. */
	NIAGARA_API int32 GetMaxParticleCountEstimate();

#if WITH_EDITORONLY_DATA
	NIAGARA_API UNiagaraEmitter* GetParent() const;

	NIAGARA_API void RemoveParent();

	NIAGARA_API void SetParent(UNiagaraEmitter& InParent);
#endif

	void InitFastPathAttributeNames();

	UPROPERTY(EditAnywhere, Category = "Script Fast Path")
	FNiagaraFastPath_Module_EmitterScalability EmitterScalability;

	UPROPERTY(EditAnywhere, Category = "Script Fast Path")
	FNiagaraFastPath_Module_EmitterLifeCycle EmitterLifeCycle;

	UPROPERTY(EditAnywhere, Category = "Script Fast Path")
	TArray<FNiagaraFastPath_Module_SpawnRate> SpawnRate;

	UPROPERTY(EditAnywhere, Category = "Script Fast Path")
	TArray<FNiagaraFastPath_Module_SpawnPerUnit> SpawnPerUnit;

	UPROPERTY(EditAnywhere, Category = "Script Fast Path")
	TArray<FNiagaraFastPath_Module_SpawnBurstInstantaneous> SpawnBurstInstantaneous;

	UPROPERTY()
	FNiagaraFastPathAttributeNames SpawnFastPathAttributeNames;

	UPROPERTY()
	FNiagaraFastPathAttributeNames UpdateFastPathAttributeNames;

protected:
	virtual void BeginDestroy() override;

#if WITH_EDITORONLY_DATA
private:
	void UpdateFromMergedCopy(const INiagaraMergeManager& MergeManager, UNiagaraEmitter* MergedEmitter);

	void SyncEmitterAlias(const FString& InOldName, const FString& InNewName);

	void UpdateChangeId(const FString& Reason);

	void ScriptRapidIterationParameterChanged();

	void ShaderStageChanged();

	void RendererChanged();

	void GraphSourceChanged();

private:
	/** Adjusted every time that we compile this emitter. Lets us know that we might differ from any cached versions.*/
	UPROPERTY()
	FGuid ChangeId;

	/** A multicast delegate which is called whenever all the scripts for this emitter have been compiled (successfully or not). */
	FOnEmitterCompiled OnVMScriptCompiledDelegate;
#endif

	UPROPERTY()
	FString UniqueEmitterName;

	UPROPERTY()
	TArray<UNiagaraRendererProperties*> RendererProperties;

	UPROPERTY(EditAnywhere, Category = "Events", meta=(NiagaraNoMerge))
	TArray<FNiagaraEventScriptProperties> EventHandlerScriptProps;

	UPROPERTY(meta = (NiagaraNoMerge))
	TArray<UNiagaraShaderStageBase*> ShaderStages;

	UPROPERTY()
	UNiagaraScript* GPUComputeScript;

	UPROPERTY()
	TArray<FName> SharedEventGeneratorIds;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UNiagaraEmitter* Parent;

	UPROPERTY()
	UNiagaraEmitter* ParentAtLastMerge;
#endif

#if WITH_EDITOR
	FOnPropertiesChanged OnPropertiesChangedDelegate;
	FOnRenderersChanged OnRenderersChangedDelegate;
#endif

	void GenerateStatID()const;
#if STATS
	mutable TStatId StatID_GT;
	mutable TStatId StatID_GT_CNC;
	mutable TStatId StatID_RT;
	mutable TStatId StatID_RT_CNC;
#endif

	MemoryRuntimeEstimation RuntimeEstimation;
	FCriticalSection EstimationCriticalSection;
};


