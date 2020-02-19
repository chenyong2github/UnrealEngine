// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "NiagaraScratchPadViewModel.generated.h"

class FNiagaraSystemViewModel;
class FNiagaraScratchPadScriptViewModel;
class FNiagaraObjectSelection;
class UNiagaraScript;

UCLASS()
class NIAGARAEDITOR_API UNiagaraScratchPadViewModel : public UObject
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE(FOnScriptViewModelsChanged);
	DECLARE_MULTICAST_DELEGATE(FOnActiveScriptChanged);
	DECLARE_MULTICAST_DELEGATE(FOnScriptRenamed);
	DECLARE_MULTICAST_DELEGATE(FOnScriptDeleted);

public:
	void Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel);

	void Finalize();

	void RefreshScriptViewModels();

	const TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>>& GetScriptViewModels() const;

	TSharedPtr<FNiagaraScratchPadScriptViewModel> GetViewModelForScript(UNiagaraScript* InScript);

	const TArray<ENiagaraScriptUsage>& GetAvailableUsages() const;

	FText GetDisplayNameForUsage(ENiagaraScriptUsage InUsage) const;

	TSharedRef<FNiagaraObjectSelection> GetObjectSelection();

	UNiagaraScript* GetActiveScript();

	void SetActiveScript(UNiagaraScript* InActiveScript);

	void DeleteActiveScript();

	TSharedPtr<FNiagaraScratchPadScriptViewModel> CreateNewScript(ENiagaraScriptUsage InScriptUsage, ENiagaraScriptUsage InTargetSupportedUsage, FNiagaraTypeDefinition InOutputType);

	TSharedPtr<FNiagaraScratchPadScriptViewModel> CreateNewScriptAsDuplicate(UNiagaraScript* ScriptToDuplicate);

	FOnScriptViewModelsChanged& OnScriptViewModelsChanged();

	FOnActiveScriptChanged& OnActiveScriptChanged();

	FOnScriptRenamed& OnScriptRenamed();

	FOnScriptDeleted& OnScriptDeleted();

private:
	TSharedRef<FNiagaraSystemViewModel> GetSystemViewModel();

	void ScriptGraphNodeSelectionChanged(TWeakPtr<FNiagaraScratchPadScriptViewModel> InScriptViewModelWeak);

	void ScriptViewModelScriptRenamed();

private:
	TSharedPtr<FNiagaraObjectSelection> ObjectSelection;

	UNiagaraScript* ActiveScript;

	TWeakPtr<FNiagaraSystemViewModel> SystemViewModelWeak;

	TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> ScriptViewModels;

	TArray<ENiagaraScriptUsage> AvailableUsages;

	FOnScriptViewModelsChanged OnScriptViewModelsChangedDelegate;

	FOnActiveScriptChanged OnActiveScriptChangedDelegate;

	FOnScriptRenamed OnScriptRenamedDelegate;

	FOnScriptDeleted OnScriptDeletedDelegate;
};