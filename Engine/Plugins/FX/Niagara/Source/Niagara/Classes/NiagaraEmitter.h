// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "NiagaraScript.h"
#include "NiagaraMessageDataBase.h"
#include "INiagaraMergeManager.h"
#include "NiagaraEffectType.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraBoundsCalculator.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraParameterDefinitionsBase.h"
#include "NiagaraParameterDefinitionsSubscriber.h"
#include "NiagaraEmitter.generated.h"

class UMaterial;
class UNiagaraEmitter;
class UNiagaraEventReceiverEmitterAction;
class UNiagaraSimulationStageBase;
class UNiagaraEditorDataBase;

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

	FNiagaraEventGeneratorProperties() = default;

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
	int32 MaxEventsPerFrame = 64; //TODO - More complex allocation so that we can grow dynamically if more space is needed ?

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

/** Legacy struct for spawn count scale overrides. This is now done in FNiagaraEmitterScalabilityOverrides*/
USTRUCT()
struct FNiagaraDetailsLevelScaleOverrides 
{
	GENERATED_BODY()

	FNiagaraDetailsLevelScaleOverrides();
	UPROPERTY()
	float Low;
	UPROPERTY()
	float Medium;
	UPROPERTY()
	float High;
	UPROPERTY()
	float Epic;
	UPROPERTY()
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
class UNiagaraEmitter : public UObject, public INiagaraParameterDefinitionsSubscriber
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
	virtual void PostRename(UObject* OldOuter, const FName OldName) override;
	virtual void PostDuplicate(EDuplicateMode::Type DuplicateMode) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
	NIAGARA_API FOnPropertiesChanged& OnPropertiesChanged();
	NIAGARA_API FOnRenderersChanged& OnRenderersChanged();
	/** Helper method for when a rename has been detected within the graph. Covers renaming the internal renderer bindings.*/
	NIAGARA_API void HandleVariableRenamed(const FNiagaraVariable& InOldVariable, const FNiagaraVariable& InNewVariable, bool bUpdateContexts);
	/** Helper method for when a rename has been detected within the graph. Covers resetting the internal renderer bindings.*/
	NIAGARA_API void HandleVariableRemoved(const FNiagaraVariable& InOldVariable, bool bUpdateContexts);

	/** Helper method for binding the notifications needed for proper editor integration. */
	NIAGARA_API void BindNotifications();
#endif
	virtual bool NeedsLoadForTargetPlatform(const ITargetPlatform* TargetPlatform) const override;
	void Serialize(FArchive& Ar)override;
	virtual void PostInitProperties() override;
	virtual void PostLoad() override;
	virtual bool IsEditorOnly() const override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	//End UObject Interface

#if WITH_EDITORONLY_DATA
	//~ Begin INiagaraParameterDefinitionsSubscriber Interface
	virtual const TArray<FParameterDefinitionsSubscription>& GetParameterDefinitionsSubscriptions() const override { return ParameterDefinitionsSubscriptions; };
	virtual TArray<FParameterDefinitionsSubscription>& GetParameterDefinitionsSubscriptions() override { return ParameterDefinitionsSubscriptions; };

	/** Get all UNiagaraScriptSourceBase of this subscriber. */
	virtual TArray<UNiagaraScriptSourceBase*> GetAllSourceScripts() override;

	/** Get the path to the UObject of this subscriber. */
	virtual FString GetSourceObjectPathName() const override;

	/** Get All adapters to editor only script vars owned directly by this subscriber. */
	virtual TArray<UNiagaraEditorParametersAdapterBase*> GetEditorOnlyParametersAdapters() override;
	//~ End INiagaraParameterDefinitionsSubscriber Interface
#endif

	bool IsEnabledOnPlatform(const FString& PlatformName)const;

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

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FNiagaraEmitterScriptProperties EmitterSpawnScriptProps;

	UPROPERTY()
	FNiagaraEmitterScriptProperties EmitterUpdateScriptProps;

	/** A whitelist of Particle attributes (e.g. "Particle.Position" or "Particle.Age") that will not be removed from the DataSet  even if they aren't read by the VM.
	    Used in conjunction with UNiagaraSystem::bTrimAttributes */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter")
	TArray<FString> AttributesToPreserve;
#endif

	UPROPERTY(EditAnywhere, Category = "Emitter")
	ENiagaraSimTarget SimTarget;
	
	/** The fixed bounding box value. bFixedBounds is the condition whether the fixed bounds can be edited. */
	UPROPERTY(EditAnywhere, Category = "Emitter", meta = (EditCondition = "bFixedBounds"))
	FBox FixedBounds;
	
	UPROPERTY()
	int32 MinDetailLevel_DEPRECATED;
	UPROPERTY()
	int32 MaxDetailLevel_DEPRECATED;
	UPROPERTY()
	FNiagaraDetailsLevelScaleOverrides GlobalSpawnCountScaleOverrides_DEPRECATED;
	
	UPROPERTY(EditAnywhere, Category = "Scalability")
	FNiagaraPlatformSet Platforms;

	UPROPERTY(EditAnywhere, Category = "Scalability")
	FNiagaraEmitterScalabilityOverrides ScalabilityOverrides;

	/** When enabled, this will spawn using interpolated parameter values and perform a partial update at spawn time. This adds significant additional cost for spawning but will produce much smoother spawning for high spawn rates, erratic frame rates and fast moving emitters. */
	UPROPERTY(EditAnywhere, Category = "Emitter")
	uint32 bInterpolatedSpawning : 1;

	/** Whether or not fixed bounds are enabled. */
	UPROPERTY(EditAnywhere, Category = "Emitter", meta = (InlineEditConditionToggle))
	uint32 bFixedBounds : 1;

	/** Whether to use the min detail or not. */
	UPROPERTY()
	uint32 bUseMinDetailLevel_DEPRECATED : 1;
	
	/** Whether to use the min detail or not. */
	UPROPERTY()
	uint32 bUseMaxDetailLevel_DEPRECATED : 1;

	/** Legacy bool to control overriding the global spawn count scales. */
	UPROPERTY()
	uint32 bOverrideGlobalSpawnCountScale_DEPRECATED : 1;

	/** Do particles in this emitter require a persistent ID? */
	UPROPERTY(EditAnywhere, Category = "Emitter")
	uint32 bRequiresPersistentIDs : 1;

	/** Performance option to allow event based spawning to be combined into a single spawn.  This will result in a single exec from 0 to number of particles rather than several, when using ExecIndex() it is recommended not to do this. */
	UPROPERTY(EditAnywhere, Category = "Emitter")
	uint32 bCombineEventSpawn : 1;

	/** Limits the delta time per tick to prevent simulation spikes due to frame lags. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter", meta = (EditCondition = "bLimitDeltaTime"))
	float MaxDeltaTimePerTick;

	/** Get the default shader stage index. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Simulation Stages", meta = (EditCondition = "bDeprecatedShaderStagesEnabled", DisplayAfter = "bDeprecatedShaderStagesEnabled"))
	uint32 DefaultShaderStageIndex;

	/** Get the number of shader stages that we fire off. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Simulation Stages", meta = (EditCondition = "bDeprecatedShaderStagesEnabled", DisplayAfter = "DefaultShaderStageIndex"))
	uint32 MaxUpdateIterations;

	/** Get whether or not shaderstages spwn. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Simulation Stages", meta = (EditCondition = "bDeprecatedShaderStagesEnabled", DisplayAfter = "MaxUpdateIterations"))
	TSet<uint32> SpawnStages;

	/** Get whether or not to use simulation stages. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Simulation Stages", meta = (DisplayName = "Enable Simulation Stages (Experimental GPU Only)"))
	uint32 bSimulationStagesEnabled : 1;

	/** Get whether or not to use shader stages. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Simulation Stages", meta = (DisplayName = "Enable Deprecated Shader Stages (Experimental GPU Only)"))
	uint32 bDeprecatedShaderStagesEnabled : 1;

	/** Whether to limit the max tick delta time or not. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter", meta = (InlineEditConditionToggle))
	uint32 bLimitDeltaTime : 1;

	void NIAGARA_API GetScripts(TArray<UNiagaraScript*>& OutScripts, bool bCompilableOnly = true, bool bEnabledOnly = false) const;

	NIAGARA_API UNiagaraScript* GetScript(ENiagaraScriptUsage Usage, FGuid UsageId);

	NIAGARA_API UNiagaraScript* GetGPUComputeScript() { return GPUComputeScript; }
	NIAGARA_API const UNiagaraScript* GetGPUComputeScript() const { return GPUComputeScript; }

	void CacheFromCompiledData(const FNiagaraDataSetCompiledData* CompiledData);
	void CacheFromShaderCompiled();

	NIAGARA_API void UpdateEmitterAfterLoad();

#if WITH_EDITORONLY_DATA
	/** 'Source' data/graphs for the scripts used by this emitter. */
	UPROPERTY()
	class UNiagaraScriptSourceBase*	GraphSource;

	/** Should we enable rapid iteration removal if the system is also set to remove rapid iteration parameters on compile? This value defaults to true.*/
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter", meta = (DisplayName = "Supports Baked Rapid Iteration"))
	uint32 bBakeOutRapidIteration : 1;

	bool NIAGARA_API AreAllScriptAndSourcesSynchronized() const;
	void NIAGARA_API OnPostCompile();

	void NIAGARA_API InvalidateCompileResults();

	/* Gets a Guid which is updated any time data in this emitter is changed. */
	FGuid NIAGARA_API GetChangeId() const;

	NIAGARA_API UNiagaraEditorDataBase* GetEditorData() const;
	NIAGARA_API UNiagaraEditorParametersAdapterBase* GetEditorParameters();

	NIAGARA_API void SetEditorData(UNiagaraEditorDataBase* InEditorData);

	/** Internal: The thumbnail image.*/
	UPROPERTY()
	class UTexture2D* ThumbnailImage;

	/** Internal: Indicates the thumbnail image is out of date.*/
	UPROPERTY()
	uint32 ThumbnailImageOutOfDate : 1;

	/* If this emitter is exposed to the library. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable)
	bool bExposeToLibrary;
	
	UPROPERTY()
	bool bIsTemplateAsset_DEPRECATED;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable)
	ENiagaraScriptTemplateSpecification TemplateSpecification;
	
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable)
	FText TemplateAssetDescription;

	/** Category to collate this emitter into for "add new emitter" dialogs.*/
	UPROPERTY(AssetRegistrySearchable, EditAnywhere, Category = Script)
	FText Category;

	UPROPERTY()
	TArray<UNiagaraScript*> ScratchPadScripts;

	UPROPERTY()
	TArray<UNiagaraScript*> ParentScratchPadScripts;
	
	/** Callback issued whenever a VM compilation successfully happened (even if the results are a script that cannot be executed due to errors)*/
	NIAGARA_API FOnEmitterCompiled& OnEmitterVMCompiled();

	/** Callback issued whenever a VM compilation successfully happened (even if the results are a script that cannot be executed due to errors)*/
	NIAGARA_API FOnEmitterCompiled& OnEmitterGPUCompiled();

	/** Callback issued whenever a GPU compilation successfully happened (even if the results are a script that cannot be executed due to errors)*/
	NIAGARA_API FOnEmitterCompiled& OnGPUCompilationComplete()
	{
		return OnGPUScriptCompiledDelegate;
	}
	
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

	FORCEINLINE const FNiagaraEmitterScalabilitySettings& GetScalabilitySettings()const { return CurrentScalabilitySettings; }

	/** Returns true if this emitter's platform filter allows it on this platform and quality level. */
	NIAGARA_API bool IsAllowedByScalability()const;

	NIAGARA_API bool RequiresPersistentIDs() const;

	NIAGARA_API bool IsValid()const;
	NIAGARA_API bool IsReadyToRun() const;
	bool UsesScript(const UNiagaraScript* Script)const;
	//bool UsesDataInterface(UNiagaraDataInterface* Interface);
	bool UsesCollection(const class UNiagaraParameterCollection* Collection)const;
	bool CanObtainParticleAttribute(const FNiagaraVariableBase& InVar) const;
	bool CanObtainEmitterAttribute(const FNiagaraVariableBase& InVarWithUniqueNameNamespace) const;
	bool CanObtainSystemAttribute(const FNiagaraVariableBase& InVar) const;
	bool CanObtainUserVariable(const FNiagaraVariableBase& InVar) const;

#if !UE_BUILD_SHIPPING
	const TCHAR* GetDebugSimName() const { return *DebugSimName; }
#endif

	FString NIAGARA_API GetUniqueEmitterName()const;
	bool NIAGARA_API SetUniqueEmitterName(const FString& InName);

	const TArray<UNiagaraRendererProperties*>& GetRenderers() const { return RendererProperties; }

	template<typename TAction>
	void ForEachEnabledRenderer(TAction Func) const;

	template<typename TAction>
	void ForEachScript(TAction Func) const;

	void NIAGARA_API AddRenderer(UNiagaraRendererProperties* Renderer);

	void NIAGARA_API RemoveRenderer(UNiagaraRendererProperties* Renderer);

	FORCEINLINE const TArray<FNiagaraEventScriptProperties>& GetEventHandlers() const { return EventHandlerScriptProps; }

	/* Gets a pointer to an event handler by script usage id.  This method is potentially unsafe because modifications to
	   the event handler array can make this pointer become invalid without warning. */
	NIAGARA_API FNiagaraEventScriptProperties* GetEventHandlerByIdUnsafe(FGuid ScriptUsageId);

	void NIAGARA_API AddEventHandler(FNiagaraEventScriptProperties EventHandler);

	void NIAGARA_API RemoveEventHandlerByUsageId(FGuid EventHandlerUsageId);

	NIAGARA_API const TArray<UNiagaraSimulationStageBase*>& GetSimulationStages() const { return SimulationStages; }

	NIAGARA_API UNiagaraSimulationStageBase* GetSimulationStageById(FGuid ScriptUsageId) const;

	void NIAGARA_API AddSimulationStage(UNiagaraSimulationStageBase* SimulationStage);

	void NIAGARA_API RemoveSimulationStage(UNiagaraSimulationStageBase* SimulationStage);

	void NIAGARA_API MoveSimulationStageToIndex(UNiagaraSimulationStageBase* SimulationStage, int32 TargetIndex);

	/* Gets whether or not the supplied event generator id matches an event generator which is shared between the particle spawn and update scrips. */
	bool IsEventGeneratorShared(FName EventGeneratorId) const;

	TStatId GetStatID(bool bGameThread, bool bConcurrent) const;

	void ClearRuntimeAllocationEstimate(uint64 ReportHandle = INDEX_NONE);
	/* This is used by the emitter instances to report runtime allocations to reduce reallocation in future simulation runs. */
	int32 AddRuntimeAllocation(uint64 ReporterHandle, int32 AllocationCount);
#if STATS
	NIAGARA_API FNiagaraStatDatabase& GetStatData() { return StatDatabase; }
#endif

	/* Returns the number of max expected particles for memory allocations. */
	NIAGARA_API int32 GetMaxParticleCountEstimate();

#if WITH_EDITORONLY_DATA
	NIAGARA_API UNiagaraEmitter* GetParent() const;

	NIAGARA_API UNiagaraEmitter* GetParentAtLastMerge() const;

	NIAGARA_API void RemoveParent();

	NIAGARA_API void SetParent(UNiagaraEmitter& InParent);

	NIAGARA_API	void Reparent(UNiagaraEmitter& InParent);

	NIAGARA_API void NotifyScratchPadScriptsChanged();
#endif

	void OnScalabilityCVarChanged();

#if WITH_EDITORONLY_DATA
	NIAGARA_API const TMap<FGuid, UNiagaraMessageDataBase*>& GetMessages() const { return MessageKeyToMessageMap; };
	NIAGARA_API void AddMessage(const FGuid& MessageKey, UNiagaraMessageDataBase* NewMessage) { MessageKeyToMessageMap.Add(MessageKey, NewMessage); };
	NIAGARA_API void RemoveMessage(const FGuid& MessageKey) { MessageKeyToMessageMap.Remove(MessageKey); };
	void RemoveMessageDelegateable(const FGuid MessageKey) { MessageKeyToMessageMap.Remove(MessageKey); };
#endif

	bool RequiresViewUniformBuffer() const { return bRequiresViewUniformBuffer; }

	uint32 GetMaxInstanceCount() const { return MaxInstanceCount; }

	TConstArrayView<TUniquePtr<FNiagaraBoundsCalculator>> GetBoundsCalculators() const { return MakeArrayView(BoundsCalculators); }

protected:
	virtual void BeginDestroy() override;

	void ResolveScalabilitySettings();

#if WITH_EDITORONLY_DATA
private:
	void UpdateFromMergedCopy(const INiagaraMergeManager& MergeManager, UNiagaraEmitter* MergedEmitter);

	void SyncEmitterAlias(const FString& InOldName, const FString& InNewName);

	void UpdateChangeId(const FString& Reason);

	void ScriptRapidIterationParameterChanged();

	void SimulationStageChanged();

	void RendererChanged();

	void GraphSourceChanged();

	void PersistentEditorDataChanged();

private:
	/** Adjusted every time that we compile this emitter. Lets us know that we might differ from any cached versions.*/
	UPROPERTY()
	FGuid ChangeId;

	/** Data used by the editor to maintain UI state etc.. */
	UPROPERTY()
	UNiagaraEditorDataBase* EditorData;

	/** Wrapper for editor only parameters. */
	UPROPERTY()
	UNiagaraEditorParametersAdapterBase* EditorParameters;

	/** A multicast delegate which is called whenever all the scripts for this emitter have been compiled (successfully or not). */
	FOnEmitterCompiled OnVMScriptCompiledDelegate;

	/** A multicast delegate which is called whenever all the scripts for this emitter have been compiled (successfully or not). */
	FOnEmitterCompiled OnGPUScriptCompiledDelegate;

	void RaiseOnEmitterGPUCompiled(UNiagaraScript* InScript, const FGuid& ScriptVersion);
#endif

	bool bFullyLoaded = false;

#if !UE_BUILD_SHIPPING
	FString DebugSimName;
#endif

	UPROPERTY()
	FString UniqueEmitterName;

	UPROPERTY()
	TArray<UNiagaraRendererProperties*> RendererProperties;

	UPROPERTY(EditAnywhere, Category = "Events", meta=(NiagaraNoMerge))
	TArray<FNiagaraEventScriptProperties> EventHandlerScriptProps;

	UPROPERTY(meta = (NiagaraNoMerge))
	TArray<UNiagaraSimulationStageBase*> SimulationStages;

	UPROPERTY()
	UNiagaraScript* GPUComputeScript;

	UPROPERTY()
	TArray<FName> SharedEventGeneratorIds;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	UNiagaraEmitter* Parent;

	UPROPERTY()
	UNiagaraEmitter* ParentAtLastMerge;

	/** Subscriptions to definitions of parameters. */
	UPROPERTY()
	TArray<FParameterDefinitionsSubscription> ParameterDefinitionsSubscriptions;
#endif

#if WITH_EDITOR
	FOnPropertiesChanged OnPropertiesChangedDelegate;
	FOnRenderersChanged OnRenderersChangedDelegate;
#endif

	void EnsureScriptsPostLoaded();

	void GenerateStatID()const;
#if STATS
	mutable TStatId StatID_GT;
	mutable TStatId StatID_GT_CNC;
	mutable TStatId StatID_RT;
	mutable TStatId StatID_RT_CNC;
	FNiagaraStatDatabase StatDatabase;
#endif

	/** Indicates that the GPU script requires the view uniform buffer. */
	uint32 bRequiresViewUniformBuffer : 1;

	/** Maximum number of instances we can create for this emitter. */
	uint32 MaxInstanceCount = 0;

	/** Optional list of bounds calculators. */
	TArray<TUniquePtr<FNiagaraBoundsCalculator>, TInlineAllocator<1>> BoundsCalculators;

	MemoryRuntimeEstimation RuntimeEstimation;
	FCriticalSection EstimationCriticalSection;

	FNiagaraEmitterScalabilitySettings CurrentScalabilitySettings;

#if WITH_EDITORONLY_DATA
	/** Messages associated with the Emitter asset. */
	UPROPERTY()
	TMap<FGuid, UNiagaraMessageDataBase*> MessageKeyToMessageMap;
#endif
};


template<typename TAction>
void UNiagaraEmitter::ForEachEnabledRenderer(TAction Func) const
{
	for (UNiagaraRendererProperties* Renderer : RendererProperties)
	{
		if (Renderer && Renderer->GetIsEnabled() && Renderer->IsSimTargetSupported(this->SimTarget))
		{
			Func(Renderer);
		}
	}
}
template<typename TAction>
void UNiagaraEmitter::ForEachScript(TAction Func) const
{
	Func(SpawnScriptProps.Script);
	Func(UpdateScriptProps.Script);

	Func(GPUComputeScript);

	for (auto& EventScriptProps : EventHandlerScriptProps)
	{
		Func(EventScriptProps.Script);
	}
}
