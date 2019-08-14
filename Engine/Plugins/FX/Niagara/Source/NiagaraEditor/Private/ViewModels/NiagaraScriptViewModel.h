// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraParameterEditMode.h"
#include "INiagaraCompiler.h"
#include "ViewModels/TNiagaraViewModelManager.h"
#include "EditorUndoClient.h"
#include "Misc/NotifyHook.h"

class UNiagaraScript;
class UNiagaraScriptSource;
class INiagaraParameterCollectionViewModel;
class FNiagaraScriptGraphViewModel;
class FNiagaraScriptInputCollectionViewModel;
class FNiagaraScriptOutputCollectionViewModel;
class FNiagaraMetaDataCollectionViewModel;
class UNiagaraEmitter;
class FNiagaraObjectSelection;


/** A view model for Niagara scripts which manages other script related view models. */
class FNiagaraScriptViewModel : public TSharedFromThis<FNiagaraScriptViewModel>, public FEditorUndoClient, public TNiagaraViewModelManager<UNiagaraScript, FNiagaraScriptViewModel>
{
public:
	FNiagaraScriptViewModel(FText DisplayName, ENiagaraParameterEditMode InParameterEditMode);

	~FNiagaraScriptViewModel();

	/** Sets the view model to a different script. */
	void SetScript(UNiagaraScript* InScript);

	void SetScripts(UNiagaraEmitter* InEmitter);

	/** Gets the view model for the input parameter collection. */
	TSharedRef<FNiagaraScriptInputCollectionViewModel> GetInputCollectionViewModel();
	
	/** Gets the view model for the output parameter collection. */
	TSharedRef<FNiagaraScriptOutputCollectionViewModel> GetOutputCollectionViewModel();

	TSharedRef<INiagaraParameterCollectionViewModel> GetInputParameterMapViewModel();
	TSharedRef<INiagaraParameterCollectionViewModel> GetOutputParameterMapViewModel();

	/** Gets the view model for the graph. */
	TSharedRef<FNiagaraScriptGraphViewModel> GetGraphViewModel();

	/** Gets the currently selected script variables. */
	TSharedRef<FNiagaraObjectSelection> GetVariableSelection();

	/** Updates the script with the latest compile status. */
	void UpdateCompileStatus(ENiagaraScriptCompileStatus InAggregateCompileStatus, const FString& InAggregateCompileErrors,
		const TArray<ENiagaraScriptCompileStatus>& InCompileStatuses, const TArray<FString>& InCompileErrors, const TArray<FString>& InCompilePaths,
		const TArray<UNiagaraScript*>& InCompileSources);

	/** Compiles a script that isn't part of an emitter or System. */
	void CompileStandaloneScript(bool bForceCompile = false);

	/** Get the latest status of this view-model's script compilation.*/
	virtual ENiagaraScriptCompileStatus GetLatestCompileStatus();

	/** Refreshes the nodes in the script graph, updating the pins to match external changes. */
	void RefreshNodes();

	//~ FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }

	UNiagaraScript* GetContainerScript(ENiagaraScriptUsage InUsage, FGuid InUsageId = FGuid());
	UNiagaraScript* GetScript(ENiagaraScriptUsage InUsage, FGuid InUsageId = FGuid());

	ENiagaraScriptCompileStatus GetScriptCompileStatus(ENiagaraScriptUsage InUsage, FGuid InUsageId) const;
	FText GetScriptErrors(ENiagaraScriptUsage InUsage, FGuid InUsageId) const;

	/** If this is editing a standalone script, returns the script being edited.*/
	UNiagaraScript* GetStandaloneScript();

private:
	/** Handles the selection changing in the graph view model. */
	void GraphViewModelSelectedNodesChanged();

	/** Handles the selection changing in the input collection view model. */
	void InputViewModelSelectionChanged();

	/** Marks this script view model as dirty and marks the scripts as needing synchrnozation. */
	void MarkAllDirty(FString Reason);

	void SetScripts(UNiagaraScriptSource* InScriptSource, TArray<UNiagaraScript*>& InScripts);

	/** Handles when a value in the input parameter collection changes. */
	void InputParameterValueChanged(FName ParameterName);

	/** Handles when a value in the output parameter collection changes. */
	void OutputParameterValueChanged(FName ParameterName);

protected:
	/** The script which provides the data for this view model. */
	TArray<TWeakObjectPtr<UNiagaraScript>> Scripts;

	virtual void OnVMScriptCompiled(UNiagaraScript* InScript);

	TWeakObjectPtr<UNiagaraScriptSource> Source;

	/** The view model for the input parameter collection. */
	TSharedRef<FNiagaraScriptInputCollectionViewModel> InputCollectionViewModel;

	/** The view model for the output parameter collection .*/
	TSharedRef<FNiagaraScriptOutputCollectionViewModel> OutputCollectionViewModel;

	/** The view model for the graph. */
	TSharedRef<FNiagaraScriptGraphViewModel> GraphViewModel;

	/** The set of variables currently selected in the graph or the parameters panel. */
	TSharedRef<FNiagaraObjectSelection> VariableSelection;

	/** A flag for preventing reentrancy when synchronizing selection. */
	bool bUpdatingSelectionInternally;

	/** The stored latest compile status.*/
	ENiagaraScriptCompileStatus LastCompileStatus;
	
	/** The handle to the graph changed delegate needed for removing. */
	FDelegateHandle OnGraphChangedHandle;

	TArray<TNiagaraViewModelManager<UNiagaraScript, FNiagaraScriptViewModel>::Handle> RegisteredHandles;

	bool IsGraphDirty() const;

	TArray<ENiagaraScriptCompileStatus> CompileStatuses;
	TArray<FString> CompileErrors;
	TArray<FString> CompilePaths;
	TArray<TPair<ENiagaraScriptUsage, FGuid>> CompileTypes;
};
