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
#include "NiagaraEditorModule.h"
#include "NiagaraClipboard.h"

#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NiagaraScratchPadViewModel"

void UNiagaraScratchPadViewModel::Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModelWeak = InSystemViewModel;
	ObjectSelection = MakeShared<FNiagaraObjectSelection>();
	RefreshScriptViewModels();
	if (ScriptViewModels.Num() > 0)
	{
		SetActiveScriptViewModel(ScriptViewModels[0]);
	}
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

	bool bViewModelsChanged = false;

	UObject* ScriptOuter;
	TArray<UNiagaraScript*>* TargetScripts;
	GetOuterAndTargetScripts(GetSystemViewModel(), ScriptOuter, TargetScripts);

	if (ScriptOuter != nullptr && TargetScripts != nullptr)
	{
		for (UNiagaraScript* ScratchPadScript : (*TargetScripts))
		{
			TSharedPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModel;

			TSharedRef<FNiagaraScratchPadScriptViewModel>* OldScriptViewModel = OldScriptViewModels.FindByPredicate(
				[ScratchPadScript](TSharedRef<FNiagaraScratchPadScriptViewModel> ScriptViewModel) { return ScriptViewModel->GetOriginalScript() == ScratchPadScript; });
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
				ScriptViewModel->OnPinnedChanged().AddUObject(this, &UNiagaraScratchPadViewModel::ScriptViewModelPinnedChanged, TWeakPtr<FNiagaraScratchPadScriptViewModel>(ScriptViewModel));
				bViewModelsChanged = true;
			}
			ScriptViewModels.Add(ScriptViewModel.ToSharedRef());
		}
	}

	if (OldScriptViewModels.Num() > 0)
	{
		for (TSharedRef<FNiagaraScratchPadScriptViewModel> OldScriptViewModel : OldScriptViewModels)
		{
			OldScriptViewModel->OnRenamed().RemoveAll(this);
			OldScriptViewModel->OnPinnedChanged().RemoveAll(this);
		}
		bViewModelsChanged = true;
	}

	bool bEditViewModelsChanged = false;
	if (ActiveScriptViewModel.IsValid() && ScriptViewModels.Contains(ActiveScriptViewModel.ToSharedRef()) == false)
	{
		ActiveScriptViewModel.Reset();
		bEditViewModelsChanged = true;
	}

	TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>> OldPinnedScriptViewModels = PinnedScriptViewModels;
	PinnedScriptViewModels.Empty();
	for (TSharedRef<FNiagaraScratchPadScriptViewModel> OldPinnedScriptViewModel : OldPinnedScriptViewModels)
	{
		// Remove pinned view models which are no longer valid, but add them one at a time to maintain the pin order.
		if (ScriptViewModels.Contains(OldPinnedScriptViewModel))
		{
			PinnedScriptViewModels.Add(OldPinnedScriptViewModel);
		}
		else
		{
			bEditViewModelsChanged = true;
		}
	}

	if (bEditViewModelsChanged)
	{
		RefreshEditScriptViewModels();
	}
	
	if (bViewModelsChanged)
	{
		OnScriptViewModelsChangedDelegate.Broadcast();
	}
}

const TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>>& UNiagaraScratchPadViewModel::GetScriptViewModels() const
{
	return ScriptViewModels;
}

const TArray<TSharedRef<FNiagaraScratchPadScriptViewModel>>& UNiagaraScratchPadViewModel::GetEditScriptViewModels() const
{
	return EditScriptViewModels;
}

TSharedPtr<FNiagaraScratchPadScriptViewModel> UNiagaraScratchPadViewModel::GetViewModelForScript(UNiagaraScript* InScript)
{
	TSharedRef<FNiagaraScratchPadScriptViewModel>* ViewModelForScript = ScriptViewModels.FindByPredicate([InScript](TSharedRef<FNiagaraScratchPadScriptViewModel>& ScriptViewModel) { return ScriptViewModel->GetOriginalScript() == InScript; });
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

TSharedPtr<FNiagaraScratchPadScriptViewModel> UNiagaraScratchPadViewModel::GetActiveScriptViewModel()
{
	return ActiveScriptViewModel;
}

void UNiagaraScratchPadViewModel::SetActiveScriptViewModel(TSharedRef<FNiagaraScratchPadScriptViewModel> InActiveScriptViewModel )
{
	if (ensureMsgf(ScriptViewModels.Contains(InActiveScriptViewModel), TEXT("Can only set an active view model from this scratch pad view model.")))
	{
		ActiveScriptViewModel = InActiveScriptViewModel;
		ObjectSelection->SetSelectedObject(ActiveScriptViewModel->GetEditScript());
		RefreshEditScriptViewModels();
		OnActiveScriptChangedDelegate.Broadcast();
	}
}

void UNiagaraScratchPadViewModel::ResetActiveScriptViewModel()
{
	if (ActiveScriptViewModel.IsValid())
	{
		ActiveScriptViewModel.Reset();
		ObjectSelection->ClearSelectedObjects();
		RefreshEditScriptViewModels();
		OnActiveScriptChangedDelegate.Broadcast();
	}
}

void UNiagaraScratchPadViewModel::CopyActiveScript()
{
	if (ActiveScriptViewModel.IsValid())
	{
		UNiagaraClipboardContent* ClipboardContent = UNiagaraClipboardContent::Create();
		ClipboardContent->Scripts.Add(CastChecked<UNiagaraScript>(StaticDuplicateObject(ActiveScriptViewModel->GetOriginalScript(), ClipboardContent)));
		FNiagaraEditorModule::Get().GetClipboard().SetClipboardContent(ClipboardContent);
	}
}

bool UNiagaraScratchPadViewModel::CanPasteScript() const
{
	const UNiagaraClipboardContent* ClipboardContent = FNiagaraEditorModule::Get().GetClipboard().GetClipboardContent();
	return ClipboardContent != nullptr && ClipboardContent->Scripts.Num() == 1;
}

void UNiagaraScratchPadViewModel::PasteScript()
{
	if (CanPasteScript())
	{
		FScopedTransaction Transaction(LOCTEXT("PasteScratchPadScriptTransaction", "Paste the scratch pad script from the system clipboard."));
		const UNiagaraClipboardContent* ClipboardContent = FNiagaraEditorModule::Get().GetClipboard().GetClipboardContent();
		TSharedPtr<FNiagaraScratchPadScriptViewModel> PastedScriptViewModel = CreateNewScriptAsDuplicate(ClipboardContent->Scripts[0]);
		SetActiveScriptViewModel(PastedScriptViewModel.ToSharedRef());
	}
}

void UNiagaraScratchPadViewModel::DeleteActiveScript()
{
	if (ActiveScriptViewModel.IsValid())
	{
		UNiagaraScript* ActiveScript = ActiveScriptViewModel->GetOriginalScript();
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

FName GetUniqueScriptName(UObject* Outer, const FString& CandidateName)
{
	return FNiagaraEditorUtilities::GetUniqueObjectName<UNiagaraScript>(Outer, CandidateName);
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
			NewScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(DefaultDynamicInput, ScriptOuter, GetUniqueScriptName(ScriptOuter, TEXT("NewScratchDynamicInput"))));
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
			NewScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(DefaultModule, ScriptOuter, GetUniqueScriptName(ScriptOuter, TEXT("NewScratchModule"))));
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

TSharedPtr<FNiagaraScratchPadScriptViewModel> UNiagaraScratchPadViewModel::CreateNewScriptAsDuplicate(const UNiagaraScript* ScriptToDuplicate)
{
	UObject* ScriptOuter;
	TArray<UNiagaraScript*>* TargetScripts;
	GetOuterAndTargetScripts(GetSystemViewModel(), ScriptOuter, TargetScripts);

	UNiagaraScript* NewScript = NewScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(ScriptToDuplicate, ScriptOuter, GetUniqueScriptName(ScriptOuter, *ScriptToDuplicate->GetFName().ToString())));
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

UNiagaraScratchPadViewModel::FOnScriptViewModelsChanged& UNiagaraScratchPadViewModel::OnEditScriptViewModelsChanged()
{
	return OnEditScriptViewModelsChangedDelegate;
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

void UNiagaraScratchPadViewModel::RefreshEditScriptViewModels()
{
	EditScriptViewModels.Empty();
	EditScriptViewModels.Append(PinnedScriptViewModels);
	if (ActiveScriptViewModel.IsValid())
	{
		EditScriptViewModels.AddUnique(ActiveScriptViewModel.ToSharedRef());
	}
	OnEditScriptViewModelsChangedDelegate.Broadcast();
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
		else if (ActiveScriptViewModel.IsValid())
		{
			ObjectSelection->SetSelectedObject(ActiveScriptViewModel->GetEditScript());
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

void UNiagaraScratchPadViewModel::ScriptViewModelPinnedChanged(TWeakPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModelWeak)
{
	TSharedPtr<FNiagaraScratchPadScriptViewModel> ScriptViewModel = ScriptViewModelWeak.Pin();
	bool bPinnedCollectionChanged = false;
	if (ScriptViewModel.IsValid())
	{
		if (ScriptViewModel->GetIsPinned())
		{
			PinnedScriptViewModels.AddUnique(ScriptViewModel.ToSharedRef());
			if (ActiveScriptViewModel != ScriptViewModel)
			{
				SetActiveScriptViewModel(ScriptViewModel.ToSharedRef());
			}
			else
			{
				RefreshEditScriptViewModels();
			}
		}
		else
		{
			PinnedScriptViewModels.Remove(ScriptViewModel.ToSharedRef());
			if (PinnedScriptViewModels.Num() == 0)
			{
				if (ActiveScriptViewModel.IsValid() == false)
				{
					// When unpinning the last script, and there is no active script set it as the active script so that it remains displayed in the UI.
					SetActiveScriptViewModel(ScriptViewModel.ToSharedRef());
				}	
			}
			else
			{
				if (ActiveScriptViewModel == ScriptViewModel)
				{
					SetActiveScriptViewModel(PinnedScriptViewModels.Last());
				}
				else
				{
					RefreshEditScriptViewModels();
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE