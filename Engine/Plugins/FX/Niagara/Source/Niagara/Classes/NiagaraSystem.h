// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticlePerfStats.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraBakerSettings.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraEffectType.h"
#include "NiagaraParameterDefinitionsSubscriber.h"

#include "NiagaraSystem.generated.h"

#if WITH_EDITORONLY_DATA
class UNiagaraEditorDataBase;
#endif

USTRUCT()
struct FNiagaraEmitterCompiledData
{
	GENERATED_USTRUCT_BODY()

	FNiagaraEmitterCompiledData();
	
	/** Attribute names in the data set that are driving each emitter's spawning. */
	UPROPERTY()
	TArray<FName> SpawnAttributes;

	/** Explicit list of Niagara Variables to bind to Emitter instances. */
	UPROPERTY()
	FNiagaraVariable EmitterSpawnIntervalVar;

	UPROPERTY()
	FNiagaraVariable EmitterInterpSpawnStartDTVar;

	UPROPERTY()
	FNiagaraVariable EmitterSpawnGroupVar;

	UPROPERTY()
	FNiagaraVariable EmitterAgeVar;

	UPROPERTY()
	FNiagaraVariable EmitterRandomSeedVar;

	UPROPERTY()
	FNiagaraVariable EmitterInstanceSeedVar;

	UPROPERTY()
	FNiagaraVariable EmitterTotalSpawnedParticlesVar;

	/** Per-Emitter DataSet Data. */
	UPROPERTY()
	FNiagaraDataSetCompiledData DataSetCompiledData;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	FNiagaraDataSetCompiledData GPUCaptureDataSetCompiledData;
#endif
};

USTRUCT()
struct FNiagaraParameterDataSetBinding
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	int32 ParameterOffset = 0;

	UPROPERTY()
	int32 DataSetComponentOffset = 0;
};

USTRUCT()
struct FNiagaraParameterDataSetBindingCollection
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<FNiagaraParameterDataSetBinding> FloatOffsets;

	UPROPERTY()
	TArray<FNiagaraParameterDataSetBinding> Int32Offsets;

#if WITH_EDITORONLY_DATA
	template<typename BufferType>
	void Build(const FNiagaraDataSetCompiledData& DataSet)
	{
		BuildInternal(BufferType::GetVariables(), DataSet, TEXT(""), TEXT(""));
	}

	template<typename BufferType>
	void Build(const FNiagaraDataSetCompiledData& DataSet, const FString& NamespaceBase, const FString& NamespaceReplacement)
	{
		BuildInternal(BufferType::GetVariables(), DataSet, NamespaceBase, NamespaceReplacement);
	}

protected:
	void BuildInternal(const TArray<FNiagaraVariable>& ParameterVars, const FNiagaraDataSetCompiledData& DataSet, const FString& NamespaceBase, const FString& NamespaceReplacement);

#endif
};

USTRUCT()
struct FNiagaraSystemCompiledData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	FNiagaraParameterStore InstanceParamStore;

	UPROPERTY()
	FNiagaraDataSetCompiledData DataSetCompiledData;

	UPROPERTY()
	FNiagaraDataSetCompiledData SpawnInstanceParamsDataSetCompiledData;

	UPROPERTY()
	FNiagaraDataSetCompiledData UpdateInstanceParamsDataSetCompiledData;

	UPROPERTY()
	FNiagaraParameterDataSetBindingCollection SpawnInstanceGlobalBinding;
	UPROPERTY()
	FNiagaraParameterDataSetBindingCollection SpawnInstanceSystemBinding;
	UPROPERTY()
	FNiagaraParameterDataSetBindingCollection SpawnInstanceOwnerBinding;
	UPROPERTY()
	TArray<FNiagaraParameterDataSetBindingCollection> SpawnInstanceEmitterBindings;

	UPROPERTY()
	FNiagaraParameterDataSetBindingCollection UpdateInstanceGlobalBinding;
	UPROPERTY()
	FNiagaraParameterDataSetBindingCollection UpdateInstanceSystemBinding;
	UPROPERTY()
	FNiagaraParameterDataSetBindingCollection UpdateInstanceOwnerBinding;
	UPROPERTY()
	TArray<FNiagaraParameterDataSetBindingCollection> UpdateInstanceEmitterBindings;
};

USTRUCT()
struct FEmitterCompiledScriptPair
{
	GENERATED_USTRUCT_BODY()
	
	bool bResultsReady;
	UNiagaraEmitter* Emitter;
	UNiagaraScript* CompiledScript;
	uint32 PendingJobID = INDEX_NONE; // this is the ID for any active shader compiler worker job
	FNiagaraVMExecutableDataId CompileId;
	TSharedPtr<FNiagaraVMExecutableData> CompileResults;
	int32 ParentIndex = INDEX_NONE;
};

USTRUCT()
struct FNiagaraSystemCompileRequest
{
	GENERATED_USTRUCT_BODY()

	double StartTime = 0.0;

	UPROPERTY()
	TArray<UObject*> RootObjects;

	TArray<FEmitterCompiledScriptPair> EmitterCompiledScriptPairs;
	
	TMap<UNiagaraScript*, TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> > MappedData;

	bool bIsValid = true;

	bool bForced = false;
};

struct FNiagaraEmitterExecutionIndex
{
	FNiagaraEmitterExecutionIndex() { bStartNewOverlapGroup = false; EmitterIndex = 0; }

	/** Flag to denote if the batcher should start a new overlap group, i.e. when we have a dependency ensure we don't overlap with the emitter we depend on. */
	uint32 bStartNewOverlapGroup : 1;
	/** Emitter index to use */
	uint32 EmitterIndex : 31;
};

struct FNiagaraRendererExecutionIndex
{
	/** The index of the emitter */
	uint32 EmitterIndex = INDEX_NONE;
	/** The index of the renderer in the emitter's list */
	uint32 EmitterRendererIndex = INDEX_NONE;
	/** The index of the renderer in the entire system */
	uint32 SystemRendererIndex = INDEX_NONE;
};

/** Container for multiple emitters that combine together to create a particle system effect.*/
UCLASS(BlueprintType)
class NIAGARA_API UNiagaraSystem : public UFXSystemAsset, public INiagaraParameterDefinitionsSubscriber
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSystemCompiled, UNiagaraSystem*);
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSystemPostEditChange, UNiagaraSystem*);
#endif
	//TestChange

	UNiagaraSystem(FVTableHelper& Helper);

	//~ UObject interface
	void PostInitProperties();
	void Serialize(FArchive& Ar)override;
	virtual void PostLoad() override; 
	virtual void BeginDestroy() override;
	virtual void PreSave(const class ITargetPlatform* TargetPlatform) override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyThatWillChange)override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override; 
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform) override;
	//~ End UObject interface

	/** Helper method to handle when an internal variable has been renamed. Renames any downstream dependencies in the emitters or exposed variables.*/
	void HandleVariableRenamed(const FNiagaraVariable& InOldVariable, const FNiagaraVariable& InNewVariable, bool bUpdateContexts);
	/** Helper method to handle when an internal variable has been removed. Resets any downstream dependencies in the emitters or exposed variables.*/
	void HandleVariableRemoved(const FNiagaraVariable& InOldVariable, bool bUpdateContexts);
#endif

#if WITH_EDITORONLY_DATA
	//~ Begin INiagaraParameterDefinitionsSubscriber interface
	virtual const TArray<FParameterDefinitionsSubscription>& GetParameterDefinitionsSubscriptions() const override { return ParameterDefinitionsSubscriptions; };
	virtual TArray<FParameterDefinitionsSubscription>& GetParameterDefinitionsSubscriptions() override { return ParameterDefinitionsSubscriptions; };

	/** Get all UNiagaraScriptSourceBase of this subscriber. */
	virtual TArray<UNiagaraScriptSourceBase*> GetAllSourceScripts() override;

	/** Get the path to the UObject of this subscriber. */
	virtual FString GetSourceObjectPathName() const override;

	/** Get All adapters to editor only script vars owned directly by this subscriber. */
	virtual TArray<UNiagaraEditorParametersAdapterBase*> GetEditorOnlyParametersAdapters() override;

	/** Get all subscribers that are owned by this subscriber.
	 *  Note: Implemented for synchronizing UNiagaraSystem. UNiagaraSystem returns all UNiagaraEmitters it owns to call SynchronizeWithParameterDefinitions for each.
	 */
	virtual TArray<INiagaraParameterDefinitionsSubscriber*> GetOwnedParameterDefinitionsSubscribers() override;
	//~ End INiagaraParameterDefinitionsSubscriber interface
#endif 

	/** Gets an array of the emitter handles. */
	const TArray<FNiagaraEmitterHandle>& GetEmitterHandles();
	const TArray<FNiagaraEmitterHandle>& GetEmitterHandles()const;

private:
	bool IsValidInternal() const;
	
public:
	/** Returns true if this system is valid and can be instanced. False otherwise. */
	bool IsValid() const { return FPlatformProperties::RequiresCookedData() ? bIsValidCached : IsValidInternal(); }

#if WITH_EDITORONLY_DATA
	/** Adds a new emitter handle to this System.  The new handle exposes an Instance value which is a copy of the
		original asset. */
	FNiagaraEmitterHandle AddEmitterHandle(UNiagaraEmitter& SourceEmitter, FName EmitterName);

	/** Adds a new emitter handle to this system without copying the original asset. This should only be used for temporary systems and never for live assets. */
	void AddEmitterHandleDirect(FNiagaraEmitterHandle& EmitterHandleToAdd);

	/** Duplicates an existing emitter handle and adds it to the System.  The new handle will reference the same source asset,
		but will have a copy of the duplicated Instance value. */
	FNiagaraEmitterHandle DuplicateEmitterHandle(const FNiagaraEmitterHandle& EmitterHandleToDuplicate, FName EmitterName);

	/** Removes the provided emitter handle. */
	void RemoveEmitterHandle(const FNiagaraEmitterHandle& EmitterHandleToDelete);

	/** Removes the emitter handles which have an Id in the supplied set. */
	void RemoveEmitterHandlesById(const TSet<FGuid>& HandlesToRemove);

#endif


	FNiagaraEmitterHandle& GetEmitterHandle(int Idx)
	{
		check(Idx < EmitterHandles.Num());
		return EmitterHandles[Idx];
	};

	const FNiagaraEmitterHandle& GetEmitterHandle(int Idx) const
	{
		check(Idx < EmitterHandles.Num());
		return EmitterHandles[Idx];
	};

	int GetNumEmitters()
	{
		return EmitterHandles.Num();
	}

	/** From the last compile, what are the variables that were exported out of the system for external use?*/
	const FNiagaraUserRedirectionParameterStore& GetExposedParameters() const {	return ExposedParameters; }
	FNiagaraUserRedirectionParameterStore& GetExposedParameters()  { return ExposedParameters; }

	/** Gets the System script which is used to populate the System parameters and parameter bindings. */
	UNiagaraScript* GetSystemSpawnScript();
	UNiagaraScript* GetSystemUpdateScript();
	const UNiagaraScript* GetSystemSpawnScript() const;
	const UNiagaraScript* GetSystemUpdateScript() const;

	TOptional<float> GetMaxDeltaTime() const { return MaxDeltaTime; }
	const FNiagaraDataSetAccessor<ENiagaraExecutionState>& GetSystemExecutionStateAccessor() const { return SystemExecutionStateAccessor; }
	TConstArrayView<FNiagaraDataSetAccessor<ENiagaraExecutionState>> GetEmitterExecutionStateAccessors() const { return MakeArrayView(EmitterExecutionStateAccessors); }
	TConstArrayView<FNiagaraDataSetAccessor<FNiagaraSpawnInfo>> GetEmitterSpawnInfoAccessors(int32 EmitterIndex) const { return MakeArrayView(EmitterSpawnInfoAccessors[EmitterIndex]);  }
	
	/** Performs the passed action for all scripts in this system. */
	template<typename TAction>
	void ForEachScript(TAction Func) const;

private:
	bool IsReadyToRunInternal() const;

public:
	bool IsReadyToRun() const { return FPlatformProperties::RequiresCookedData() ? bIsReadyToRunCached : IsReadyToRunInternal(); }

	FORCEINLINE bool NeedsWarmup()const { return WarmupTickCount > 0 && WarmupTickDelta > SMALL_NUMBER; }
	FORCEINLINE float GetWarmupTime()const { return WarmupTime; }
	FORCEINLINE int32 GetWarmupTickCount()const { return WarmupTickCount; }
	FORCEINLINE float GetWarmupTickDelta()const { return WarmupTickDelta; }
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags)  const override;

#if STATS
	FNiagaraStatDatabase& GetStatData() { return StatDatabase; }
#endif

#if WITH_EDITORONLY_DATA
	/** Are there any pending compile requests?*/
	bool HasOutstandingCompilationRequests(bool bIncludingGPUShaders = false) const;

	/** Determines if this system has the supplied emitter as an editable and simulating emitter instance. */
	bool ReferencesInstanceEmitter(UNiagaraEmitter& Emitter);

	/** Updates the system's rapid iteration parameters from a specific emitter. */
	void RefreshSystemParametersFromEmitter(const FNiagaraEmitterHandle& EmitterHandle);

	/** Removes the system's rapid iteration parameters for a specific emitter. */
	void RemoveSystemParametersForEmitter(const FNiagaraEmitterHandle& EmitterHandle);

	/** Request that any dirty scripts referenced by this system be compiled.*/
	bool RequestCompile(bool bForce, FNiagaraSystemUpdateContext* OptionalUpdateContext = nullptr);

	/** If we have a pending compile request, is it done with yet? */
	bool PollForCompilationComplete();

	/** Blocks until all active compile jobs have finished */
	void WaitForCompilationComplete(bool bIncludingGPUShaders = false, bool bShowProgress = true);

	/** Invalidates any active compilation requests which will ignore their results. */
	void InvalidateActiveCompiles();

	/** Delegate called when the system's dependencies have all been compiled.*/
	FOnSystemCompiled& OnSystemCompiled();

	/** Delegate called on PostEditChange.*/
	FOnSystemPostEditChange& OnSystemPostEditChange();

	/** Gets editor specific data stored with this system. */
	UNiagaraEditorDataBase* GetEditorData();

	/** Gets editor specific parameters stored with this system */
	UNiagaraEditorParametersAdapterBase* GetEditorParameters();

	/** Gets editor specific data stored with this system. */
	const UNiagaraEditorDataBase* GetEditorData() const;

	/** Internal: The thumbnail image.*/
	UPROPERTY()
	class UTexture2D* ThumbnailImage;

	/** Internal: Indicates the thumbnail image is out of date.*/
	UPROPERTY()
	uint32 ThumbnailImageOutOfDate : 1;

	/* If this system is exposed to the library. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable, meta = (SkipSystemResetOnChange = "true"))
	bool bExposeToLibrary;

	UPROPERTY()
	bool bIsTemplateAsset_DEPRECATED;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable, meta = (SkipSystemResetOnChange = "true"))
	ENiagaraScriptTemplateSpecification TemplateSpecification;;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable, meta = (SkipSystemResetOnChange = "true"))
	FText TemplateAssetDescription;

	UPROPERTY()
	TArray<UNiagaraScript*> ScratchPadScripts;

	UPROPERTY(transient)
	FNiagaraParameterStore EditorOnlyAddedParameters;

	bool GetIsolateEnabled() const;
	void SetIsolateEnabled(bool bIsolate);
	
	UPROPERTY(transient)
	FNiagaraSystemUpdateContext UpdateContext;
#endif

	void UpdateSystemAfterLoad();
	void EnsureFullyLoaded() const;

	bool ShouldAutoDeactivate() const { return bAutoDeactivate; }
	bool IsLooping() const;

	const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& GetEmitterCompiledData() const { return EmitterCompiledData; };

	const FNiagaraSystemCompiledData& GetSystemCompiledData() const { return SystemCompiledData; };

	bool UsesCollection(const UNiagaraParameterCollection* Collection)const;
#if WITH_EDITORONLY_DATA
	bool UsesEmitter(const UNiagaraEmitter* Emitter) const;
	bool UsesScript(const UNiagaraScript* Script)const; 
	void ForceGraphToRecompileOnNextCheck();

	static void RequestCompileForEmitter(UNiagaraEmitter* InEmitter);
	static void RecomputeExecutionOrderForEmitter(UNiagaraEmitter* InEmitter);
	static void RecomputeExecutionOrderForDataInterface(class UNiagaraDataInterface* DataInterface);

	/** Experimental feature that allows us to bake out rapid iteration parameters during the normal compile process. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Performance")
	uint32 bBakeOutRapidIteration : 1;

	/** If true bBakeOutRapidIteration will be made to be true during cooks  */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Performance", meta = (SkipSystemResetOnChange = "true"))
	uint32 bBakeOutRapidIterationOnCook : 1;

	/** Toggles whether or not emitters within this system will try and compress their particle attributes.
	In some cases, this precision change can lead to perceivable differences, but memory costs and or performance (especially true for GPU emitters) can improve. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Performance")
	uint32 bCompressAttributes : 1;

	/** If true Particle attributes will be removed from the DataSet if they are unnecessary (are never read by ParameterMap) */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Performance")
	uint32 bTrimAttributes : 1;

	/** If true bTrimAttributes will be made to be true during cooks */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Performance", meta = (SkipSystemResetOnChange = "true"))
	uint32 bTrimAttributesOnCook : 1;

	/** If true, forcefully disables all debug switches */
	UPROPERTY(meta = (SkipSystemResetOnChange = "true"))
	uint32 bDisableAllDebugSwitches : 1;
	/** Subscriptions to definitions of parameters. */
	UPROPERTY()
	TArray<FParameterDefinitionsSubscription> ParameterDefinitionsSubscriptions;

#endif

	/** Computes emitter priorities based on the dependency information. */
	bool ComputeEmitterPriority(int32 EmitterIdx, TArray<int32, TInlineAllocator<32>>& EmitterPriorities, const TBitArray<TInlineAllocator<32>>& EmitterDependencyGraph);

	/** Queries all the data interfaces in the array for emitter dependencies. */
	void FindDataInterfaceDependencies(UNiagaraEmitter* Emitter, UNiagaraScript* Script, TArray<class UNiagaraEmitter*>& Dependencies);

	/** Looks at all the event handlers in the emitter to determine which other emitters it depends on. */
	void FindEventDependencies(UNiagaraEmitter* Emitter, TArray<UNiagaraEmitter*>& Dependencies);

	/** Computes the order in which the emitters in the Emitters array will be ticked and stores the results in EmitterExecutionOrder. */
	void ComputeEmittersExecutionOrder();

	/** Computes the order in which renderers will render */
	void ComputeRenderersDrawOrder();

	/** Cache data & accessors from the compiled data, allows us to avoid per instance. */
	void CacheFromCompiledData();

	FORCEINLINE TConstArrayView<FNiagaraEmitterExecutionIndex> GetEmitterExecutionOrder() const { return MakeArrayView(EmitterExecutionOrder); }
	FORCEINLINE TConstArrayView<FNiagaraRendererExecutionIndex> GetRendererPostTickOrder() const { return MakeArrayView(RendererPostTickOrder); }
	FORCEINLINE TConstArrayView<FNiagaraRendererExecutionIndex> GetRendererCompletionOrder() const { return MakeArrayView(RendererCompletionOrder); }

	FORCEINLINE TConstArrayView<int32> GetRendererDrawOrder() const { return MakeArrayView(RendererDrawOrder); }

	/** When an index inside the EmitterExecutionOrder array has this bit set, it means the corresponding emitter cannot execute in parallel with the previous emitters due to a data dependency. */
	static constexpr int32 kStartNewOverlapGroupBit = (1 << 31);

	FORCEINLINE UNiagaraParameterCollectionInstance* GetParameterCollectionOverride(UNiagaraParameterCollection* Collection)
	{
		UNiagaraParameterCollectionInstance** Found = ParameterCollectionOverrides.FindByPredicate(
			[&](const UNiagaraParameterCollectionInstance* CheckInst)
		{
			return CheckInst && Collection == CheckInst->Collection;
		});

		return Found ? *Found : nullptr;
	}
	
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDumpDebugSystemInfo;

	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDumpDebugEmitterInfo;

	bool bFullyLoaded = false;

	/** When enabled, we follow the settings on the UNiagaraComponent for tick order. When this option is disabled, we ignore any dependencies from data interfaces or other variables and instead fire off the simulation as early in the frame as possible. This greatly
	reduces overhead and allows the game thread to run faster, but comes at a tradeoff if the dependencies might leave gaps or other visual artifacts.*/
	UPROPERTY(EditAnywhere, Category = "Performance")
	bool bRequireCurrentFrameData = true;

	bool HasSystemScriptDIsWithPerInstanceData() const;
	FORCEINLINE bool HasDIsWithPostSimulateTick()const{ return bHasDIsWithPostSimulateTick; }
	FORCEINLINE bool HasAnyGPUEmitters()const{ return bHasAnyGPUEmitters; }
	FORCEINLINE bool NeedsGPUContextInitForDataInterfaces() const { return bNeedsGPUContextInitForDataInterfaces; }

	const TArray<FName>& GetUserDINamesReadInSystemScripts() const;

	FBox GetFixedBounds() const;
	FORCEINLINE void SetFixedBounds(const FBox& Box) { FixedBounds = Box;  }

#if WITH_EDITOR
	void SetEffectType(UNiagaraEffectType* EffectType);

	FORCEINLINE bool GetOverrideScalabilitySettings()const { return bOverrideScalabilitySettings; }
	FORCEINLINE void SetOverrideScalabilitySettings(bool bOverride) { bOverrideScalabilitySettings = bOverride; }
#endif
	UNiagaraEffectType* GetEffectType()const;
	FORCEINLINE const FNiagaraSystemScalabilitySettings& GetScalabilitySettings() { return CurrentScalabilitySettings; }
	FORCEINLINE bool NeedsSortedSignificanceCull()const{ return bNeedsSortedSignificanceCull; }
	
	void OnScalabilityCVarChanged();

	/** Whether or not fixed bounds are enabled. */
	UPROPERTY(EditAnywhere, Category = "System", meta = (SkipSystemResetOnChange = "true", InlineEditConditionToggle))
	uint32 bFixedBounds : 1;

	TStatId GetStatID(bool bGameThread, bool bConcurrent)const;
	void AddToInstanceCountStat(int32 NumInstances, bool bSolo)const;

	const FString& GetCrashReporterTag()const;
	bool CanObtainEmitterAttribute(const FNiagaraVariableBase& InVarWithUniqueNameNamespace) const;
	bool CanObtainSystemAttribute(const FNiagaraVariableBase& InVar) const;
	bool CanObtainUserVariable(const FNiagaraVariableBase& InVar) const;

#if WITH_EDITORONLY_DATA
	const TMap<FGuid, UNiagaraMessageDataBase*>& GetMessages() const { return MessageKeyToMessageMap; };
	void AddMessage(const FGuid& MessageKey, UNiagaraMessageDataBase* NewMessage) { MessageKeyToMessageMap.Add(MessageKey, NewMessage); };
	void RemoveMessage(const FGuid& MessageKey) { MessageKeyToMessageMap.Remove(MessageKey); };
	void RemoveMessageDelegateable(const FGuid MessageKey) { MessageKeyToMessageMap.Remove(MessageKey); };
	const FGuid& GetAssetGuid() const {return AssetGuid;};
#endif

	FORCEINLINE void RegisterActiveInstance();
	FORCEINLINE void UnregisterActiveInstance();
	FORCEINLINE int32& GetActiveInstancesCount() { return ActiveInstances; }

#if WITH_EDITORONLY_DATA
	UNiagaraBakerSettings* GetBakerSettings();
	const UNiagaraBakerSettings* GetBakerGeneratedSettings() const { return BakerGeneratedSettings; }
	void SetBakerGeneratedSettings(UNiagaraBakerSettings* Settings) { BakerGeneratedSettings = Settings; }
#endif

private:
#if WITH_EDITORONLY_DATA
	/** Checks the ddc for vm execution data for the given script. Return true if the data was loaded from the ddc, false otherwise. */
	bool GetFromDDC(FEmitterCompiledScriptPair& ScriptPair);

	/** Since the shader compilation is done in another process, this is used to check if the result for any ongoing compilations is done.
	*   If bWait is true then this *blocks* the game thread (and ui) until all running compilations are finished.
	*/
	bool QueryCompileComplete(bool bWait, bool bDoPost, bool bDoNotApply = false);

	bool ProcessCompilationResult(FEmitterCompiledScriptPair& ScriptPair, bool bWait, bool bDoNotApply);

	bool CompilationResultsValid(FNiagaraSystemCompileRequest& CompileRequest) const;

	void InitEmitterCompiledData();

	void InitSystemCompiledData();

	/** Helper for filling in precomputed variable names per emitter. Converts an emitter paramter "Emitter.XXXX" into it's real parameter name. */
	void InitEmitterVariableAliasNames(FNiagaraEmitterCompiledData& EmitterCompiledDataToInit, const UNiagaraEmitter* InAssociatedEmitter);

	/** Helper for generating aliased FNiagaraVariable names for the Emitter they are associated with. */
	const FName GetEmitterVariableAliasName(const FNiagaraVariable& InEmitterVar, const UNiagaraEmitter* InEmitter) const;

	/** Helper for filling in attribute datasets per emitter. */
	void InitEmitterDataSetCompiledData(FNiagaraDataSetCompiledData& DataSetToInit, const UNiagaraEmitter* InAssociatedEmitter, const FNiagaraEmitterHandle& InAssociatedEmitterHandle);
#endif

	void ResolveScalabilitySettings();
	void UpdatePostCompileDIInfo();
	void UpdateDITickFlags();
	void UpdateHasGPUEmitters();

protected:
	UPROPERTY(EditAnywhere, Category = "System")
	UNiagaraEffectType* EffectType;

	UPROPERTY(EditAnywhere, Category = "Scalability")
	bool bOverrideScalabilitySettings;

	UPROPERTY()
	TArray<FNiagaraSystemScalabilityOverride> ScalabilityOverrides_DEPRECATED;

	UPROPERTY(EditAnywhere, Category = "Scalability", meta = (EditCondition="bOverrideScalabilitySettings"))
	FNiagaraSystemScalabilityOverrides SystemScalabilityOverrides;

	/** Handles to the emitter this System will simulate. */
	UPROPERTY()
	TArray<FNiagaraEmitterHandle> EmitterHandles;

	UPROPERTY(EditAnywhere, Category="System")
	TArray<UNiagaraParameterCollectionInstance*> ParameterCollectionOverrides;

#if WITH_EDITORONLY_DATA
	UPROPERTY(Transient)
	TArray<FNiagaraSystemCompileRequest> ActiveCompilations;
#endif

// 	/** Category of this system. */
// 	UPROPERTY(EditAnywhere, Category = System)
// 	UNiagaraSystemCategory* Category;

	/** The script which defines the System parameters, and which generates the bindings from System
		parameter to emitter parameter. */
	UPROPERTY()
	UNiagaraScript* SystemSpawnScript;

	/** The script which defines the System parameters, and which generates the bindings from System
	parameter to emitter parameter. */
	UPROPERTY()
	UNiagaraScript* SystemUpdateScript;

	//** Post compile generated data used for initializing Emitter Instances during runtime. */
	TArray<TSharedRef<const FNiagaraEmitterCompiledData>> EmitterCompiledData;

	//** Post compile generated data used for initializing System Instances during runtime. */
	UPROPERTY()
	FNiagaraSystemCompiledData SystemCompiledData;

	/** Variables exposed to the outside work for tweaking*/
	UPROPERTY()
	FNiagaraUserRedirectionParameterStore ExposedParameters;

#if WITH_EDITORONLY_DATA
	/** Data used by the editor to maintain UI state etc.. */
	UPROPERTY()
	UNiagaraEditorDataBase* EditorData;

	/** Wrapper for editor only parameters. */
	UPROPERTY()
	UNiagaraEditorParametersAdapterBase* EditorParameters;

	bool bIsolateEnabled;

	/** A multicast delegate which is called whenever the script has been compiled (successfully or not). */
	FOnSystemCompiled OnSystemCompiledDelegate;

	/** A multicast delegate which is called whenever this system's properties are changed. */
	FOnSystemPostEditChange OnSystemPostEditChangeDelegate;
#endif

	/** The fixed bounding box value. bFixedBounds is the condition whether the fixed bounds can be edited. */
	UPROPERTY(EditAnywhere, Category = "System", meta = (SkipSystemResetOnChange = "true", EditCondition = "bFixedBounds"))
	FBox FixedBounds;

	UPROPERTY(EditAnywhere, Category = Performance, meta = (ToolTip = "Auto-deactivate system if all emitters are determined to not spawn particles again, regardless of lifetime."))
	bool bAutoDeactivate;

	/** Warm up time in seconds. Used to calculate WarmupTickCount. Rounds down to the nearest multiple of WarmupTickDelta. */
	UPROPERTY(EditAnywhere, Category = Warmup)
	float WarmupTime;

	/** Number of ticks to process for warmup. You can set by this or by time via WarmupTime. */
	UPROPERTY(EditAnywhere, Category = Warmup)
	int32 WarmupTickCount;

	/** Delta time to use for warmup ticks. */
	UPROPERTY(EditAnywhere, Category = Warmup)
	float WarmupTickDelta;

#if WITH_EDITORONLY_DATA
	/** Settings used inside the baker */
	UPROPERTY(Export)
	UNiagaraBakerSettings* BakerSettings;

	/** Generated data baker settings, will be null until we have generated at least once. */
	UPROPERTY(Export)
	UNiagaraBakerSettings* BakerGeneratedSettings;
#endif

	UPROPERTY()
	bool bHasSystemScriptDIsWithPerInstanceData;

	UPROPERTY()
	bool bNeedsGPUContextInitForDataInterfaces;


	UPROPERTY()
	TArray<FName> UserDINamesReadInSystemScripts;

	/** Array of emitter indices sorted by execution priority. The emitters will be ticked in this order. Please note that some indices may have the top bit set (kStartNewOverlapGroupBit)
	* to indicate synchronization points in parallel execution, so mask it out before using the values as indices in the emitters array.
	*/
	TArray<FNiagaraEmitterExecutionIndex> EmitterExecutionOrder;

	/** Array of renderer indices to notify system PostTick, in order of execution */
	TArray<FNiagaraRendererExecutionIndex> RendererPostTickOrder;
	/** Array of renderer indices to notify system Completion, in order of execution */
	TArray<FNiagaraRendererExecutionIndex> RendererCompletionOrder;

	/** Precomputed emitter renderer draw order, since emitters & renderers are not dynamic we can do this. */
	TArray<int32> RendererDrawOrder;

	uint32 bIsValidCached : 1;
	uint32 bIsReadyToRunCached : 1;

	TOptional<float> MaxDeltaTime;
	FNiagaraDataSetAccessor<ENiagaraExecutionState> SystemExecutionStateAccessor;
	TArray<FNiagaraDataSetAccessor<ENiagaraExecutionState>> EmitterExecutionStateAccessors;
	TArray<TArray<FNiagaraDataSetAccessor<FNiagaraSpawnInfo>>> EmitterSpawnInfoAccessors;

	void GenerateStatID()const;
#if STATS
	mutable TStatId StatID_GT;
	mutable TStatId StatID_GT_CNC;
	mutable TStatId StatID_RT;
	mutable TStatId StatID_RT_CNC;

	mutable TStatId StatID_InstanceCount;
	mutable TStatId StatID_InstanceCountSolo;
	FNiagaraStatDatabase StatDatabase;
#endif

	FNiagaraSystemScalabilitySettings CurrentScalabilitySettings;

	mutable FString CrashReporterTag;

	uint32 bHasDIsWithPostSimulateTick : 1;
	uint32 bHasAnyGPUEmitters : 1;
	uint32 bNeedsSortedSignificanceCull : 1;

#if WITH_EDITORONLY_DATA
	/** Messages associated with the System asset. */
	UPROPERTY()
	TMap<FGuid, UNiagaraMessageDataBase*> MessageKeyToMessageMap;

	FGuid AssetGuid;
#endif

	/** Total active instances of this system. */
	int32 ActiveInstances;
};

FORCEINLINE void UNiagaraSystem::RegisterActiveInstance()
{
	++ActiveInstances;
}

FORCEINLINE void UNiagaraSystem::UnregisterActiveInstance()
{
	--ActiveInstances;
}

template<typename TAction>
void UNiagaraSystem::ForEachScript(TAction Func) const
{	
	Func(SystemSpawnScript);
	Func(SystemUpdateScript);
			
	for (const FNiagaraEmitterHandle& Handle : EmitterHandles)
	{
		if (UNiagaraEmitter* Emitter = Handle.GetInstance())
		{
			Emitter->ForEachScript(Func);
		}
	}
}
