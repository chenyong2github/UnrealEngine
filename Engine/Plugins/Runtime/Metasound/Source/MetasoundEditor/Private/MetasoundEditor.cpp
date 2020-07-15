// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditor.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailsView.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Modules/ModuleManager.h"
#include "MetasoundEditorCommands.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphSchema.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "SMetasoundPalette.h"
#include "SNodePanel.h"
#include "Widgets/Docking/SDockTab.h"
#include "Components/AudioComponent.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"

const FName FMetasoundEditor::GraphCanvasTabId(TEXT("MetasoundEditor_GraphCanvas"));
const FName FMetasoundEditor::PropertiesTabId(TEXT("MetasoundEditor_Properties"));
const FName FMetasoundEditor::PaletteTabId(TEXT("MetasoundEditor_Palette"));

FMetasoundEditor::FMetasoundEditor()
	: Metasound(nullptr)
{
}

void FMetasoundEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MetasoundEditor", "Metasound Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(GraphCanvasTabId, FOnSpawnTab::CreateSP(this, &FMetasoundEditor::SpawnTab_GraphCanvas))
		.SetDisplayName(LOCTEXT("GraphCanvasTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FMetasoundEditor::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(PaletteTabId, FOnSpawnTab::CreateSP(this, &FMetasoundEditor::SpawnTab_Palette))
		.SetDisplayName(LOCTEXT("PaletteTab", "Palette"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.Palette"));
}

void FMetasoundEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(GraphCanvasTabId);
	InTabManager->UnregisterTabSpawner(PropertiesTabId);
	InTabManager->UnregisterTabSpawner(PaletteTabId);
}

FMetasoundEditor::~FMetasoundEditor()
{
	// Stop any playing sounds when the editor closes
	UAudioComponent* Component = GEditor->GetPreviewAudioComponent();
	if (Component && Component->IsPlaying())
	{
		Stop();
	}

	check(GEditor);
	GEditor->UnregisterForUndo(this);
}

void FMetasoundEditor::InitMetasoundEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit)
{
	Metasound = CastChecked<UMetasound>(ObjectToEdit);

	// Support undo/redo
	Metasound->SetFlags(RF_Transactional);
	
	if (!Metasound->GetGraph())
	{
		UMetasoundEditorGraph* Graph = NewObject<UMetasoundEditorGraph>();
		Graph->Metasound = Metasound;
		Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
		Metasound->SetGraph(Graph);
	}

	GEditor->RegisterForUndo(this);

	FGraphEditorCommands::Register();
	FMetasoundEditorCommands::Register();

	BindGraphCommands();

	CreateInternalWidgets();

	const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_MetasoundEditor_Layout_v1")
	->AddArea
	(
		FTabManager::NewPrimaryArea()
		->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.1f)
			->SetHideTabWell(true)
			->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
		)
		->Split(FTabManager::NewSplitter()
			->SetOrientation(Orient_Horizontal)
			->SetSizeCoefficient(0.9f)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.225f)
				->SetHideTabWell(true)
				->AddTab(PropertiesTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.65f)
				->SetHideTabWell(true)
				->AddTab(GraphCanvasTabId, ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.125f)
				->SetHideTabWell(true)
				->AddTab(PaletteTabId, ETabState::OpenedTab)
			)
		)
	);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, TEXT("MetasoundEditorApp"), StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectToEdit, false);

	ExtendToolbar();
	RegenerateMenusAndToolbars();
}

UMetasound* FMetasoundEditor::GetMetasound() const
{
	return Metasound;
}

void FMetasoundEditor::SetSelection(TArray<UObject*> SelectedObjects)
{
	if (MetasoundProperties.IsValid())
	{
		MetasoundProperties->SetObjects(SelectedObjects);
	}
}

bool FMetasoundEditor::GetBoundsForSelectedNodes(FSlateRect& Rect, float Padding)
{
	return MetasoundGraphEditor->GetBoundsForSelectedNodes(Rect, Padding);
}

int32 FMetasoundEditor::GetNumberOfSelectedNodes() const
{
	return MetasoundGraphEditor->GetSelectedNodes().Num();
}

FName FMetasoundEditor::GetToolkitFName() const
{
	return FName("MetasoundEditor");
}

FText FMetasoundEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Metasound Editor");
}

FString FMetasoundEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Metasound ").ToString();
}

FLinearColor FMetasoundEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

TSharedRef<SDockTab> FMetasoundEditor::SpawnTab_GraphCanvas(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == GraphCanvasTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("MetasoundGraphCanvasTitle", "Viewport"));

	if (MetasoundGraphEditor.IsValid())
	{
		SpawnedTab->SetContent(MetasoundGraphEditor.ToSharedRef());
	}

	return SpawnedTab;
}

TSharedRef<SDockTab> FMetasoundEditor::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PropertiesTabId);

	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("MetasoundDetailsTitle", "Details"))
		[
			MetasoundProperties.ToSharedRef()
		];
}

TSharedRef<SDockTab> FMetasoundEditor::SpawnTab_Palette(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PaletteTabId);

	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("Kismet.Tabs.Palette"))
		.Label(LOCTEXT("MetasoundPaletteTitle", "Palette"))
		[
			Palette.ToSharedRef()
		];
}

void FMetasoundEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Metasound);
}

void FMetasoundEditor::PostUndo(bool bSuccess)
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->ClearSelectionSet();
		MetasoundGraphEditor->NotifyGraphChanged();
		FSlateApplication::Get().DismissAllMenus();
	}

}

void FMetasoundEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
{
	if (MetasoundGraphEditor.IsValid() && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
	{
		MetasoundGraphEditor->NotifyGraphChanged();
	}
}

void FMetasoundEditor::CreateInternalWidgets()
{
	MetasoundGraphEditor = CreateGraphEditorWidget();

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.NotifyHook = this;

	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	MetasoundProperties = PropertyModule.CreateDetailView(Args);
	MetasoundProperties->SetObject(Metasound);

	Palette = SNew(SMetasoundPalette);
}

void FMetasoundEditor::ExtendToolbar()
{
	TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
	ToolbarExtender->AddToolBarExtension
	(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateLambda([](FToolBarBuilder& ToolbarBuilder)
		{
			ToolbarBuilder.BeginSection("Audition");
			{
				ToolbarBuilder.AddToolBarButton(FMetasoundEditorCommands::Get().Play);
				ToolbarBuilder.AddToolBarButton(FMetasoundEditorCommands::Get().Stop);
			}
			ToolbarBuilder.EndSection();
		})
	);

	AddToolbarExtender(ToolbarExtender);
}

void FMetasoundEditor::BindGraphCommands()
{
	const FMetasoundEditorCommands& Commands = FMetasoundEditorCommands::Get();

	ToolkitCommands->MapAction(
		Commands.Play,
		FExecuteAction::CreateSP(this, &FMetasoundEditor::Play));

	ToolkitCommands->MapAction(
		Commands.Stop,
		FExecuteAction::CreateSP(this, &FMetasoundEditor::Stop));

	ToolkitCommands->MapAction(
		Commands.TogglePlayback,
		FExecuteAction::CreateSP(this, &FMetasoundEditor::TogglePlayback));

	ToolkitCommands->MapAction(
		FGenericCommands::Get().Undo,
		FExecuteAction::CreateSP(this, &FMetasoundEditor::UndoGraphAction));

	ToolkitCommands->MapAction(
		FGenericCommands::Get().Redo,
		FExecuteAction::CreateSP(this, &FMetasoundEditor::RedoGraphAction));
}

void FMetasoundEditor::Play()
{
// TODO: Implement play

// 	GEditor->PlayPreviewSound(Metasound);

// 	MetasoundGraphEditor->RegisterActiveTimer(0.0f, 
// 		FWidgetActiveTimerDelegate::CreateLambda([](double InCurrentTime, float InDeltaTime)
// 		{
// 			UAudioComponent* PreviewComp = GEditor->GetPreviewAudioComponent();
// 			if (PreviewComp && PreviewComp->IsPlaying())
// 			{
// 				return EActiveTimerReturnType::Continue;
// 			}
// 			else
// 			{
// 				return EActiveTimerReturnType::Stop;
// 			}
// 		})
// 	);
}

void FMetasoundEditor::PlayNode()
{
	// already checked that only one node is selected
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		PlaySingleNode(CastChecked<UEdGraphNode>(*NodeIt));
	}
}

bool FMetasoundEditor::CanPlayNode() const
{
	return false;
}

void FMetasoundEditor::Stop()
{
	check(GEditor);
	GEditor->ResetPreviewAudioComponent();
}

void FMetasoundEditor::TogglePlayback()
{
	check(GEditor);

	UAudioComponent* Component = GEditor->GetPreviewAudioComponent();
	if (Component && Component->IsPlaying())
	{
		Stop();
	}
	else
	{
		Play();
	}
}

void FMetasoundEditor::PlaySingleNode(UEdGraphNode* Node)
{
	// TODO: Implement? Will we support single node playback?
}

void FMetasoundEditor::SyncInBrowser()
{
	TArray<UObject*> ObjectsToSync;

	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		// TODO: Implement sync to referenced Metasound if selected node is a reference to another metasound
	}

	if (!ObjectsToSync.Num())
	{
		ObjectsToSync.Add(Metasound);
	}

	check(GEditor);
	GEditor->SyncBrowserToObjects(ObjectsToSync);
}

void FMetasoundEditor::AddInput()
{
}

bool FMetasoundEditor::CanAddInput() const
{
	return GetSelectedNodes().Num() == 1;
}

void FMetasoundEditor::DeleteInput()
{
}

bool FMetasoundEditor::CanDeleteInput() const
{
	return true;
}

void FMetasoundEditor::OnCreateComment()
{
	Metasound->MarkPackageDirty();
}

TSharedRef<SGraphEditor> FMetasoundEditor::CreateGraphEditorWidget()
{
	if (!GraphEditorCommands.IsValid())
	{
		GraphEditorCommands = MakeShared<FUICommandList>();

		GraphEditorCommands->MapAction(FMetasoundEditorCommands::Get().BrowserSync,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::SyncInBrowser));

		GraphEditorCommands->MapAction(FMetasoundEditorCommands::Get().AddInput,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::AddInput),
			FCanExecuteAction::CreateSP(this, &FMetasoundEditor::CanAddInput));

		GraphEditorCommands->MapAction(FMetasoundEditorCommands::Get().DeleteInput,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::DeleteInput),
			FCanExecuteAction::CreateSP(this, &FMetasoundEditor::CanDeleteInput));

		// Graph Editor Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnCreateComment));

		// Editing commands
		GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::SelectAllNodes),
			FCanExecuteAction::CreateSP(this, &FMetasoundEditor::CanSelectAllNodes));

		GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::DeleteSelectedNodes),
			FCanExecuteAction::CreateSP(this, &FMetasoundEditor::CanDeleteNodes));

		GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::CopySelectedNodes),
			FCanExecuteAction::CreateSP(this, &FMetasoundEditor::CanCopyNodes));

		GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::CutSelectedNodes),
			FCanExecuteAction::CreateSP(this, &FMetasoundEditor::CanCutNodes));

		GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::PasteNodes),
			FCanExecuteAction::CreateSP(this, &FMetasoundEditor::CanPasteNodes));

		GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::DuplicateNodes),
			FCanExecuteAction::CreateSP(this, &FMetasoundEditor::CanDuplicateNodes));

		// Alignment Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesTop,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnAlignTop));

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesMiddle,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnAlignMiddle));

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesBottom,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnAlignBottom));

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesLeft,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnAlignLeft));

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesCenter,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnAlignCenter));

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesRight,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnAlignRight));

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().StraightenConnections,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnStraightenConnections));

		// Distribution Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesHorizontally,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnDistributeNodesH));

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesVertically,
			FExecuteAction::CreateSP(this, &FMetasoundEditor::OnDistributeNodesV));
	}

	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_Metasound", "Metasound");

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FMetasoundEditor::OnSelectedNodesChanged);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FMetasoundEditor::OnNodeTitleCommitted);
	InEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &FMetasoundEditor::PlaySingleNode);

	return SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.IsEditable(true)
		.Appearance(AppearanceInfo)
		.GraphToEdit(Metasound->GetGraph())
		.GraphEvents(InEvents)
		.AutoExpandActionMenu(true)
		.ShowGraphStateOverlay(false);
}

FGraphPanelSelectionSet FMetasoundEditor::GetSelectedNodes() const
{
	FGraphPanelSelectionSet CurrentSelection;
	if (MetasoundGraphEditor.IsValid())
	{
		CurrentSelection = MetasoundGraphEditor->GetSelectedNodes();
	}
	return CurrentSelection;
}

void FMetasoundEditor::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
	TArray<UObject*> Selection;

	if(NewSelection.Num())
	{
		for(TSet<UObject*>::TConstIterator SetIt(NewSelection); SetIt; ++SetIt)
		{
			if (Cast<UMetasoundEditorGraphNode>(*SetIt))
			{
				Selection.Add(GetMetasound());
			}
			else
			{
				Selection.Add(*SetIt);
			}
		}
	}
	else
	{
		Selection.Add(GetMetasound());
	}

	SetSelection(Selection);
}

void FMetasoundEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{
	if (NodeBeingChanged)
	{
		const FScopedTransaction Transaction(LOCTEXT("RenameNode", "Rename Node"));
		NodeBeingChanged->Modify();
		NodeBeingChanged->OnRenameNode(NewText.ToString());
	}
}

void FMetasoundEditor::SelectAllNodes()
{
	MetasoundGraphEditor->SelectAllNodes();
}

bool FMetasoundEditor::CanSelectAllNodes() const
{
	return true;
}

void FMetasoundEditor::DeleteSelectedNodes()
{
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "MetasoundEditorDeleteSelectedNode", "Delete Selected Metasound Node"));

	MetasoundGraphEditor->GetCurrentGraph()->Modify();

	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	MetasoundGraphEditor->ClearSelectionSet();

	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		UEdGraphNode* Node = CastChecked<UEdGraphNode>(*NodeIt);
		if (Node->CanUserDeleteNode())
		{
			if (UMetasoundEditorGraphNode* MetasoundGraphNode = Cast<UMetasoundEditorGraphNode>(Node))
			{
				// TODO: Implement delete selected here
			}
		}
	}
}

bool FMetasoundEditor::CanDeleteNodes() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	if (SelectedNodes.Num() == 1)
	{
		for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
		{
			if (Cast<UMetasoundEditorGraphNode>(*NodeIt))
			{
				// Return false if only root node is selected, as it can't be deleted
				return false;
			}
		}
	}

	return SelectedNodes.Num() > 0;
}

void FMetasoundEditor::DeleteSelectedDuplicatableNodes()
{
	// Cache off the old selection
	const FGraphPanelSelectionSet OldSelectedNodes = GetSelectedNodes();

	// Clear the selection and only select the nodes that can be duplicated
	FGraphPanelSelectionSet RemainingNodes;
	MetasoundGraphEditor->ClearSelectionSet();

	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(OldSelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
		if ((Node != nullptr) && Node->CanDuplicateNode())
		{
			MetasoundGraphEditor->SetNodeSelection(Node, true);
		}
		else
		{
			RemainingNodes.Add(Node);
		}
	}

	// Delete the duplicatable nodes
	DeleteSelectedNodes();

	// Reselect whatever's left from the original selection after the deletion
	MetasoundGraphEditor->ClearSelectionSet();

	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(RemainingNodes); SelectedIter; ++SelectedIter)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter))
		{
			MetasoundGraphEditor->SetNodeSelection(Node, true);
		}
	}
}

void FMetasoundEditor::CutSelectedNodes()
{
	CopySelectedNodes();
	// Cut should only delete nodes that can be duplicated
	DeleteSelectedDuplicatableNodes();
}

bool FMetasoundEditor::CanCutNodes() const
{
	return CanCopyNodes() && CanDeleteNodes();
}

void FMetasoundEditor::CopySelectedNodes()
{
	// Export the selected nodes and place the text on the clipboard
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	FString ExportedText;

	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		if(UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(*SelectedIter))
		{
			Node->PrepareForCopying();
		}
	}

	FEdGraphUtilities::ExportNodesToText(SelectedNodes, /*out*/ ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);

	// Make sure Metasound remains the owner of the copied nodes
	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		if (UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(*SelectedIter))
		{
			Node->PostCopyNode();
		}
	}
}

bool FMetasoundEditor::CanCopyNodes() const
{
	// If any of the nodes can be duplicated then we should allow copying
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
		if ((Node != nullptr) && Node->CanDuplicateNode())
		{
			return true;
		}
	}
	return false;
}

void FMetasoundEditor::PasteNodes()
{
	PasteNodesAtLocation(MetasoundGraphEditor->GetPasteLocation());
}

void FMetasoundEditor::PasteNodesAtLocation(const FVector2D& Location)
{
	UEdGraph* Graph = Metasound->GetGraph();
	if (!Graph)
	{
		return;
	}

	// Undo/Redo support
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "MetasoundEditorPaste", "Paste Metasound Node"));
	Graph->Modify();
	Metasound->Modify();

	// Clear the selection set (newly pasted stuff will be selected)
	MetasoundGraphEditor->ClearSelectionSet();

	// Grab the text to paste from the clipboard.
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	// Import the nodes
	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(Graph, TextToImport, /*out*/ PastedNodes);

	//Average position of nodes so we can move them while still maintaining relative distances to each other
	FVector2D AvgNodePosition(0.0f,0.0f);

	for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
	{
		UEdGraphNode* Node = *It;
		AvgNodePosition.X += Node->NodePosX;
		AvgNodePosition.Y += Node->NodePosY;
	}

	if (PastedNodes.Num() > 0)
	{
		float InvNumNodes = 1.0f/float(PastedNodes.Num());
		AvgNodePosition.X *= InvNumNodes;
		AvgNodePosition.Y *= InvNumNodes;
	}

	for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
	{
		UEdGraphNode* Node = *It;

		if (UMetasoundEditorGraphNode* SoundGraphNode = Cast<UMetasoundEditorGraphNode>(Node))
		{
			// TODO: Add newly referenced nodes to MS reference list
		}

		// Select the newly pasted stuff
		MetasoundGraphEditor->SetNodeSelection(Node, true);

		Node->NodePosX = (Node->NodePosX - AvgNodePosition.X) + Location.X ;
		Node->NodePosY = (Node->NodePosY - AvgNodePosition.Y) + Location.Y ;

		Node->SnapToGrid(SNodePanel::GetSnapGridSize());

		// Give new node a different Guid from the old one
		Node->CreateNewGuid();
	}

	// TODO: Compile metasound here???

	// Update UI
	MetasoundGraphEditor->NotifyGraphChanged();

	Metasound->PostEditChange();
	Metasound->MarkPackageDirty();
}

bool FMetasoundEditor::CanPasteNodes() const
{
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	// TODO: Implement
// 	const bool bCanPasteNodes = FEdGraphUtilities::CanImportNodesFromText(Metasound->MetasoundGraph, ClipboardContent);
// 	return bCanPasteNodes;

	return false;
}

void FMetasoundEditor::DuplicateNodes()
{
	// Copy and paste current selection
	CopySelectedNodes();
	PasteNodes();
}

bool FMetasoundEditor::CanDuplicateNodes() const
{
	return CanCopyNodes();
}

void FMetasoundEditor::UndoGraphAction()
{
	check(GEditor);
	GEditor->UndoTransaction();
}

void FMetasoundEditor::RedoGraphAction()
{
	// Clear selection, to avoid holding refs to nodes that go away
	MetasoundGraphEditor->ClearSelectionSet();

	check(GEditor);
	GEditor->RedoTransaction();
}

void FMetasoundEditor::OnAlignTop()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnAlignTop();
	}
}

void FMetasoundEditor::OnAlignMiddle()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnAlignMiddle();
	}
}

void FMetasoundEditor::OnAlignBottom()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnAlignBottom();
	}
}

void FMetasoundEditor::OnAlignLeft()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnAlignLeft();
	}
}

void FMetasoundEditor::OnAlignCenter()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnAlignCenter();
	}
}

void FMetasoundEditor::OnAlignRight()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnAlignRight();
	}
}

void FMetasoundEditor::OnStraightenConnections()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnStraightenConnections();
	}
}

void FMetasoundEditor::OnDistributeNodesH()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnDistributeNodesH();
	}
}

void FMetasoundEditor::OnDistributeNodesV()
{
	if (MetasoundGraphEditor.IsValid())
	{
		MetasoundGraphEditor->OnDistributeNodesV();
	}
}

#undef LOCTEXT_NAMESPACE
