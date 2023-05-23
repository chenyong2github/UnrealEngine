// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraHlslTranslator.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraSimulationStageCompileData.h"
#include "NiagaraTypes.h"
#include "Templates/SharedPointer.h"

class FCompileConstantResolver;
class FNiagaraCompileRequestData;
class FNiagaraCompileRequestDuplicateData;
class UNiagaraGraph;
struct FNiagaraGraphCachedBuiltHistory;
class UNiagaraScriptSourceBase;

struct FNiagaraSimulationStageInfo
{
	FGuid StageId;
	bool bEnabled = true;
	bool bGenericStage = false;
	ENiagaraIterationSource IterationSource = ENiagaraIterationSource::Particles;
	FName DataInterfaceBindingName;
	bool bHasCompilationData = false;
	FNiagaraSimulationStageCompilationData CompilationData;
};

class FNiagaraCompileRequestData : public FNiagaraCompileRequestDataBase
{
public:
	FNiagaraCompileRequestData()
	{

	}

	virtual void GatherPreCompiledVariables(const FString& InNamespaceFilter, TArray<FNiagaraVariable>& OutVars) const override;
	virtual FName ResolveEmitterAlias(FName VariableName) const override;

	const FString& GetUniqueEmitterName() const { return EmitterUniqueName; }
	void FinishPrecompile(const TArray<FNiagaraVariable>& EncounterableVariables, const TArray<FNiagaraVariable>& InStaticVariables, FCompileConstantResolver ConstantResolver, const TArray<ENiagaraScriptUsage>& UsagesToProcess, const TArray<class UNiagaraSimulationStageBase*>* SimStages, const TArray<FString> EmitterNames);
	virtual int32 GetDependentRequestCount() const override {
		return EmitterData.Num();
	};
	virtual TSharedPtr<FNiagaraCompileRequestDataBase, ESPMode::ThreadSafe> GetDependentRequest(int32 Index) override {
		return EmitterData[Index];
	}
	virtual const FNiagaraCompileRequestDataBase* GetDependentRequest(int32 Index) const override
	{
		return EmitterData[Index].Get();
	}
	void AddRapidIterationParameters(const FNiagaraParameterStore& InParamStore, FCompileConstantResolver InResolver);
	virtual bool GetUseRapidIterationParams() const override { return bUseRapidIterationParams; }

	virtual bool GetDisableDebugSwitches() const override { return bDisableDebugSwitches; }

	// Simulation Stage Variables. Sim stage of 0 is always Spawn/Update
	TArray<FNiagaraSimulationStageCompilationData> CompileSimStageData;

	struct FCompileDataInterfaceData
	{
		FString EmitterName;
		ENiagaraScriptUsage Usage;
		FGuid UsageId;
		FNiagaraVariable Variable;
		TArray<FString> ReadsEmitterParticleData;
	};
	TSharedPtr<TArray<FCompileDataInterfaceData>> SharedCompileDataInterfaceData;

	TArray<FNiagaraVariable> EncounteredVariables;
	FString EmitterUniqueName;
	TArray<TSharedPtr<FNiagaraCompileRequestData, ESPMode::ThreadSafe>> EmitterData;
	TWeakObjectPtr<UNiagaraScriptSource> Source;
	FString SourceName;
	bool bUseRapidIterationParams = true;
	bool bDisableDebugSwitches = false;

	UEnum* ENiagaraScriptCompileStatusEnum = nullptr;
	UEnum* ENiagaraScriptUsageEnum = nullptr;

	TArray<FNiagaraVariable> RapidIterationParams;

	TMap<FGraphTraversalHandle, FString> PinToConstantValues;
	TArray<FNiagaraVariable> StaticVariables;
	TArray<FNiagaraVariable> StaticVariablesWithMultipleWrites;

	template<typename T> T GetStaticVariableValue(const FNiagaraVariableBase Variable, const T DefaultValue) const
	{
		const int32 Index = StaticVariables.Find(Variable);
		return Index != INDEX_NONE && StaticVariables[Index].IsDataAllocated() ? StaticVariables[Index].GetValue<T>() : DefaultValue;
	}

	void CompareAgainst(FNiagaraGraphCachedBuiltHistory* InCachedDataBase);

	static void SortOutputNodesByDependencies(TArray<class UNiagaraNodeOutput*>& NodesToSort, const TArray<class UNiagaraSimulationStageBase*>* SimStages);
};

class FNiagaraCompileRequestDuplicateData : public FNiagaraCompileRequestDuplicateDataBase
{
public:
	FNiagaraCompileRequestDuplicateData()
	{
	}

	virtual bool IsDuplicateDataFor(UNiagaraSystem* InSystem, UNiagaraEmitter* InEmitter, UNiagaraScript* InScript) const override;
	virtual void GetDuplicatedObjects(TArray<UObject*>& Objects) override;
	virtual const TMap<FName, UNiagaraDataInterface*>& GetObjectNameMap() override;
	virtual const UNiagaraScriptSourceBase* GetScriptSource() const override;
	UNiagaraDataInterface* GetDuplicatedDataInterfaceCDOForClass(UClass* Class) const;

	TArray<FNiagaraParameterMapHistory>& GetPrecomputedHistories() { return PrecompiledHistories; }
	const TArray<FNiagaraParameterMapHistory>& GetPrecomputedHistories() const { return PrecompiledHistories; }

	void DuplicateReferencedGraphs(UNiagaraGraph* InSrcGraph, UNiagaraGraph* InDupeGraph, ENiagaraScriptUsage InUsage, FCompileConstantResolver ConstantResolver, TMap<UNiagaraNodeFunctionCall*, ENiagaraScriptUsage> FunctionsWithUsage = TMap<UNiagaraNodeFunctionCall*, ENiagaraScriptUsage>());
	void DeepCopyGraphs(UNiagaraScriptSource* ScriptSource, ENiagaraScriptUsage InUsage, FCompileConstantResolver ConstantResolver);
	void DeepCopyGraphs(const FVersionedNiagaraEmitter& Emitter);

	void FinishPrecompileDuplicate(const TArray<FNiagaraVariable>& EncounterableVariables, const TArray<FNiagaraVariable>& InStaticVariables, FCompileConstantResolver ConstantResolver, const TArray<class UNiagaraSimulationStageBase*>* SimStages, const TArray<FNiagaraVariable>& InParamStore);
	void CreateDataInterfaceCDO(TArrayView<UClass*> VariableDataInterfaces);

	virtual int32 GetDependentRequestCount() const override { return EmitterData.Num(); }
	virtual TSharedPtr<FNiagaraCompileRequestDuplicateDataBase, ESPMode::ThreadSafe> GetDependentRequest(int32 Index) override { return EmitterData[Index]; }

	virtual void ReleaseCompilationCopies() override;

	TWeakObjectPtr<UNiagaraSystem> OwningSystem;
	TWeakObjectPtr<UNiagaraEmitter> OwningEmitter;
	TArray<ENiagaraScriptUsage> ValidUsages;

	TWeakObjectPtr<UNiagaraScriptSource> SourceDeepCopy;
	TWeakObjectPtr<UNiagaraGraph> NodeGraphDeepCopy;

	TArray<FNiagaraParameterMapHistory> PrecompiledHistories;

	TArray<FNiagaraVariable> ChangedFromNumericVars;
	FString EmitterUniqueName;
	TArray<TSharedPtr<FNiagaraCompileRequestDuplicateData, ESPMode::ThreadSafe>> EmitterData;

	struct FDuplicatedGraphData
	{
		UNiagaraScript* ClonedScript;
		UNiagaraGraph* ClonedGraph;
		TArray<UEdGraphPin*> CallInputs;
		TArray<UEdGraphPin*> CallOutputs;
		ENiagaraScriptUsage Usage;
		bool bHasNumericParameters;
	};

	TSharedPtr<TMap<const UNiagaraGraph*, TArray<FDuplicatedGraphData>>> SharedSourceGraphToDuplicatedGraphsMap;
	TSharedPtr<TMap<FName, UNiagaraDataInterface*>> SharedNameToDuplicatedDataInterfaceMap;
	TSharedPtr<TMap<UClass*, UNiagaraDataInterface*>> SharedDataInterfaceClassToDuplicatedCDOMap;

protected:
	void DuplicateReferencedGraphsRecursive(UNiagaraGraph* InGraph, const FCompileConstantResolver& ConstantResolver, TMap<UNiagaraNodeFunctionCall*, ENiagaraScriptUsage> FunctionsWithUsage);
private:
	TArray<TWeakObjectPtr<UNiagaraScriptSource>> TrackedScriptSourceCopies;
};
