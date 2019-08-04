// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "Framework/Commands/GenericCommands.h"
#include "ScopedTransaction.h"
#include "EdGraphUtilities.h"
#include "HAL/PlatformApplicationMisc.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraOverviewNode.h"
#include "NiagaraObjectSelection.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "NiagaraSystem.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewGraphViewModel"

FNiagaraOverviewGraphViewModel::FNiagaraOverviewGraphViewModel()
	: Commands(MakeShareable(new FUICommandList()))
	, NodeSelection(MakeShareable(new FNiagaraObjectSelection()))
	, bUpdatingSystemSelectionFromGraph(false)
	, bUpdatingGraphSelectionFromSystem(false)
{
}

FNiagaraOverviewGraphViewModel::~FNiagaraOverviewGraphViewModel()
{
}

void FNiagaraOverviewGraphViewModel::Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModel = InSystemViewModel;
	OverviewGraph = InSystemViewModel->GetEditorData().GetSystemOverviewGraph();

	SetupCommands();
	NodeSelection->OnSelectedObjectsChanged().AddSP(this, &FNiagaraOverviewGraphViewModel::GraphSelectionChanged);
	InSystemViewModel->GetSelectionViewModel()->OnSelectionChanged().AddSP(this, &FNiagaraOverviewGraphViewModel::SystemSelectionChanged);
}

FText FNiagaraOverviewGraphViewModel::GetDisplayName() const
{
	if (DisplayNameCache.IsSet() == false)
	{
		DisplayNameCache = GetDisplayNameInternal();
	}
	return DisplayNameCache.GetValue();
}

UEdGraph* FNiagaraOverviewGraphViewModel::GetGraph() const
{
	if (SystemViewModel.IsValid())
	{
		return SystemViewModel.Pin()->GetEditorData().GetSystemOverviewGraph();
	}
	return nullptr;
}

TSharedRef<FUICommandList> FNiagaraOverviewGraphViewModel::GetCommands()
{
	return Commands;
}

TSharedRef<FNiagaraObjectSelection> FNiagaraOverviewGraphViewModel::GetNodeSelection()
{
	return NodeSelection;
}

const FNiagaraGraphViewSettings& FNiagaraOverviewGraphViewModel::GetViewSettings() const
{
	return GetSystemViewModel()->GetEditorData().GetSystemOverviewGraphViewSettings();
}

void FNiagaraOverviewGraphViewModel::SetViewSettings(const FNiagaraGraphViewSettings& InOverviewGraphViewSettings)
{
	GetSystemViewModel()->GetEditorData().SetSystemOverviewGraphViewSettings(InOverviewGraphViewSettings);
}

FNiagaraOverviewGraphViewModel::FOnNodesPasted& FNiagaraOverviewGraphViewModel::OnNodesPasted()
{
	return OnNodesPastedDelegate;
}

TSharedRef<FNiagaraSystemViewModel> FNiagaraOverviewGraphViewModel::GetSystemViewModel()
{
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModelPinned = SystemViewModel.Pin();
	checkf(SystemViewModelPinned.IsValid(), TEXT("System view model destroyed before overview graph view model."));
	return SystemViewModelPinned.ToSharedRef();
}

const TSharedRef<FNiagaraSystemViewModel> FNiagaraOverviewGraphViewModel::GetSystemViewModel() const
{
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModelPinned = SystemViewModel.Pin();
	checkf(SystemViewModelPinned.IsValid(), TEXT("System view model destroyed before overview graph view model."));
	return SystemViewModelPinned.ToSharedRef();
}

void FNiagaraOverviewGraphViewModel::SetupCommands()
{
	Commands->MapAction(
		FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::SelectAllNodes));

	Commands->MapAction(
		FGenericCommands::Get().Delete,
		FExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::DeleteSelectedNodes),
		FCanExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::CanDeleteNodes));

	Commands->MapAction(
		FGenericCommands::Get().Copy,
		FExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::CopySelectedNodes),
		FCanExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::CanCopyNodes));

	Commands->MapAction(
		FGenericCommands::Get().Cut,
		FExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::CutSelectedNodes),
		FCanExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::CanCutNodes));

	Commands->MapAction(
		FGenericCommands::Get().Paste,
		FExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::PasteNodes),
		FCanExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::CanPasteNodes));

	Commands->MapAction(
		FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::DuplicateNodes),
		FCanExecuteAction::CreateRaw(this, &FNiagaraOverviewGraphViewModel::CanDuplicateNodes));
}

void FNiagaraOverviewGraphViewModel::SelectAllNodes()
{
	UEdGraph* Graph = GetGraph();
	if (Graph != nullptr)
	{
		TArray<UObject*> AllNodes;
		Graph->GetNodesOfClass<UObject>(AllNodes);
		TSet<UObject*> AllNodeSet;
		AllNodeSet.Append(AllNodes);
		NodeSelection->SetSelectedObjects(AllNodeSet);
	}
}

void FNiagaraOverviewGraphViewModel::DeleteSelectedNodes()
{
	UEdGraph* Graph = GetGraph();
	if (Graph != nullptr)
	{
		const FScopedTransaction Transaction(FGenericCommands::Get().Delete->GetDescription());
		Graph->Modify();

		TArray<UObject*> NodesToDelete = NodeSelection->GetSelectedObjects().Array();
		NodeSelection->ClearSelectedObjects();

		TSet<FGuid> EmitterGuidsToDelete;
		for (UObject* NodeToDelete : NodesToDelete)
		{
			UNiagaraOverviewNode* GraphNodeToDelete = Cast<UNiagaraOverviewNode>(NodeToDelete);
			if (GraphNodeToDelete != nullptr && GraphNodeToDelete->CanUserDeleteNode())
			{
				EmitterGuidsToDelete.Add(GraphNodeToDelete->GetEmitterHandleGuid());
			}
		}
		//we have checked SystemViewModel is valid as this is a requisite for Graph to not be null.
		SystemViewModel.Pin()->DeleteEmitters(EmitterGuidsToDelete);
	}
}

bool FNiagaraOverviewGraphViewModel::CanDeleteNodes() const
{
	UEdGraph* Graph = GetGraph();
	if (Graph != nullptr)
	{
		for (UObject* SelectedNode : NodeSelection->GetSelectedObjects())
		{
			UEdGraphNode* SelectedGraphNode = Cast<UEdGraphNode>(SelectedNode);
			if (SelectedGraphNode != nullptr && SelectedGraphNode->CanUserDeleteNode())
			{
				return true;
			}
		}
	}
	return false;
}

void FNiagaraOverviewGraphViewModel::CutSelectedNodes()
{
	// Collect nodes which can not be delete or duplicated so they can be reselected.
	TSet<UObject*> CanBeDuplicatedAndDeleted;
	TSet<UObject*> CanNotBeDuplicatedAndDeleted;
	for (UObject* SelectedNode : NodeSelection->GetSelectedObjects())
	{
		UEdGraphNode* SelectedGraphNode = Cast<UEdGraphNode>(SelectedNode);
		if (SelectedGraphNode != nullptr)
		{
			if (SelectedGraphNode->CanDuplicateNode() && SelectedGraphNode->CanUserDeleteNode())
			{
				CanBeDuplicatedAndDeleted.Add(SelectedNode);
			}
			else
			{
				CanNotBeDuplicatedAndDeleted.Add(SelectedNode);
			}
		}
	}

	// Select the nodes which can be copied and deleted, copy and delete them, and then restore the ones which couldn't be copied or deleted.
	NodeSelection->SetSelectedObjects(CanBeDuplicatedAndDeleted);
	CopySelectedNodes();
	DeleteSelectedNodes();
	NodeSelection->SetSelectedObjects(CanNotBeDuplicatedAndDeleted);
}

bool FNiagaraOverviewGraphViewModel::CanCutNodes() const
{
	return CanCopyNodes() && CanDeleteNodes();
}

void FNiagaraOverviewGraphViewModel::CopySelectedNodes()
{
	TSet<UObject*> NodesToCopy;
	for (UObject* SelectedNode : NodeSelection->GetSelectedObjects())
	{
		UEdGraphNode* SelectedGraphNode = Cast<UEdGraphNode>(SelectedNode);
		if (SelectedGraphNode != nullptr)
		{
			if (SelectedGraphNode->CanDuplicateNode())
			{
				SelectedGraphNode->PrepareForCopying();
				NodesToCopy.Add(SelectedNode);
			}
		}
	}

	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(NodesToCopy, ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FNiagaraOverviewGraphViewModel::CanCopyNodes() const
{
	UEdGraph* Graph = GetGraph();
	if (Graph != nullptr)
	{
		for (UObject* SelectedNode : NodeSelection->GetSelectedObjects())
		{
			UEdGraphNode* SelectedGraphNode = Cast<UEdGraphNode>(SelectedNode);
			if (SelectedGraphNode != nullptr && SelectedGraphNode->CanDuplicateNode())
			{
				return true;
			}
		}
	}
	return false;
}


void FNiagaraOverviewGraphViewModel::PasteNodes()
{
	UEdGraph* Graph = GetGraph();
	if (Graph != nullptr)
	{
		const FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());;
		Graph->Modify();

		NodeSelection->ClearSelectedObjects();

		// Grab the text to paste from the clipboard.
		FString TextToImport;
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);

		// Import the nodes
		TSet<UEdGraphNode*> PastedNodes;
		FEdGraphUtilities::ImportNodesFromText(Graph, TextToImport, PastedNodes);

// 		for (UEdGraphNode* PastedNode : PastedNodes) //@TODO System Overview: fix this, copy the impl from sequencer impl in system toolkit
// 		{
// 			PastedNode->CreateNewGuid();
// 			UNiagaraNode* Node = Cast<UNiagaraNode>(PastedNode);
// 			if (Node)
// 				Node->MarkNodeRequiresSynchronization(__FUNCTION__, false);
// 		}

		OnNodesPastedDelegate.Broadcast(PastedNodes);

		TSet<UObject*> PastedObjects;
		for (UEdGraphNode* PastedNode : PastedNodes)
		{
			PastedObjects.Add(PastedNode);
		}

		NodeSelection->SetSelectedObjects(PastedObjects);
		Graph->NotifyGraphChanged(); //@TODO System Overview: might not be necessary 
	}
}

bool FNiagaraOverviewGraphViewModel::CanPasteNodes() const
{
	UEdGraph* Graph = GetGraph();
	if (Graph == nullptr)
	{
		return false;
	}

	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	return FEdGraphUtilities::CanImportNodesFromText(Graph, ClipboardContent);
}

void FNiagaraOverviewGraphViewModel::DuplicateNodes()
{
	CopySelectedNodes();
	PasteNodes();
}

bool FNiagaraOverviewGraphViewModel::CanDuplicateNodes() const
{
	return CanCopyNodes();
}

FText FNiagaraOverviewGraphViewModel::GetDisplayNameInternal() const
{
	ensureMsgf(SystemViewModel.IsValid(), TEXT("SystemViewModel was not initialized before InitDisplayName!"));
	TSharedPtr<FNiagaraSystemViewModel> WeakSystemViewModel = SystemViewModel.Pin();
	ENiagaraSystemViewModelEditMode SystemEditMode = WeakSystemViewModel->GetEditMode();
	if (SystemEditMode == ENiagaraSystemViewModelEditMode::EmitterAsset)
	{
		TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EditedEmitterHandleViewModels = WeakSystemViewModel->GetEmitterHandleViewModels();
		if (ensureMsgf(EditedEmitterHandleViewModels.Num() > 0, TEXT("SystemViewModel did not have any EmitterHandleViewModels! Cannot get currently edited Emitter's Name.")))
		{
			return FText::FromName(WeakSystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterHandle()->GetName());
		}
	}
	else if (SystemEditMode == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		return FText::FromString(WeakSystemViewModel->GetSystem().GetName());
	}
	else
	{
		ensureMsgf(false, TEXT("Encountered unexpected SystemViewModel edit mode!"));
	}
	return LOCTEXT("NiagaraOverview_FallbackTitlebar", "Niagara");
}

void FNiagaraOverviewGraphViewModel::GraphSelectionChanged()
{
	if (bUpdatingGraphSelectionFromSystem == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingSystemSelectionFromGraph, true);

		bool bSystemIsSelected = false;
		TArray<FGuid> SelectedEmitterHandleGuids;
		for (UObject* SelectedNode : NodeSelection->GetSelectedObjects())
		{
			UNiagaraOverviewNode* OverviewNode = Cast<UNiagaraOverviewNode>(SelectedNode);
			if (OverviewNode != nullptr)
			{
				if (OverviewNode->GetEmitterHandleGuid().IsValid())
				{
					SelectedEmitterHandleGuids.Add(OverviewNode->GetEmitterHandleGuid());
				}
				else
				{
					bSystemIsSelected = true;
				}
			}
		}

		bool bClearCurrentSelection = FSlateApplication::Get().GetModifierKeys().IsControlDown() == false;
		GetSystemViewModel()->GetSelectionViewModel()->UpdateSelectionFromTopLevelObjects(bSystemIsSelected, SelectedEmitterHandleGuids, bClearCurrentSelection);
	}
}

void FNiagaraOverviewGraphViewModel::SystemSelectionChanged(UNiagaraSystemSelectionViewModel::ESelectionChangeSource SelectionChangeSource)
{
	if (bUpdatingSystemSelectionFromGraph == false && 
		SelectionChangeSource != UNiagaraSystemSelectionViewModel::ESelectionChangeSource::EntrySelection)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingGraphSelectionFromSystem, true);

		TArray<UObject*> SelectedNodes;
		TArray<UNiagaraOverviewNode*> OverviewNodes;
		OverviewGraph->GetNodesOfClass<UNiagaraOverviewNode>(OverviewNodes);
		for (UNiagaraOverviewNode* OverviewNode : OverviewNodes)
		{
			if (OverviewNode->GetEmitterHandleGuid().IsValid())
			{
				if (GetSystemViewModel()->GetSelectionViewModel()->GetSelectedEmitterHandleIds().Contains(OverviewNode->GetEmitterHandleGuid()))
				{
					SelectedNodes.Add(OverviewNode);
				}
			}
			else
			{
				if (GetSystemViewModel()->GetSelectionViewModel()->GetSystemIsSelected())
				{
					SelectedNodes.Add(OverviewNode);
				}
			}
		}

		NodeSelection->SetSelectedObjects(SelectedNodes);
	}
}

#undef LOCTEXT_NAMESPACE // NiagaraScriptGraphViewModel
