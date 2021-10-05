// Copyright Epic Games, Inc. All Rights Reserved.


#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "NiagaraScriptSourceBase.h"
#include "INiagaraCompiler.h"
#include "NiagaraParameterMapHistory.h"
#include "GraphEditAction.h"
#include "NiagaraScriptSource.generated.h"

UCLASS(MinimalAPI)
class UNiagaraScriptSource : public UNiagaraScriptSourceBase
{
	GENERATED_UCLASS_BODY()

	/** Graph for particle update expression */
	UPROPERTY()
	TObjectPtr<class UNiagaraGraph>	NodeGraph;
	
	// UObject interface
	virtual void PostLoad() override;

	// UNiagaraScriptSourceBase interface.
	//virtual ENiagaraScriptCompileStatus Compile(UNiagaraScript* ScriptOwner, FString& OutGraphLevelErrorMessages) override;
	virtual bool IsSynchronized(const FGuid& InChangeId) override;
	virtual void MarkNotSynchronized(FString Reason) override;

	virtual FGuid GetChangeID();

	virtual void ComputeVMCompilationId(struct FNiagaraVMExecutableDataId& Id, ENiagaraScriptUsage InUsage, const FGuid& InUsageId, bool bForceRebuild = false) const override;

	virtual FGuid GetCompileBaseId(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const override;

	virtual FNiagaraCompileHash GetCompileHash(ENiagaraScriptUsage InUsage, const FGuid& InUsageId) const override;

	virtual void PostLoadFromEmitter(UNiagaraEmitter& OwningEmitter) override;

	NIAGARAEDITOR_API virtual bool AddModuleIfMissing(FString ModulePath, ENiagaraScriptUsage Usage, bool& bOutFoundModule)override;

	virtual void FixupRenamedParameters(UNiagaraNode* Node, FNiagaraParameterStore& RapidIterationParameters, const TArray<FNiagaraVariable>& OldRapidIterationVariables, const TSet<FName>& ValidRapidIterationParameterNames, const UNiagaraEmitter* Emitter, ENiagaraScriptUsage ScriptUsage) const;
	virtual void CleanUpOldAndInitializeNewRapidIterationParameters(const UNiagaraEmitter* Emitter, ENiagaraScriptUsage ScriptUsage, FGuid ScriptUsageId, FNiagaraParameterStore& RapidIterationParameters) const override;
	virtual void ForceGraphToRecompileOnNextCheck() override;
	virtual void RefreshFromExternalChanges() override;

	virtual void CollectDataInterfaces(TArray<const UNiagaraDataInterfaceBase*>& DataInterfaces) const override;

	/** Synchronize all source script variables that have been changed or removed from the parameter definitions to all eligible destination script variables owned by the nodegraph. */
	virtual void SynchronizeGraphParametersWithParameterDefinitions(const TArray<UNiagaraParameterDefinitionsBase*> ParameterDefinitions, const TArray<FGuid>& ParameterDefinitionsParameterIds, FSynchronizeWithParameterDefinitionsArgs Args) override;

	/** Rename all graph assignment and map set node pins.
	 *  Used when synchronizing definitions with source scripts of systems and emitters.
	 */
	virtual void RenameGraphAssignmentAndSetNodePins(const FName OldName, const FName NewName);

private:
	void OnGraphChanged(const FEdGraphEditAction &Action);
	void OnGraphDataInterfaceChanged();
};
