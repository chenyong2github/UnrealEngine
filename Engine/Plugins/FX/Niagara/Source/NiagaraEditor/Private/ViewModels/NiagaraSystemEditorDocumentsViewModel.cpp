// Copyright Epic Games, Inc. All Rights Reserved.

#include "ViewModels/NiagaraSystemEditorDocumentsViewModel.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/NiagaraScriptGraphViewModel.h"
#include "ViewModels/NiagaraParameterPanelViewModel.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackFunctionInput.h"
#include "Toolkits/SystemToolkitModes/NiagaraSystemToolkitModeBase.h"
#include "NiagaraSystem.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraObjectSelection.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraEditorModule.h"
#include "NiagaraClipboard.h"
#include "Toolkits/NiagaraSystemToolkit.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraScriptFactoryNew.h"
#include "NiagaraGraph.h"

#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/PackageName.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Misc/MessageDialog.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/PlatformApplicationMisc.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"
#include "NiagaraScriptSource.h"
#include "Widgets/NiagaraScratchScriptEditor.h"
#include "Toolkits/NiagaraSystemToolkit.h"

#define LOCTEXT_NAMESPACE "NiagaraScratchPadViewModel"

void UNiagaraSystemEditorDocumentsViewModel::Initialize(TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModelWeak = InSystemViewModel;
}

void UNiagaraSystemEditorDocumentsViewModel::Finalize()
{
	ActiveDocumentTabScriptViewModel.Reset();
}

TArray<UNiagaraGraph*> UNiagaraSystemEditorDocumentsViewModel::GetEditableGraphsForActiveDocument()
{
	if (ActiveDocumentTabScriptViewModel.IsValid())
		return ActiveDocumentTabScriptViewModel->GetEditableGraphs();
	else
		return TArray<UNiagaraGraph*>();
}

void UNiagaraSystemEditorDocumentsViewModel::OpenChildScript(UEdGraph* InGraph)
{
	TArray< TSharedPtr<SDockTab> > Results;
	if (!FindOpenTabsContainingDocument(InGraph, Results))
	{
		OpenDocument(InGraph, FDocumentTracker::EOpenDocumentCause::ForceOpenNewDocument);
	}
	else if (Results.Num() > 0)
	{
		TabManager->DrawAttention(Results[0].ToSharedRef());
	}
}

void UNiagaraSystemEditorDocumentsViewModel::CloseChildScript(UEdGraph* InGraph)
{
	CloseDocumentTab(InGraph);
}


TSharedRef<FNiagaraSystemViewModel> UNiagaraSystemEditorDocumentsViewModel::GetSystemViewModel()
{
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel = SystemViewModelWeak.Pin();
	checkf(SystemViewModel.IsValid(), TEXT("SystemViewModel destroyed before system editor document view model."));
	return SystemViewModel.ToSharedRef();
}

void UNiagaraSystemEditorDocumentsViewModel::InitializePreTabManager(TSharedPtr<FNiagaraSystemToolkit> InToolkit)
{
	DocumentManager = MakeShareable(new FDocumentTracker);
	DocumentManager->Initialize(InToolkit);


	// Register the document factories
	{

		TSharedRef<FDocumentTabFactory> GraphEditorFactory = MakeShareable(new FGraphEditorSummoner(InToolkit,
			FGraphEditorSummoner::FOnCreateGraphEditorWidget::CreateUObject(this, &UNiagaraSystemEditorDocumentsViewModel::CreateGraphEditorWidget)
		));

		// Also store off a reference to the grapheditor factory so we can find all the tabs spawned by it later.
		GraphEditorTabFactoryPtr = GraphEditorFactory;
		DocumentManager->RegisterDocumentFactory(GraphEditorFactory);
	}
}

void UNiagaraSystemEditorDocumentsViewModel::InitializePostTabManager(TSharedPtr<FNiagaraSystemToolkit> InToolkit)
{
	TabManager = InToolkit->GetTabManager();
	DocumentManager->SetTabManager(TabManager.ToSharedRef());
}


TSharedRef<SNiagaraScratchPadScriptEditor> UNiagaraSystemEditorDocumentsViewModel::CreateGraphEditorWidget(TSharedRef<FTabInfo> InTabInfo, UEdGraph* InGraph)
{
	TSharedRef<FNiagaraScratchPadScriptViewModel> GraphViewModel = MakeShared<FNiagaraScratchPadScriptViewModel>(false);

	for (TSharedRef<FNiagaraScratchPadScriptViewModel> ScratchPadViewModel : GetSystemViewModel()->GetScriptScratchPadViewModel()->GetScriptViewModels())
	{
		if (ScratchPadViewModel->GetGraphViewModel()->GetGraph() == InGraph)
		{
			GraphViewModel = ScratchPadViewModel;
			break;
		}
	}

	TSharedRef<SNiagaraScratchPadScriptEditor> Editor = SNew(SNiagaraScratchPadScriptEditor, GraphViewModel);
	return Editor;
}



TSharedPtr<SDockTab> UNiagaraSystemEditorDocumentsViewModel::OpenDocument(const UObject* DocumentID, FDocumentTracker::EOpenDocumentCause Cause)
{
	TSharedRef<FTabPayload_UObject> Payload = FTabPayload_UObject::Make(DocumentID);
	return DocumentManager->OpenDocument(Payload, Cause);
}

void UNiagaraSystemEditorDocumentsViewModel::NavigateTab(FDocumentTracker::EOpenDocumentCause InCause)
{
	OpenDocument(nullptr, InCause);
}

void UNiagaraSystemEditorDocumentsViewModel::CloseDocumentTab(const UObject* DocumentID)
{
	TSharedRef<FTabPayload_UObject> Payload = FTabPayload_UObject::Make(DocumentID);
	DocumentManager->CloseTab(Payload);
	DocumentManager->CleanInvalidTabs();
}

// Finds any open tabs containing the specified document and adds them to the specified array; returns true if at least one is found
bool UNiagaraSystemEditorDocumentsViewModel::FindOpenTabsContainingDocument(const UObject* DocumentID, /*inout*/ TArray< TSharedPtr<SDockTab> >& Results)
{
	int32 StartingCount = Results.Num();

	TSharedRef<FTabPayload_UObject> Payload = FTabPayload_UObject::Make(DocumentID);

	DocumentManager->FindMatchingTabs(Payload, /*inout*/ Results);

	// Did we add anything new?
	return (StartingCount != Results.Num());
}

void UNiagaraSystemEditorDocumentsViewModel::SetActiveDocumentTab(TSharedPtr<SDockTab> Tab)
{
	ActiveDocumentTab = Tab;
	ActiveDocumentTabScriptViewModel = GetActiveScratchPadViewModelIfSet();

	ActiveDocChangedDelegate.Broadcast(Tab);


	// We need to update the parameter panel view model with new parameters potentially
	if (GetSystemViewModel()->GetParameterPanelViewModel())
		GetSystemViewModel()->GetParameterPanelViewModel()->RefreshNextTick();
}

TSharedPtr<FNiagaraScratchPadScriptViewModel> UNiagaraSystemEditorDocumentsViewModel::GetScratchPadViewModelFromGraph(FNiagaraSystemViewModel* InSysViewModel, UEdGraph* InTargetGraph)
{
	if (InSysViewModel && InTargetGraph)
	{
		UNiagaraScript* Script = InTargetGraph->GetTypedOuter<UNiagaraScript>();
		UNiagaraScratchPadViewModel* ScratchViewModel = InSysViewModel->GetScriptScratchPadViewModel();
		if (ScratchViewModel && Script)
		{
			for (TSharedRef<FNiagaraScratchPadScriptViewModel> ScratchScriptViewModel : ScratchViewModel->GetScriptViewModels())
			{
				UNiagaraScript* EditScript = ScratchScriptViewModel->GetGraphViewModel()->GetScriptSource()->GetTypedOuter<UNiagaraScript>();
				if (EditScript == Script)
				{
					return ScratchScriptViewModel;
				}
				else if (ScratchScriptViewModel->GetOriginalScript() == Script)
				{
					return ScratchScriptViewModel;
				}
			}
		}
	}
	return nullptr;
}


TSharedPtr<FNiagaraScratchPadScriptViewModel> UNiagaraSystemEditorDocumentsViewModel::GetActiveScratchPadViewModelIfSet()
{
	TSharedPtr<SDockTab> Tab = ActiveDocumentTab.Pin();
	if (Tab.IsValid() && Tab->GetLayoutIdentifier().TabType == TEXT("Document"))
	{
		TSharedRef<SNiagaraScratchPadScriptEditor> GraphEditor = StaticCastSharedRef<SNiagaraScratchPadScriptEditor>(Tab->GetContent());
		if (GraphEditor->GetGraphEditor())
		{
			TSharedPtr<FNiagaraSystemViewModel> SystemVM = SystemViewModelWeak.Pin();
			return GetScratchPadViewModelFromGraph(SystemVM.Get(), GraphEditor->GetGraphEditor()->GetCurrentGraph());
		}
	}

	return nullptr;
}


#undef LOCTEXT_NAMESPACE