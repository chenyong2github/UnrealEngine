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
	class UNiagaraGraph*	NodeGraph = nullptr;
	
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

	/** Synchronize all source script variables that have been changed or removed from the parameter definitions to all eligible destination script variables owned by the graph.
	 *
	 *  @param TargetDefinitions			The set of parameter definitions that will be synchronized with the graph parameters.
	 *	@param AllDefinitions				All parameter definitions in the project. Used to add new subscriptions to definitions if specified in Args.
	 *  @param AllDefinitionsParameterIds	All unique Ids of all parameter definitions.
	 *	@param Subscriber					The INiagaraParameterDefinitionsSubscriber that owns the graph. Used to add new subscriptions to definitions if specified in Args.
	 *	@param Args							Additional arguments that specify how to perform the synchronization.
	 */
	virtual void SynchronizeGraphParametersWithParameterDefinitions(
		const TArray<UNiagaraParameterDefinitionsBase*> TargetDefinitions,
		const TArray<UNiagaraParameterDefinitionsBase*> AllDefinitions,
		const TSet<FGuid>& AllDefinitionsParameterIds,
		INiagaraParameterDefinitionsSubscriber* Subscriber,
		FSynchronizeWithParameterDefinitionsArgs Args
	) override;

	/** Rename all graph assignment and map set node pins.
	 *  Used when synchronizing definitions with source scripts of systems and emitters.
	 */
	virtual void RenameGraphAssignmentAndSetNodePins(const FName OldName, const FName NewName);

private:
	void OnGraphChanged(const FEdGraphEditAction &Action);
	void OnGraphDataInterfaceChanged();
};
