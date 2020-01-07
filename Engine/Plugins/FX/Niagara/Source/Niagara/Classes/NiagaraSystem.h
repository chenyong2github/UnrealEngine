// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticlePerfStats.h"
#include "NiagaraCommon.h"
#include "NiagaraDataSet.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraParameterCollection.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "NiagaraSystemFastPath.h"
#include "NiagaraEffectType.h"

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
struct FNiagaraSystemCompiledData
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray<FNiagaraVariable> NumParticleVars;

	UPROPERTY()
	TArray<FNiagaraVariable> TotalSpawnedParticlesVars;

	UPROPERTY()
	FNiagaraParameterStore InstanceParamStore;

	UPROPERTY()
	TArray<FNiagaraVariable> SpawnCountScaleVars;

	UPROPERTY()
	FNiagaraDataSetCompiledData DataSetCompiledData;

	UPROPERTY()
	FNiagaraDataSetCompiledData SpawnInstanceParamsDataSetCompiledData;

	UPROPERTY()
	FNiagaraDataSetCompiledData UpdateInstanceParamsDataSetCompiledData;
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
};

USTRUCT()
struct FNiagaraSystemCompileRequest
{
	GENERATED_USTRUCT_BODY()
		
	double StartTime;

	UPROPERTY()
	TArray<UObject*> RootObjects;

	TArray<FEmitterCompiledScriptPair> EmitterCompiledScriptPairs;
	
	TMap<UNiagaraScript*, TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> > MappedData;
};

/** Container for multiple emitters that combine together to create a particle system effect.*/
UCLASS(BlueprintType)
class NIAGARA_API UNiagaraSystem : public UFXSystemAsset
{
	GENERATED_UCLASS_BODY()

public:
#if WITH_EDITOR
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSystemCompiled, UNiagaraSystem*);
#endif
	//TestChange

	UNiagaraSystem(FVTableHelper& Helper);

	//~ UObject interface
	void PostInitProperties();
	void Serialize(FArchive& Ar)override;
	virtual void PostLoad() override; 
	virtual void BeginDestroy() override;
	virtual void PreSave(const class ITargetPlatform * TargetPlatform) override;
#if WITH_EDITOR
	virtual void PreEditChange(FProperty* PropertyThatWillChange)override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override; 
	virtual void BeginCacheForCookedPlatformData(const ITargetPlatform *TargetPlatform) override;
#endif

	/** Gets an array of the emitter handles. */
	const TArray<FNiagaraEmitterHandle>& GetEmitterHandles();
	const TArray<FNiagaraEmitterHandle>& GetEmitterHandles()const;

	/** Returns true if this system is valid and can be instanced. False otherwise. */
	bool IsValid()const;

#if WITH_EDITORONLY_DATA
	/** Adds a new emitter handle to this System.  The new handle exposes an Instance value which is a copy of the
		original asset. */
	FNiagaraEmitterHandle AddEmitterHandle(UNiagaraEmitter& SourceEmitter, FName EmitterName);

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

private:
	bool IsReadyToRunInternal() const;
	bool bIsReadyToRunCached = false;
public:
	bool IsReadyToRun() const;

	FORCEINLINE bool NeedsWarmup()const { return WarmupTickCount > 0 && WarmupTickDelta > SMALL_NUMBER; }
	FORCEINLINE float GetWarmupTime()const { return WarmupTime; }
	FORCEINLINE int32 GetWarmupTickCount()const { return WarmupTickCount; }
	FORCEINLINE float GetWarmupTickDelta()const { return WarmupTickDelta; }

#if WITH_EDITORONLY_DATA
	/** Are there any pending compile requests?*/
	bool HasOutstandingCompilationRequests() const;

	/** Determines if this system has the supplied emitter as an editable and simulating emitter instance. */
	bool ReferencesInstanceEmitter(UNiagaraEmitter& Emitter);

	/** Updates the system's rapid iteration parameters from a specific emitter. */
	void RefreshSystemParametersFromEmitter(const FNiagaraEmitterHandle& EmitterHandle);

	/** Removes the system's rapid iteration parameters for a specific emitter. */
	void RemoveSystemParametersForEmitter(const FNiagaraEmitterHandle& EmitterHandle);

	/** Request that any dirty scripts referenced by this system be compiled.*/
	bool RequestCompile(bool bForce);

	/** If we have a pending compile request, is it done with yet? */
	bool PollForCompilationComplete();

	/** Blocks until all active compile jobs have finished */
	void WaitForCompilationComplete();

	/** Delegate called when the system's dependencies have all been compiled.*/
	FOnSystemCompiled& OnSystemCompiled();

	/** Gets editor specific data stored with this system. */
	UNiagaraEditorDataBase* GetEditorData();

	/** Gets editor specific data stored with this system. */
	const UNiagaraEditorDataBase* GetEditorData() const;

	/** Internal: The thumbnail image.*/
	UPROPERTY()
	class UTexture2D* ThumbnailImage;

	/** Internal: Indicates the thumbnail image is out of date.*/
	UPROPERTY()
	uint32 ThumbnailImageOutOfDate : 1;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable)
	bool bIsTemplateAsset;

	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Asset Options", AssetRegistrySearchable)
	FText TemplateAssetDescription;

	bool GetIsolateEnabled() const;
	void SetIsolateEnabled(bool bIsolate);
	
	UPROPERTY(transient)
	FNiagaraSystemUpdateContext UpdateContext;
#endif

	bool ShouldAutoDeactivate() const { return bAutoDeactivate; }
	bool IsLooping() const;

	const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& GetEmitterCompiledData() const { return EmitterCompiledData; };

	const FNiagaraSystemCompiledData& GetSystemCompiledData() const { return SystemCompiledData; };

	bool UsesCollection(const UNiagaraParameterCollection* Collection)const;
#if WITH_EDITORONLY_DATA
	bool UsesEmitter(const UNiagaraEmitter* Emitter) const;
	bool UsesScript(const UNiagaraScript* Script)const; 
	void InvalidateCachedCompileIds();

	static void RequestCompileForEmitter(UNiagaraEmitter* InEmitter);

	/** Experimental feature that allows us to bake out rapid iteration parameters during the normal compile process. */
	UPROPERTY(EditAnywhere, AdvancedDisplay, Category = "Emitter")
	uint32 bBakeOutRapidIteration : 1;
#endif

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

	bool HasSystemScriptDIsWithPerInstanceData() const;

	const TArray<FName>& GetUserDINamesReadInSystemScripts() const;

	FBox GetFixedBounds() const;

	FORCEINLINE int32* GetCycleCounter(bool bGameThread, bool bConcurrent);

	UNiagaraEffectType* GetEffectType()const;
	const FNiagaraScalabilitySettings& GetScalabilitySettings(int32 DetailLevel=INDEX_NONE);
	
	void ResolveScalabilityOverrides();
	void OnDetailLevelChanges(int32 DetailLevel);

	/** Whether or not fixed bounds are enabled. */
	UPROPERTY(EditAnywhere, Category = "System", meta = (InlineEditConditionToggle))
	uint32 bFixedBounds : 1;

	TStatId GetStatID(bool bGameThread, bool bConcurrent)const;

	UPROPERTY(EditAnywhere, Category = "Script Fast Path")
	ENiagaraFastPathMode FastPathMode;

	UPROPERTY(EditAnywhere, Category = "Script Fast Path")
	FNiagaraFastPath_Module_SystemScalability SystemScalability;

	UPROPERTY(EditAnywhere, Category = "Script Fast Path")
	FNiagaraFastPath_Module_SystemLifeCycle SystemLifeCycle;

private:
#if WITH_EDITORONLY_DATA
	/** Checks the ddc for vm execution data for the given script. Return true if the data was loaded from the ddc, false otherwise. */
	bool GetFromDDC(FEmitterCompiledScriptPair& ScriptPair);

	/** Since the shader compilation is done in another process, this is used to check if the result for any ongoing compilations is done.
	*   If bWait is true then this *blocks* the game thread (and ui) until all running compilations are finished.
	*/
	bool QueryCompileComplete(bool bWait, bool bDoPost, bool bDoNotApply = false);

	bool ProcessCompilationResult(FEmitterCompiledScriptPair& ScriptPair, bool bWait, bool bDoNotApply);

	void InitEmitterCompiledData();

	void InitSystemCompiledData();

	/** Helper for filling in precomputed variable names per emitter. Converts an emitter paramter "Emitter.XXXX" into it's real parameter name. */
	void InitEmitterVariableAliasNames(FNiagaraEmitterCompiledData& EmitterCompiledDataToInit, const UNiagaraEmitter* InAssociatedEmitter);

	/** Helper for generating aliased FNiagaraVariable names for the Emitter they are associated with. */
	const FName GetEmitterVariableAliasName(const FNiagaraVariable& InEmitterVar, const UNiagaraEmitter* InEmitter) const;

	/** Helper for filling in attribute datasets per emitter. */
	void InitEmitterDataSetCompiledData(FNiagaraDataSetCompiledData& DataSetToInit, const UNiagaraEmitter* InAssociatedEmitter, const FNiagaraEmitterHandle& InAssociatedEmitterHandle);
#endif

	void UpdatePostCompileDIInfo();

protected:
	UPROPERTY(EditAnywhere, Category = "System")
	UNiagaraEffectType* EffectType;

	UPROPERTY(EditAnywhere, Category = "System", meta=(InlineEditConditionToggle))
	bool bOverrideScalabilitySettings;

	UPROPERTY(EditAnywhere, Category = "System", meta = (EditCondition="bOverrideScalabilitySettings"))
	TArray<FNiagaraScalabilityOverrides> ScalabilityOverrides;

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

	bool bIsolateEnabled;

	/** A multicast delegate which is called whenever the script has been compiled (successfully or not). */
	FOnSystemCompiled OnSystemCompiledDelegate;
#endif

	/** The fixed bounding box value. bFixedBounds is the condition whether the fixed bounds can be edited. */
	UPROPERTY(EditAnywhere, Category = "System", meta = (EditCondition = "bFixedBounds"))
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

	UPROPERTY()
	bool bHasSystemScriptDIsWithPerInstanceData;

	UPROPERTY()
	TArray<FName> UserDINamesReadInSystemScripts;

	void GenerateStatID()const;
#if STATS
	mutable TStatId StatID_GT;
	mutable TStatId StatID_GT_CNC;
	mutable TStatId StatID_RT;
	mutable TStatId StatID_RT_CNC;
#endif

	/** Resolved results of this system's overrides applied on top of it's effect type settings. */
	UPROPERTY()
	TArray<FNiagaraScalabilitySettings> ResolvedScalabilitySettings;

	FNiagaraScalabilitySettings CurrentScalabilitySettings;
};

extern int32 GEnableNiagaraRuntimeCycleCounts;
FORCEINLINE int32* UNiagaraSystem::GetCycleCounter(bool bGameThread, bool bConcurrent)
{
	if (GEnableNiagaraRuntimeCycleCounts && EffectType)
	{
		return EffectType->GetCycleCounter(bGameThread, bConcurrent);
	}
	return nullptr;
}