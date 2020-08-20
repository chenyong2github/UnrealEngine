// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "NiagaraModule.h"
#include "INiagaraMergeManager.h"
#include "NiagaraTypes.h"
#include "NiagaraCommon.h"

#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/ObjectKey.h"

class UNiagaraEmitter;
class UNiagaraEmitterEditorData;
class UNiagaraScript;
class UNiagaraNodeOutput;
class UNiagaraNodeInput;
class UNiagaraNodeFunctionCall;
class UNiagaraNodeParameterMapSet;
class UEdGraphNode;
class UEdGraphPin;
class UNiagaraDataInterface;
struct FNiagaraEventScriptProperties;
class UNiagaraSimulationStageBase;
class UNiagaraRendererProperties;

class FNiagaraStackFunctionMergeAdapter;

class FNiagaraStackFunctionInputOverrideMergeAdapter
{
public:
	FNiagaraStackFunctionInputOverrideMergeAdapter(
		const UNiagaraEmitter& InOwningEmitter, 
		UNiagaraScript& InOwningScript, 
		UNiagaraNodeFunctionCall& InOwningFunctionCallNode, 
		UEdGraphPin& InOverridePin);

	FNiagaraStackFunctionInputOverrideMergeAdapter(
		UNiagaraScript& InOwningScript,
		UNiagaraNodeFunctionCall& InOwningFunctionCallNode,
		FString InInputName,
		FNiagaraVariable InRapidIterationParameter);

	FNiagaraStackFunctionInputOverrideMergeAdapter(UEdGraphPin* InStaticSwitchPin);

	UNiagaraScript* GetOwningScript() const;
	UNiagaraNodeFunctionCall* GetOwningFunctionCall() const;
	FString GetInputName() const;
	UNiagaraNodeParameterMapSet* GetOverrideNode() const;
	UEdGraphPin* GetOverridePin() const;
	const FGuid& GetOverrideNodeId() const;
	const FNiagaraTypeDefinition& GetType() const;

	TOptional<FString> GetLocalValueString() const;
	TOptional<FNiagaraVariable> GetLocalValueRapidIterationParameter() const;
	TOptional<FNiagaraParameterHandle> GetLinkedValueHandle() const;
	TOptional<FName> GetDataValueInputName() const;
	UNiagaraDataInterface* GetDataValueObject() const;
	TSharedPtr<FNiagaraStackFunctionMergeAdapter> GetDynamicValueFunction() const;
	TOptional<FString> GetStaticSwitchValue() const;

private:
	TWeakObjectPtr<UNiagaraScript> OwningScript;
	TWeakObjectPtr<UNiagaraNodeFunctionCall> OwningFunctionCallNode;
	FString InputName;
	FNiagaraTypeDefinition Type;

	TWeakObjectPtr<UNiagaraNodeParameterMapSet> OverrideNode;
	UEdGraphPin* OverridePin;

	TOptional<FString> LocalValueString;
	TOptional<FNiagaraVariable> LocalValueRapidIterationParameter;
	TOptional<FNiagaraParameterHandle> LinkedValueHandle;
	TOptional<FName> DataValueInputName;
	UNiagaraDataInterface* DataValueObject;
	TSharedPtr<FNiagaraStackFunctionMergeAdapter> DynamicValueFunction;
	TOptional<FString> StaticSwitchValue;

	FGuid OverrideValueNodePersistentId;
};

class FNiagaraStackFunctionMergeAdapter
{
public:
	FNiagaraStackFunctionMergeAdapter(const UNiagaraEmitter& InOwningEmitter, UNiagaraScript& InOwningScript, UNiagaraNodeFunctionCall& InFunctionCallNode,	int32 InStackIndex);

	UNiagaraNodeFunctionCall* GetFunctionCallNode() const;

	int32 GetStackIndex() const;

	int32 GetScratchPadScriptIndex() const;

	const TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>>& GetInputOverrides() const;

	TSharedPtr<FNiagaraStackFunctionInputOverrideMergeAdapter> GetInputOverrideByInputName(FString InputName) const;

private:
	TWeakObjectPtr<UNiagaraScript> OwningScript;
	TWeakObjectPtr<UNiagaraNodeFunctionCall> FunctionCallNode;
	int32 StackIndex;
	int32 ScratchPadScriptIndex;

	TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> InputOverrides;
};

class FNiagaraScriptStackMergeAdapter
{
public:
	FNiagaraScriptStackMergeAdapter(const UNiagaraEmitter& InOwningEmitter, UNiagaraNodeOutput& InOutputNode, UNiagaraScript& InScript);

	UNiagaraNodeOutput* GetOutputNode() const;
	UNiagaraNodeInput* GetInputNode() const;

	UNiagaraScript* GetScript() const;

	FString GetUniqueEmitterName() const;

	const TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>>& GetModuleFunctions() const;

	TSharedPtr<FNiagaraStackFunctionMergeAdapter> GetModuleFunctionById(FGuid FunctionCallNodeId) const;

private:
	TWeakObjectPtr<UNiagaraNodeInput> InputNode;
	TWeakObjectPtr<UNiagaraNodeOutput> OutputNode;
	TWeakObjectPtr<UNiagaraScript> Script;
	FString UniqueEmitterName;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> ModuleFunctions;
};

class FNiagaraEventHandlerMergeAdapter
{
public:
	FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, const FNiagaraEventScriptProperties* InEventScriptProperties, UNiagaraNodeOutput* InOutputNode);
	FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, FNiagaraEventScriptProperties* InEventScriptProperties, UNiagaraNodeOutput* InOutputNode);
	FNiagaraEventHandlerMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraNodeOutput* InOutputNode);

	FGuid GetUsageId() const;
	const UNiagaraEmitter* GetEmitter() const;
	const FNiagaraEventScriptProperties* GetEventScriptProperties() const;
	FNiagaraEventScriptProperties* GetEditableEventScriptProperties() const;
	UNiagaraNodeOutput* GetOutputNode() const;
	UNiagaraNodeInput* GetInputNode() const;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> GetEventStack() const;

private:
	void Initialize(const UNiagaraEmitter& InEmitter, const FNiagaraEventScriptProperties* InEventScriptProperties, FNiagaraEventScriptProperties* InEditableEventScriptProperties, UNiagaraNodeOutput* InOutputNode);

private:
	TWeakObjectPtr<UNiagaraEmitter> Emitter;
	const FNiagaraEventScriptProperties* EventScriptProperties;
	FNiagaraEventScriptProperties* EditableEventScriptProperties;
	TWeakObjectPtr<UNiagaraNodeOutput> OutputNode;
	TWeakObjectPtr<UNiagaraNodeInput> InputNode;

	TSharedPtr<FNiagaraScriptStackMergeAdapter> EventStack;
};

class FNiagaraSimulationStageMergeAdapter
{
public:
	FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, const UNiagaraSimulationStageBase* InSimulationStage, UNiagaraNodeOutput* InOutputNode);
	FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraSimulationStageBase* InSimulationStage, UNiagaraNodeOutput* InOutputNode);
	FNiagaraSimulationStageMergeAdapter(const UNiagaraEmitter& InEmitter, UNiagaraNodeOutput* InOutputNode);

	FGuid GetUsageId() const;
	const UNiagaraEmitter* GetEmitter() const;
	const UNiagaraSimulationStageBase* GetSimulationStage() const;
	UNiagaraSimulationStageBase* GetEditableSimulationStage() const;
	UNiagaraNodeOutput* GetOutputNode() const;
	UNiagaraNodeInput* GetInputNode() const;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> GetSimulationStageStack() const;

private:
	void Initialize(const UNiagaraEmitter& InEmitter, const UNiagaraSimulationStageBase* InSimulationStage, UNiagaraSimulationStageBase* InEditableSimulationStage, UNiagaraNodeOutput* InOutputNode);

private:
	TWeakObjectPtr<UNiagaraEmitter> Emitter;
	const UNiagaraSimulationStageBase* SimulationStage;
	UNiagaraSimulationStageBase* EditableSimulationStage;
	TWeakObjectPtr<UNiagaraNodeOutput> OutputNode;
	TWeakObjectPtr<UNiagaraNodeInput> InputNode;

	TSharedPtr<FNiagaraScriptStackMergeAdapter> SimulationStageStack;
};

class FNiagaraRendererMergeAdapter
{
public:
	FNiagaraRendererMergeAdapter(UNiagaraRendererProperties& InRenderer);

	UNiagaraRendererProperties* GetRenderer();

private:
	TWeakObjectPtr<UNiagaraRendererProperties> Renderer;
};

class FNiagaraEmitterMergeAdapter
{
public:
	FNiagaraEmitterMergeAdapter(const UNiagaraEmitter& InEmitter);
	FNiagaraEmitterMergeAdapter(UNiagaraEmitter& InEmitter);

	UNiagaraEmitter* GetEditableEmitter() const;

	TSharedPtr<FNiagaraScriptStackMergeAdapter> GetEmitterSpawnStack() const;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> GetEmitterUpdateStack() const;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> GetParticleSpawnStack() const;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> GetParticleUpdateStack() const;
	const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>> GetEventHandlers() const;
	const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>> GetSimulationStages() const;
	const TArray<TSharedRef<FNiagaraRendererMergeAdapter>> GetRenderers() const;
	const UNiagaraEmitterEditorData* GetEditorData() const;

	TSharedPtr<FNiagaraScriptStackMergeAdapter> GetScriptStack(ENiagaraScriptUsage Usage, FGuid UsageId);

	TSharedPtr<FNiagaraEventHandlerMergeAdapter> GetEventHandler(FGuid EventScriptUsageId);

	TSharedPtr<FNiagaraSimulationStageMergeAdapter> GetSimulationStage(FGuid SimulationStageUsageId);

	TSharedPtr<FNiagaraRendererMergeAdapter> GetRenderer(FGuid RendererMergeId);

private:
	void Initialize(const UNiagaraEmitter& InEmitter, UNiagaraEmitter* InEditableEmitter);

private:
	TWeakObjectPtr<const UNiagaraEmitter> Emitter;
	TWeakObjectPtr<UNiagaraEmitter> EditableEmitter;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> EmitterSpawnStack;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> EmitterUpdateStack;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> ParticleSpawnStack;
	TSharedPtr<FNiagaraScriptStackMergeAdapter> ParticleUpdateStack;
	TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>> EventHandlers;
	TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>> SimulationStages;
	TArray<TSharedRef<FNiagaraRendererMergeAdapter>> Renderers;
	TWeakObjectPtr<const UNiagaraEmitterEditorData> EditorData;
};

struct FNiagaraScriptStackDiffResults
{
	FNiagaraScriptStackDiffResults();

	bool IsEmpty() const;

	bool IsValid() const;

	void AddError(FText ErrorMessage);

	const TArray<FText>& GetErrorMessages() const;

	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> RemovedBaseModules;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> AddedOtherModules;

	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> MovedBaseModules;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> MovedOtherModules;

	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> EnabledChangedBaseModules;
	TArray<TSharedRef<FNiagaraStackFunctionMergeAdapter>> EnabledChangedOtherModules;

	TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> RemovedBaseInputOverrides;
	TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> AddedOtherInputOverrides;
	TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> ModifiedBaseInputOverrides;
	TArray<TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter>> ModifiedOtherInputOverrides;

	TOptional<ENiagaraScriptUsage> ChangedBaseUsage;
	TOptional<ENiagaraScriptUsage> ChangedOtherUsage;

private:
	bool bIsValid;
	TArray<FText> ErrorMessages;
};

struct FNiagaraModifiedEventHandlerDiffResults
{
	TSharedPtr<FNiagaraEventHandlerMergeAdapter> BaseAdapter;
	TSharedPtr<FNiagaraEventHandlerMergeAdapter> OtherAdapter;
	TArray<FProperty*> ChangedProperties;
	FNiagaraScriptStackDiffResults ScriptDiffResults;
};

struct FNiagaraModifiedSimulationStageDiffResults
{
	TSharedPtr<FNiagaraSimulationStageMergeAdapter> BaseAdapter;
	TSharedPtr<FNiagaraSimulationStageMergeAdapter> OtherAdapter;
	TArray<FProperty*> ChangedProperties;
	FNiagaraScriptStackDiffResults ScriptDiffResults;
};

struct FNiagaraEmitterDiffResults
{
	FNiagaraEmitterDiffResults();

	bool IsValid() const;

	bool IsEmpty() const;

	void AddError(FText ErrorMessage);

	const TArray<FText>& GetErrorMessages() const;

	FString GetErrorMessagesString() const;

	TArray<FProperty*> DifferentEmitterProperties;

	FNiagaraScriptStackDiffResults EmitterSpawnDiffResults;
	FNiagaraScriptStackDiffResults EmitterUpdateDiffResults;
	FNiagaraScriptStackDiffResults ParticleSpawnDiffResults;
	FNiagaraScriptStackDiffResults ParticleUpdateDiffResults;

	TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>> RemovedBaseEventHandlers;
	TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>> AddedOtherEventHandlers;
	TArray<FNiagaraModifiedEventHandlerDiffResults> ModifiedEventHandlers;

	TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>> RemovedBaseSimulationStages;
	TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>> AddedOtherSimulationStages;
	TArray<FNiagaraModifiedSimulationStageDiffResults> ModifiedSimulationStages;

	TArray<TSharedRef<FNiagaraRendererMergeAdapter>> RemovedBaseRenderers;
	TArray<TSharedRef<FNiagaraRendererMergeAdapter>> AddedOtherRenderers;
	TArray<TSharedRef<FNiagaraRendererMergeAdapter>> ModifiedBaseRenderers;
	TArray<TSharedRef<FNiagaraRendererMergeAdapter>> ModifiedOtherRenderers;

	TMap<FString, FText> ModifiedStackEntryDisplayNames;

private:
	bool bIsValid;
	TArray<FText> ErrorMessages;
};

class FNiagaraScriptMergeManager : public INiagaraMergeManager
{
public:
	struct FApplyDiffResults
	{
		FApplyDiffResults()
			: bSucceeded(true)
			, bModifiedGraph(false)
		{
		}

		bool bSucceeded;
		bool bModifiedGraph;
		TArray<FText> ErrorMessages;
	};

public:
	virtual INiagaraMergeManager::FMergeEmitterResults MergeEmitter(UNiagaraEmitter& Parent, UNiagaraEmitter* ParentAtLastMerge, UNiagaraEmitter& Instance) const override;

	static TSharedRef<FNiagaraScriptMergeManager> Get();

	FNiagaraEmitterDiffResults DiffEmitters(UNiagaraEmitter& BaseEmitter, UNiagaraEmitter& OtherEmitter) const;

	bool IsMergeableScriptUsage(ENiagaraScriptUsage ScriptUsage) const;

	bool HasBaseModule(const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId);

	bool IsModuleInputDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId, FString InputName);

	FApplyDiffResults ResetModuleInputToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FGuid ModuleId, FString InputName);

	bool HasBaseEventHandler(const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId);

	bool IsEventHandlerPropertySetDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId);

	void ResetEventHandlerPropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid EventScriptUsageId);

	bool HasBaseSimulationStage(const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId);

	bool IsSimulationStagePropertySetDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId);

	void ResetSimulationStagePropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid SimulationStageScriptUsageId);

	bool HasBaseRenderer(const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId);

	bool IsRendererDifferentFromBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId);

	void ResetRendererToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter, FGuid RendererMergeId);

	bool IsEmitterEditablePropertySetDifferentFromBase(const UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter);

	void ResetEmitterEditablePropertySetToBase(UNiagaraEmitter& Emitter, const UNiagaraEmitter& BaseEmitter);

	void DiffEventHandlers(const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>>& BaseEventHandlers, const TArray<TSharedRef<FNiagaraEventHandlerMergeAdapter>>& OtherEventHandlers, FNiagaraEmitterDiffResults& DiffResults) const;

	void DiffSimulationStages(const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& BaseSimulationStages, const TArray<TSharedRef<FNiagaraSimulationStageMergeAdapter>>& OtherSimulationStages, FNiagaraEmitterDiffResults& DiffResults) const;

	void DiffRenderers(const TArray<TSharedRef<FNiagaraRendererMergeAdapter>>& BaseRenderers, const TArray<TSharedRef<FNiagaraRendererMergeAdapter>>& OtherRenderers, FNiagaraEmitterDiffResults& DiffResults) const;

	void DiffScriptStacks(TSharedRef<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter, TSharedRef<FNiagaraScriptStackMergeAdapter> OtherScriptStackAdapter, FNiagaraScriptStackDiffResults& DiffResults) const;

	void DiffFunctionInputs(TSharedRef<FNiagaraStackFunctionMergeAdapter> BaseFunctionAdapter, TSharedRef<FNiagaraStackFunctionMergeAdapter> OtherFunctionAdapter, FNiagaraScriptStackDiffResults& DiffResults) const;

	virtual void DiffEditableProperties(const void* BaseDataAddress, const void* OtherDataAddress, UStruct& Struct, TArray<FProperty*>& OutDifferentProperties) const override;

	void DiffStackEntryDisplayNames(const UNiagaraEmitterEditorData* BaseEditorData, const UNiagaraEmitterEditorData* OtherEditorData, TMap<FString, FText>& OutModifiedStackEntryDisplayNames) const;

	virtual void CopyPropertiesToBase(void* BaseDataAddress, const void* OtherDataAddress, TArray<FProperty*> PropertiesToCopy) const override;


private:
	TOptional<bool> DoFunctionInputOverridesMatch(TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> BaseFunctionInputAdapter, TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OtherFunctionInputAdapter) const;

	void CopyInstanceScratchPadScripts(UNiagaraEmitter& MergedInstance, const UNiagaraEmitter& SourceInstance, TMap<UNiagaraScript*, UNiagaraScript*>& OutSourceToMergedScratchPadScriptMap) const;

	FApplyDiffResults ApplyScriptStackDiff(
		TSharedRef<FNiagaraScriptStackMergeAdapter> BaseScriptStackAdapter,
		const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap, 
		const FNiagaraScriptStackDiffResults& DiffResults,
		const bool bNoParentAtLastMerge) const;

	FApplyDiffResults ApplyEventHandlerDiff(
		TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
		const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
		const FNiagaraEmitterDiffResults& DiffResults,
		const bool bNoParentAtLastMerge) const;

	FApplyDiffResults ApplySimulationStageDiff(
		TSharedRef<FNiagaraEmitterMergeAdapter> BaseEmitterAdapter,
		const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
		const FNiagaraEmitterDiffResults& DiffResults,
		const bool bNoParentAtLastMerge) const;

	FApplyDiffResults ApplyRendererDiff(UNiagaraEmitter& BaseEmitter, const FNiagaraEmitterDiffResults& DiffResults, const bool bNoParentAtLastMerge) const;

	FApplyDiffResults ApplyStackEntryDisplayNameDiffs(UNiagaraEmitter& Emitter, const FNiagaraEmitterDiffResults& DiffResults) const;

	FApplyDiffResults AddModule(
		FString UniqueEmitterName,
		UNiagaraScript& OwningScript,
		UNiagaraNodeOutput& TargetOutputNode,
		const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
		TSharedRef<FNiagaraStackFunctionMergeAdapter> AddModule) const;

	FApplyDiffResults RemoveInputOverride(UNiagaraScript& OwningScript, TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToRemove) const;

	FApplyDiffResults AddInputOverride(
		FString UniqueEmitterName,
		UNiagaraScript& OwningScript,
		UNiagaraNodeFunctionCall& TargetFunctionCall,
		const TMap<UNiagaraScript*, UNiagaraScript*>& SourceToMergedScratchPadScriptMap,
		TSharedRef<FNiagaraStackFunctionInputOverrideMergeAdapter> OverrideToAdd) const;

private:
	TSharedRef<FNiagaraEmitterMergeAdapter> GetEmitterMergeAdapterUsingCache(const UNiagaraEmitter& Emitter);

	TSharedRef<FNiagaraEmitterMergeAdapter> GetEmitterMergeAdapterUsingCache(UNiagaraEmitter& Emitter);

	void DiffChangeIds(const TMap<FGuid, FGuid>& InSourceChangeIds, const TMap<FGuid, FGuid>& InLastMergedChangeIds, const TMap<FGuid, FGuid>& InInstanceChangeIds, TMap<FGuid, FGuid>& OutChangeIdsToKeepOnInstance) const;
	FApplyDiffResults ResolveChangeIds(TSharedRef<FNiagaraEmitterMergeAdapter> MergedInstanceAdapter, UNiagaraEmitter& OriginalEmitterInstance, const TMap<FGuid, FGuid>& ChangeIdsThatNeedToBeReset) const;


private:
	struct FCachedMergeAdapter
	{
		FGuid ChangeId;
		TSharedPtr<FNiagaraEmitterMergeAdapter> EmitterMergeAdapter;
	};

	TMap<FObjectKey, FCachedMergeAdapter> CachedMergeAdapters;
};