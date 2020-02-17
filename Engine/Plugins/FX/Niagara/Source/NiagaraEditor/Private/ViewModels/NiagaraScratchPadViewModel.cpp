// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/NiagaraScriptGraphViewModel.h"
#include "NiagaraSystem.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraObjectSelection.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraNodeFunctionCall.h"

#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NiagaraScratchPadViewModel"

void UNiagaraScratchPadViewModel::Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModelWeak = InSystemViewModel;
	ObjectSelection = MakeShared<FNiagaraObjectSelection>();
	RefreshScriptViewModels();
	AvailableUsages = { ENiagaraScriptUsage::DynamicInput, ENiagaraScriptUsage::Module };
}

void UNiagaraScratchPadViewModel::Finalize()
{
}

void GetOuterAndTargetScripts(TSharedRef<FNiagaraSystemViewModel> SystemViewModel, UObject*& OutOuter, TArray<UNiagaraScript*>*& OutTargetScripts)
{
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		OutOuter = &SystemViewModel->GetSystem();
		OutTargetScripts = &SystemViewModel->GetSystem().ScratchPadScripts;
	}
	else
	{
		if (SystemViewModel->GetSystem().GetEmitterHandles().Num() == 1)
		{
			UNiagaraEmitter* TargetEmitter = SystemViewModel->GetSystem().GetEmitterHandles()[0].GetInstance();
			OutOuter = TargetEmitter;
			OutTargetScripts = &TargetEmitter->ScratchPadScripts;
		}
		else
		{
			OutOuter = nullptr;
			OutTargetScripts = nullptr;
		}
	}
}

void UNiagaraScratchPadViewModel::RefreshScriptViewModels()
{
	TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> OldScriptViewModels = ScriptViewModels;
	ScriptViewModels.Empty();

	bool bNewViewModels = false;

	UObject* ScriptOuter;
	TArray<UNiagaraScript*>* TargetScripts;
	GetOuterAndTargetScripts(GetSystemViewModel(), ScriptOuter, TargetScripts);

	if (ScriptOuter != nullptr && TargetScripts != nullptr)
	{
		for (UNiagaraScript* ScratchPadScript : (*TargetScripts))
		{
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModel;

			TSharedRef<FNiagaraScratchPadScriptViewModel>* OldScriptViewModel = OldScriptViewModels.FindByPredicate(
				[ScratchPadScript](TSharedRef<FNiagaraScratchPadScriptViewModel> ScriptViewModel) { return ScriptViewModel->GetScript() == ScratchPadScript; });
			if (OldScriptViewModel != nullptr)
			{
				ScriptViewModel = *OldScriptViewModel;
				OldScriptViewModels.Remove(ScriptViewModel.ToSharedRef());
			}
			else
			{
				ScriptViewModel = MakeShared<FNiagaraScratchPadScriptViewModel>();
				ScriptViewModel->Initialize(ScratchPadScript);
				ScriptViewModel->GetGraphViewModel()->GetNodeSelection()->OnSelectedObjectsChanged().AddUObject(this, &UNiagaraScratchPadViewModel::ScriptGraphNodeSelectionChanged, TWeakPtr<FNiagaraScratchPadScriptViewModel>(ScriptViewModel));
				ScriptViewModel->OnRenamed().AddUObject(this, &UNiagaraScratchPadViewModel::ScriptViewModelScriptRenamed);
				bNewViewModels = true;
			}
			ScriptViewModels.Add(ScriptViewModel.ToSharedRef());
		}
	}

	for (TSharedRef<FNiagaraScratchPadScriptViewModel> OldScriptViewModel : OldScriptViewModels)
	{
		OldScriptViewModel->OnRenamed().RemoveAll(this);
	}

	if (bNewViewModels || OldScriptViewModels.Num() > 0)
	{
		OnScriptViewModelsChangedDelegate.Broadcast();
	}
}

const TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>>& UNiagaraScratchPadViewModel::GetScriptViewModels() const
{
	return ScriptViewModels;
}

TSharedPtr<FNiagaraScratchPadScriptViewModel> UNiagaraScratchPadViewModel::GetViewModelForScript(UNiagaraScript* InScript)
{
	TSharedRef<FNiagaraScratchPadScriptViewModel>* ViewModelForScript = ScriptViewModels.FindByPredicate([InScript](TSharedRef<FNiagaraScratchPadScriptViewModel>& ScriptViewModel) { return ScriptViewModel->GetScript() == InScript; });
	if (ViewModelForScript != nullptr)
	{
		return *ViewModelForScript;
	}
	return TSharedPtr<FNiagaraScratchPadScriptViewModel>();
}

const TArray<ENiagaraScriptUsage>& UNiagaraScratchPadViewModel::GetAvailableUsages() const
{
	return AvailableUsages;
}

FText UNiagaraScratchPadViewModel::GetDisplayNameForUsage(ENiagaraScriptUsage InUsage) const
{
	switch (InUsage)
	{
	case ENiagaraScriptUsage::DynamicInput:
		return LOCTEXT("DynamicInputDisplayName", "Dynamic Inputs");
	case ENiagaraScriptUsage::Module:
		return LOCTEXT("ModuleDisplayName", "Modules");
	case ENiagaraScriptUsage::Function:
		return LOCTEXT("FunctionDisplayName", "Functions");
	default: 
		return LOCTEXT("InvalidUsageDisplayName", "Invalid");
	}
}

TSharedRef<FNiagaraObjectSelection> UNiagaraScratchPadViewModel::GetObjectSelection()
{
	return ObjectSelection.ToSharedRef();
}

UNiagaraScript* UNiagaraScratchPadViewModel::GetActiveScript()
{
	return ActiveScript;
}

void UNiagaraScratchPadViewModel::SetActiveScript(UNiagaraScript* InActiveScript)
{
	ActiveScript = InActiveScript;
	if (ActiveScript != nullptr)
	{
		ObjectSelection->SetSelectedObject(ActiveScript);
	}
	else
	{
		ObjectSelection->ClearSelectedObjects();
	}
	OnActiveScriptChangedDelegate.Broadcast();
}

void UNiagaraScratchPadViewModel::DeleteActiveScript()
{
	if (ActiveScript != nullptr)
	{
		FScopedTransaction DeleteTransaction(LOCTEXT("DeleteScratchPadScriptTransaction", "Delete scratch pad script."));
		for (TObjectIterator<UNiagaraNodeFunctionCall> It; It; ++It)
		{
			UNiagaraNodeFunctionCall* FunctionCallNode = *It;
			if (FunctionCallNode->FunctionScript == ActiveScript)
			{
				FunctionCallNode->Modify();
				FunctionCallNode->FunctionScript = nullptr;
			}
		}

		UObject* ScriptOuter;
		TArray<UNiagaraScript*>* TargetScripts;
		GetOuterAndTargetScripts(GetSystemViewModel(), ScriptOuter, TargetScripts);

		ScriptOuter->Modify();
		TargetScripts->Remove(ActiveScript);

		OnScriptDeletedDelegate.Broadcast();
		RefreshScriptViewModels();
	}
}

TSharedPtr<FNiagaraScratchPadScriptViewModel> UNiagaraScratchPadViewModel::CreateNewScript(ENiagaraScriptUsage InScriptUsage, ENiagaraScriptUsage InTargetSupportedUsage, FNiagaraTypeDefinition InOutputType)
{
	UObject* ScriptOuter;
	TArray<UNiagaraScript*>* TargetScripts;
	GetOuterAndTargetScripts(GetSystemViewModel(), ScriptOuter, TargetScripts);

	UNiagaraScript* NewScript = nullptr;
	switch (InScriptUsage)
	{
	case ENiagaraScriptUsage::DynamicInput:
	{
		UNiagaraScript* DefaultDynamicInput = Cast<UNiagaraScript>(GetDefault<UNiagaraEditorSettings>()->DefaultDynamicInputScript.TryLoad());
		if (DefaultDynamicInput != nullptr)
		{
			NewScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(DefaultDynamicInput, ScriptOuter, MakeUniqueObjectName(ScriptOuter, UNiagaraScript::StaticClass(), TEXT("NewScratchDynamicInput"))));
			TArray<UNiagaraNodeOutput*> OutputNodes;
			CastChecked<UNiagaraScriptSource>(NewScript->GetSource())->NodeGraph->GetNodesOfClass(OutputNodes);
			if (OutputNodes.Num() == 1)
			{
				if (InOutputType.IsValid())
				{
					UNiagaraNodeOutput* DynamicInputOutputNode = OutputNodes[0];
					DynamicInputOutputNode->Outputs.Empty();
					DynamicInputOutputNode->Outputs.Add(FNiagaraVariable(InOutputType, "Output"));
					FPropertyChangedEvent OutputsChangedEvent(FindFieldChecked<FProperty>(UNiagaraNodeOutput::StaticClass(), GET_MEMBER_NAME_CHECKED(UNiagaraNodeOutput, Outputs)));
					DynamicInputOutputNode->PostEditChangeProperty(OutputsChangedEvent);
				}
			}
		}
		break;
	}
	case ENiagaraScriptUsage::Module:
	{
		UNiagaraScript* DefaultModule = Cast<UNiagaraScript>(GetDefault<UNiagaraEditorSettings>()->DefaultModuleScript.TryLoad());
		if (DefaultModule != nullptr)
		{
			NewScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(DefaultModule, ScriptOuter, MakeUniqueObjectName(ScriptOuter, UNiagaraScript::StaticClass(), TEXT("NewScratchModule"))));
		}
		break;
	}
	}

	if (NewScript != nullptr)
	{
		NewScript->ClearFlags(RF_Public | RF_Standalone);
		ScriptOuter->Modify();
		TargetScripts->Add(NewScript);
		NewScript->ModuleUsageBitmask |= (1 << (int32)InTargetSupportedUsage);
		RefreshScriptViewModels();
	}

	return GetViewModelForScript(NewScript);
}

TSharedPtr<FNiagaraScratchPadScriptViewModel> UNiagaraScratchPadViewModel::CreateNewScriptAsDuplicate(UNiagaraScript* ScriptToDuplicate)
{
	UObject* ScriptOuter;
	TArray<UNiagaraScript*>* TargetScripts;
	GetOuterAndTargetScripts(GetSystemViewModel(), ScriptOuter, TargetScripts);

	UNiagaraScript* NewScript = NewScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(ScriptToDuplicate, ScriptOuter, MakeUniqueObjectName(ScriptOuter, UNiagaraScript::StaticClass(), ScriptToDuplicate->GetFName())));
	NewScript->ClearFlags(RF_Public | RF_Standalone);
	ScriptOuter->Modify();
	TargetScripts->Add(NewScript);
	RefreshScriptViewModels();

	return GetViewModelForScript(NewScript);
}

UNiagaraScratchPadViewModel::FOnScriptViewModelsChanged& UNiagaraScratchPadViewModel::OnScriptViewModelsChanged()
{
	return OnScriptViewModelsChangedDelegate;
}

UNiagaraScratchPadViewModel::FOnActiveScriptChanged& UNiagaraScratchPadViewModel::OnActiveScriptChanged()
{
	return OnActiveScriptChangedDelegate;
}

UNiagaraScratchPadViewModel::FOnScriptRenamed& UNiagaraScratchPadViewModel::OnScriptRenamed()
{
	return OnScriptRenamedDelegate;
}

UNiagaraScratchPadViewModel::FOnScriptDeleted& UNiagaraScratchPadViewModel::OnScriptDeleted()
{
	return OnScriptDeletedDelegate;
}

TSharedRef<FNiagaraSystemViewModel> UNiagaraScratchPadViewModel::GetSystemViewModel()
{
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel = SystemViewModelWeak.Pin();
	checkf(SystemViewModel.IsValid(), TEXT("SystemViewModel destroyed before scratch pad view model."));
	return SystemViewModel.ToSharedRef();
}

void UNiagaraScratchPadViewModel::ScriptGraphNodeSelectionChanged(TWeakPtr<FNiagaraScratchPadScriptViewModel> InScriptViewModelWeak)
{
	TSharedPtr<FNiagaraScratchPadScriptViewModel> InScriptViewModel = InScriptViewModelWeak.Pin();
	if (InScriptViewModel.IsValid())
	{
		TArray<UObject*> SelectedNodes = InScriptViewModel->GetGraphViewModel()->GetNodeSelection()->GetSelectedObjects().Array();
		if (SelectedNodes.Num() > 0)
		{
			ObjectSelection->SetSelectedObjects(SelectedNodes);
		}
		else if (ActiveScript != nullptr)
		{
			ObjectSelection->SetSelectedObject(ActiveScript);
		}
		else
		{
			ObjectSelection->ClearSelectedObjects();
		}
	}
}

void UNiagaraScratchPadViewModel::ScriptViewModelScriptRenamed()
{
	OnScriptRenamed().Broadcast();
}

#undef LOCTEXT_NAMESPACE