// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraStackFunctionInputCollection.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEditorUtilities.h"

#include "ObjectTools.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "NiagaraScratchPadScriptViewModel"

FNiagaraScratchPadScriptViewModel::FNiagaraScratchPadScriptViewModel()
	: FNiagaraScriptViewModel(TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateRaw(this, &FNiagaraScratchPadScriptViewModel::GetDisplayNameInternal)), ENiagaraParameterEditMode::EditAll)
	, bIsPendingRename(false)
	, bIsPinned(false)
	, EditorHeight(300)
	, bHasPendingChanges(false)
{
}

FNiagaraScratchPadScriptViewModel::~FNiagaraScratchPadScriptViewModel()
{
	if (EditScript != nullptr)
	{
		if (EditScript->GetSource() != nullptr)
		{
			UNiagaraScriptSource* EditScriptSource = CastChecked<UNiagaraScriptSource>(EditScript->GetSource());
			EditScriptSource->NodeGraph->RemoveOnGraphNeedsRecompileHandler(OnGraphNeedsRecompileHandle);
		}
		EditScript->OnPropertyChanged().RemoveAll(this);
		EditScript = nullptr;
	}
}

void FNiagaraScratchPadScriptViewModel::Initialize(UNiagaraScript* Script)
{
	OriginalScript = Script;
	EditScript = CastChecked<UNiagaraScript>(StaticDuplicateObject(Script, GetTransientPackage()));
	SetScript(EditScript);
	UNiagaraScriptSource* EditScriptSource = CastChecked<UNiagaraScriptSource>(EditScript->GetSource());
	OnGraphNeedsRecompileHandle = EditScriptSource->NodeGraph->AddOnGraphNeedsRecompileHandler(FOnGraphChanged::FDelegate::CreateSP(this, &FNiagaraScratchPadScriptViewModel::OnScriptGraphChanged));
	EditScript->OnPropertyChanged().AddSP(this, &FNiagaraScratchPadScriptViewModel::OnScriptPropertyChanged);
}

void FNiagaraScratchPadScriptViewModel::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(EditScript);
}

UNiagaraScript* FNiagaraScratchPadScriptViewModel::GetOriginalScript() const
{
	return OriginalScript;
}

UNiagaraScript* FNiagaraScratchPadScriptViewModel::GetEditScript() const
{
	return EditScript;
}

FText FNiagaraScratchPadScriptViewModel::GetToolTip() const
{
	return FText::Format(LOCTEXT("ScratchPadScriptToolTipFormat", "Description: {0}{1}"),
		EditScript->Description.IsEmptyOrWhitespace() ? LOCTEXT("NoDescription", "(none)") : EditScript->Description,
		bHasPendingChanges ? LOCTEXT("HasPendingChangesStatus", "\n* Has pending changes to apply") : FText());
}

bool FNiagaraScratchPadScriptViewModel::GetIsPendingRename() const
{
	return bIsPendingRename;
}

void FNiagaraScratchPadScriptViewModel::SetIsPendingRename(bool bInIsPendingRename)
{
	bIsPendingRename = bInIsPendingRename;
}

void GetReferencingFunctionCallNodes(UNiagaraScript* Script, TArray<UNiagaraNodeFunctionCall*>& OutReferencingFunctionCallNodes)
{
	for (TObjectIterator<UNiagaraNodeFunctionCall> It; It; ++It)
	{
		UNiagaraNodeFunctionCall* FunctionCallNode = *It;
		if (FunctionCallNode->FunctionScript == Script)
		{
			OutReferencingFunctionCallNodes.Add(FunctionCallNode);
		}
	}
}

void FNiagaraScratchPadScriptViewModel::SetScriptName(FText InScriptName)
{
	FString NewName = ObjectTools::SanitizeObjectName(InScriptName.ToString());
	if (OriginalScript->GetName() != NewName)
	{
		FScopedTransaction RenameTransaction(LOCTEXT("RenameScriptTransaction", "Rename scratch pad script."));

		FName NewUniqueName = FNiagaraEditorUtilities::GetUniqueObjectName<UNiagaraScript>(OriginalScript->GetOuter(), *NewName);
		OriginalScript->Modify();
		OriginalScript->Rename(*NewUniqueName.ToString(), nullptr, REN_DontCreateRedirectors);

		TArray<UNiagaraNodeFunctionCall*> ReferencingFunctionCallNodes;
		GetReferencingFunctionCallNodes(OriginalScript, ReferencingFunctionCallNodes);
		for(UNiagaraNodeFunctionCall* ReferencingFunctionCallNode : ReferencingFunctionCallNodes)
		{
			ReferencingFunctionCallNode->Modify();
			FString OldFunctionName = ReferencingFunctionCallNode->GetFunctionName();
			ReferencingFunctionCallNode->SuggestName(FString());
			const FString NewFunctionName = ReferencingFunctionCallNode->GetFunctionName();
			UNiagaraSystem* System = ReferencingFunctionCallNode->GetTypedOuter<UNiagaraSystem>();
			UNiagaraEmitter* Emitter = ReferencingFunctionCallNode->GetTypedOuter<UNiagaraEmitter>();
			if (System != nullptr)
			{
				FNiagaraStackGraphUtilities::RenameReferencingParameters(*System, Emitter, *ReferencingFunctionCallNode, OldFunctionName, NewFunctionName);
			}
		}

		OnRenamedDelegate.Broadcast();
	}
}

bool FNiagaraScratchPadScriptViewModel::GetIsPinned() const
{
	return bIsPinned;
}

void FNiagaraScratchPadScriptViewModel::SetIsPinned(bool bInIsPinned)
{
	if (bIsPinned != bInIsPinned)
	{
		bIsPinned = bInIsPinned;
		OnPinnedChangedDelegate.Broadcast();
	}
}

float FNiagaraScratchPadScriptViewModel::GetEditorHeight() const
{
	return EditorHeight;
}

void FNiagaraScratchPadScriptViewModel::SetEditorHeight(float InEditorHeight)
{
	EditorHeight = InEditorHeight;
}

bool FNiagaraScratchPadScriptViewModel::CanApplyChanges() const
{
	return bHasPendingChanges;
}

void FNiagaraScratchPadScriptViewModel::ApplyChanges()
{
	ResetLoaders(OriginalScript->GetOutermost()); // Make sure that we're not going to get invalid version number linkers into the package we are going into. 
	OriginalScript->GetOutermost()->LinkerCustomVersion.Empty();

	OriginalScript = (UNiagaraScript*)StaticDuplicateObject(EditScript, OriginalScript->GetOuter(), OriginalScript->GetFName(),
		RF_AllFlags,
		OriginalScript->GetClass());
	bHasPendingChanges = false;

	TArray<UNiagaraNodeFunctionCall*> FunctionCallNodesToRefresh;
	GetReferencingFunctionCallNodes(OriginalScript, FunctionCallNodesToRefresh);

	TArray<UNiagaraStackFunctionInputCollection*> InputCollectionsToRefresh;
	if (FunctionCallNodesToRefresh.Num())
	{
		for (TObjectIterator<UNiagaraStackFunctionInputCollection> It; It; ++It)
		{
			UNiagaraStackFunctionInputCollection* StackFunctionInputCollection = *It;
			if (StackFunctionInputCollection->IsFinalized() == false && FunctionCallNodesToRefresh.Contains(StackFunctionInputCollection->GetInputFunctionCallNode()))
			{
				InputCollectionsToRefresh.Add(StackFunctionInputCollection);
			}
		}
	}

	for (UNiagaraNodeFunctionCall* FunctionCallNodeToRefresh : FunctionCallNodesToRefresh)
	{
		FunctionCallNodeToRefresh->RefreshFromExternalChanges();
		FunctionCallNodeToRefresh->MarkNodeRequiresSynchronization(TEXT("ScratchPadChangesApplied"), true);
	}

	for (UNiagaraStackFunctionInputCollection* InputCollectionToRefresh : InputCollectionsToRefresh)
	{
		InputCollectionToRefresh->RefreshChildren();
	}
}

FNiagaraScratchPadScriptViewModel::FOnRenamed& FNiagaraScratchPadScriptViewModel::OnRenamed()
{
	return OnRenamedDelegate;
}

FNiagaraScratchPadScriptViewModel::FOnPinnedChanged& FNiagaraScratchPadScriptViewModel::OnPinnedChanged()
{
	return OnPinnedChangedDelegate;
}

FText FNiagaraScratchPadScriptViewModel::GetDisplayNameInternal() const
{
	return FText::Format(LOCTEXT("DisplayNameFormat", "{0}{1}"), FText::FromString(OriginalScript->GetName()), bHasPendingChanges ? LOCTEXT("HasPendingChanges", "*") : FText());
}

void FNiagaraScratchPadScriptViewModel::OnScriptGraphChanged(const FEdGraphEditAction &Action)
{
	bHasPendingChanges = true;
}

void FNiagaraScratchPadScriptViewModel::OnScriptPropertyChanged(FPropertyChangedEvent& PropertyChangedEvent)
{
	bHasPendingChanges = true;
}

#undef LOCTEXT_NAMESPACE