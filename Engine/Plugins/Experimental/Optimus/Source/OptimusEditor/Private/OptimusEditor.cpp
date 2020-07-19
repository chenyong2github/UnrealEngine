// Copyright Epic Games, Inc. All Rights Reserved.

#include "OptimusEditor.h"

#include "OptimusDeformer.h"
#include "OptimusActionStack.h"

#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphNode.h"
#include "OptimusEditorGraphSchema.h"
#include "OptimusEditorCommands.h"
#include "OptimusEditorViewport.h"
#include "OptimusNode.h"
#include "OptimusNodeGraph.h"
#include "SOptimusNodePalette.h"

#include "Engine/SkeletalMesh.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "AdvancedPreviewSceneModule.h"
#include "ScopedTransaction.h"


#define LOCTEXT_NAMESPACE "OptimusEditor"

const FName OptimusEditorAppName(TEXT("OptimusEditorApp"));

const FName FOptimusEditor::PreviewTabId(TEXT("OptimusEditor_Preview"));
const FName FOptimusEditor::PaletteTabId(TEXT("OptimusEditor_Palette"));
const FName FOptimusEditor::GraphAreaTabId(TEXT("OptimusEditor_GraphArea"));
const FName FOptimusEditor::NodeDetailsTabId(TEXT("OptimusEditor_NodeDetails"));
const FName FOptimusEditor::PreviewDetailsTabId(TEXT("OptimusEditor_PreviewDetails"));
const FName FOptimusEditor::OutputTabId(TEXT("OptimusEditor_Output"));


FOptimusEditor::FOptimusEditor()
{

}


FOptimusEditor::~FOptimusEditor()
{

}


void FOptimusEditor::Construct(
	const EToolkitMode::Type InMode,
	const TSharedPtr< class IToolkitHost >& InToolkitHost,
	UOptimusDeformer* InDeformerObject)
{
	DeformerObject = InDeformerObject;

	// Construct a new graph with a default name
	// FIXME: Don't use FBlueprintEditorUtils, since we don't care about the name.
	DeformerGraph = CastChecked<UOptimusEditorGraph>(FBlueprintEditorUtils::CreateNewGraph(
		InDeformerObject, NAME_None,
		UOptimusEditorGraph::StaticClass(),
		UOptimusEditorGraphSchema::StaticClass()));

	BindCommands();
	RegisterToolbar();

	CreateWidgets();

	TSharedRef<FTabManager::FLayout> Layout = CreatePaneLayout();

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;

	// This call relies on virtual functions, so cannot be called from the constructor, hence
	// the dual-construction style.
	InitAssetEditor(InMode, InToolkitHost, OptimusEditorAppName, Layout,
		bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, { InDeformerObject });

	DeformerGraph->InitFromNodeGraph(DeformerObject->GetGraphs()[0]);

	// Ensure that the action stack creates undoable transactions when actions are run.
	DeformerObject->GetActionStack()->SetTransactionScopeFunctions(
		[](UObject* InTransactObject, const FString& Title)->int32 {
			int32 TransactionId = INDEX_NONE;
			if (GEditor && GEditor->Trans)
			{
				InTransactObject->SetFlags(RF_Transactional);
				TransactionId = GEditor->BeginTransaction(TEXT(""), FText::FromString(Title), InTransactObject);
				InTransactObject->Modify();
			}
			return TransactionId;
		},
		[](int32 InTransactionId) {
			if (GEditor && GEditor->Trans && InTransactionId >= 0)
			{
				// For reasons I cannot fathom, EndTransaction returns the active index upon
				// entry, rather than the active index on exit. Which makes it one higher than
				// the index we got from BeginTransaction ¯\_(ツ)_/¯
				int32 TransactionId = GEditor->EndTransaction();
				check(InTransactionId == (TransactionId - 1));
			}
		}
		);


	if (DeformerObject->Mesh)
	{
		EditorViewportWidget->SetPreviewAsset(DeformerObject->Mesh);
	}
}


FName FOptimusEditor::GetToolkitFName() const
{
	return FName("OptimusEditor");
}


FText FOptimusEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Optimus Deformer Editor");
}


FString FOptimusEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Optimus Deformer Editor ").ToString();
}


FLinearColor FOptimusEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.2f, 0.2f, 0.6f, 0.5f);
}



void FOptimusEditor::SelectAllNodes()
{
	GraphEditorWidget->SelectAllNodes();
}


bool FOptimusEditor::CanSelectAllNodes() const
{
	return GraphEditorWidget.IsValid();
}


void FOptimusEditor::DeleteSelectedNodes()
{
	TArray<UOptimusNode*> NodesToDelete;
	for (UObject* Object : GraphEditorWidget->GetSelectedNodes())
	{
		UOptimusEditorGraphNode* GraphNode = Cast<UOptimusEditorGraphNode>(Object);
		if (GraphNode && GraphNode->CanUserDeleteNode())
		{
			NodesToDelete.Add(GraphNode->ModelNode);
		}
	}

	if (NodesToDelete.Num() == 0)
	{
		return;
	}

	UOptimusEditorGraph* Graph = Cast<UOptimusEditorGraph>(GraphEditorWidget->GetCurrentGraph());
	if (Graph) 
	{
		Graph->GetModelGraph()->RemoveNodes(NodesToDelete);
	}

	GraphEditorWidget->ClearSelectionSet();
	GraphEditorWidget->NotifyGraphChanged();
}


bool FOptimusEditor::CanDeleteSelectedNodes() const
{
	for (UObject* Object : GraphEditorWidget->GetSelectedNodes())
	{
		UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Object);
		if (GraphNode && GraphNode->CanUserDeleteNode())
		{
			return true;
		}
	}

	return false;
}


void FOptimusEditor::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;

	for (UObject* Object : NewSelection)
	{
		if (UOptimusEditorGraphNode* GraphNode = Cast<UOptimusEditorGraphNode>(Object))
		{
			SelectedObjects.Add(GraphNode->ModelNode);
		}
		else
		{
			SelectedObjects.Add(Object);
		}
	}

	if (SelectedObjects.Num() == 0)
	{
		// If nothing was selected, default to the deformer object.
		SelectedObjects.Add(DeformerObject);
	}


	NodeDetailsWidget->SetObjects(SelectedObjects, /*bForceRefresh=*/true);

	// Bring the node details tab into the open.
	GetTabManager()->TryInvokeTab(NodeDetailsTabId);
}


void FOptimusEditor::OnNodeDoubleClicked(class UEdGraphNode* Node)
{

}


void FOptimusEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{

}


bool FOptimusEditor::OnVerifyNodeTextCommit(const FText& NewText, UEdGraphNode* NodeBeingChanged, FText& OutErrorMessage)
{
	return false;
}


FReply FOptimusEditor::OnSpawnGraphNodeByShortcut(FInputChord InChord, const FVector2D& InPosition, UEdGraph* InGraph)
{
	return FReply::Handled();
}


void FOptimusEditor::RegisterToolbar()
{
	const FName MenuName = GetToolMenuToolbarName();
	if (UToolMenus::Get()->IsMenuRegistered(MenuName))
	{
		return;
	}

	UToolMenu* ToolBar = UToolMenus::Get()->RegisterMenu(MenuName, "AssetEditor.DefaultToolBar", EMultiBoxType::ToolBar);

	FToolMenuInsert InsertAfterAssetSection("Asset", EToolMenuInsertType::After);
	{
		FToolMenuSection& Section = ToolBar->AddSection("Apply", TAttribute<FText>(), InsertAfterAssetSection);
		Section.AddEntry(FToolMenuEntry::InitToolBarButton(FOptimusEditorCommands::Get().Apply));
	}

}


void FOptimusEditor::BindCommands()
{
	const FOptimusEditorCommands& Commands = FOptimusEditorCommands::Get();

	// FIXME: Bind commands from FOptimusEditorCommands
#if 0
	ToolkitCommands->MapAction(
		Commands.Apply,
		FExecuteAction::CreateSP(this, &FOptimusEditor::OnApply),
		FCanExecuteAction::CreateSP(this, &FOptimusEditor::OnApplyEnabled));
#endif
}


TSharedRef<SDockTab> FOptimusEditor::SpawnTab_Preview(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("ViewportTabTitle", "Viewport"))
		[
			EditorViewportWidget.ToSharedRef()
		];

	EditorViewportWidget->SetOwnerTab(SpawnedTab);

	return SpawnedTab;
}


TSharedRef<SDockTab> FOptimusEditor::SpawnTab_Palette(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("Kismet.Tabs.Palette"))
		.Label(LOCTEXT("MaterialPaletteTitle", "Palette"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("MaterialPalette")))
			[
				NodePaletteWidget.ToSharedRef()
			]
		];
}


TSharedRef<SDockTab> FOptimusEditor::SpawnTab_GraphArea(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Label(LOCTEXT("GraphCanvasTitle", "Graph"))
		[
			GraphEditorWidget.ToSharedRef()
		];
}


TSharedRef<SDockTab> FOptimusEditor::SpawnTab_NodeDetails(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("NodeSettings", "Node Settings"))
		[
			NodeDetailsWidget.ToSharedRef()
		];
}


TSharedRef<SDockTab> FOptimusEditor::SpawnTab_PreviewDetails(const FSpawnTabArgs& Args)
{
	TSharedRef<SWidget> Widget = SNullWidget::NullWidget;

	if (EditorViewportWidget.IsValid())
	{
		FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
		Widget = AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(EditorViewportWidget->GetAdvancedPreviewScene());
	}

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		[
			SNew(SBox)
			[
				Widget
			]
		];

	return SpawnedTab;
}


TSharedRef<SDockTab> FOptimusEditor::SpawnTab_Output(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("Kismet.Tabs.CompilerResults"))
		.Label(LOCTEXT("CompilerOutput", "Compiler Output"))
		[
			SNullWidget::NullWidget // OutputTabId().ToSharedRef()
		];
}


TSharedRef<FTabManager::FLayout> FOptimusEditor::CreatePaneLayout() const
{
	// The default layout looks like so:
	// 
	// +-----------------------------------------+
	// |                Toolbar                  |
	// +-----+---------------------------+-------+
	// |     |                           |       |
	// | Pre |                           | Deets |
	// |     |                           |       |
	// +-----+          Graph            |       |
	// |     |                           |       |
	// | Pal +---------------------------+       |
	// |     |          Output           |       |
	// +-----+---------------------------+-------+
	//
	// Pre = 3D Preview 
	// Pal = Node Palette
	// Deets = Details panel

	TSharedRef<FTabManager::FLayout> Layout = FTabManager::NewLayout("Standalone_OptimusEditor_Layout_v01")
		->AddArea(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split(							// - Toolbar
				FTabManager::NewStack()->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split(							// - Main work area
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(0.9f)
				->Split(						// -- Preview + palette
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.2f)
					->Split(					// --- Preview widget
						FTabManager::NewStack()
						->SetHideTabWell(true)
						->AddTab(PreviewTabId, ETabState::OpenedTab)
					)
					->Split(					// --- Node palette
						FTabManager::NewStack()
						->SetHideTabWell(true)
						->AddTab(PaletteTabId, ETabState::OpenedTab)
					)
				)
				->Split(						// -- Graph + output
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.8f)
					->Split(					// --- Graph
						FTabManager::NewStack()->SetSizeCoefficient(0.8f)
						->SetHideTabWell(true)
						->AddTab(GraphAreaTabId, ETabState::OpenedTab)
					)
					->Split(					// --- Output
						FTabManager::NewStack()->SetSizeCoefficient(0.2f)
						->AddTab(OutputTabId, ETabState::OpenedTab)
					)
				)
				->Split(						// -- Details
					FTabManager::NewStack()->SetSizeCoefficient(0.2f)
					->AddTab(NodeDetailsTabId, ETabState::OpenedTab)
					->AddTab(PreviewDetailsTabId, ETabState::OpenedTab)
					->SetForegroundTab(NodeDetailsTabId)
				)
			)
		);

	return Layout;
}

void FOptimusEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_OptimusEditor", "OptimusEditor Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(PreviewTabId, FOnSpawnTab::CreateSP(this, &FOptimusEditor::SpawnTab_Preview))
		.SetDisplayName(LOCTEXT("ViewportTab", "Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(PaletteTabId, FOnSpawnTab::CreateSP(this, &FOptimusEditor::SpawnTab_Palette))
		.SetDisplayName(LOCTEXT("PaletteTab", "Palette"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.Palette"));

	InTabManager->RegisterTabSpawner(GraphAreaTabId, FOnSpawnTab::CreateSP(this, &FOptimusEditor::SpawnTab_GraphArea))
		.SetDisplayName(LOCTEXT("GraphAreaTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(NodeDetailsTabId, FOnSpawnTab::CreateSP(this, &FOptimusEditor::SpawnTab_NodeDetails))
		.SetDisplayName(LOCTEXT("NodeSettingsTab", "Node Settings"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(PreviewDetailsTabId, FOnSpawnTab::CreateSP(this, &FOptimusEditor::SpawnTab_PreviewDetails))
		.SetDisplayName(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(OutputTabId, FOnSpawnTab::CreateSP(this, &FOptimusEditor::SpawnTab_Output))
		.SetDisplayName(LOCTEXT("OutputTab", "Output"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.StatsViewer"));
}


void FOptimusEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(PreviewTabId);
	InTabManager->UnregisterTabSpawner(PaletteTabId);
	InTabManager->UnregisterTabSpawner(GraphAreaTabId);
	InTabManager->UnregisterTabSpawner(NodeDetailsTabId);
	InTabManager->UnregisterTabSpawner(PreviewDetailsTabId);
	InTabManager->UnregisterTabSpawner(OutputTabId);
}


void FOptimusEditor::CreateWidgets()
{
	// -- The preview viewport
	EditorViewportWidget = SNew(SOptimusEditorViewport, SharedThis(this));

	// -- The node palette
	NodePaletteWidget = SNew(SOptimusNodePalette, SharedThis(this));

	// -- Graph editor
	GraphEditorWidget = CreateGraphEditorWidget();
	GraphEditorWidget->SetViewLocation(FVector2D::ZeroVector, 1);

	// -- The property details panel
	// FIXME: Preview prop panel should be separate.
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	const FDetailsViewArgs DetailsViewArgs(false, false, true, FDetailsViewArgs::HideNameArea, true, this);
	NodeDetailsWidget = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	// TODO: See FMaterialEditor::CreateInternalWidgets for prop layout customization.


}


TSharedRef<SGraphEditor> FOptimusEditor::CreateGraphEditorWidget()
{
	GraphEditorCommands = MakeShareable(new FUICommandList);
	{
		// Editing commands
		GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
			FExecuteAction::CreateSP(this, &FOptimusEditor::SelectAllNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanSelectAllNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &FOptimusEditor::DeleteSelectedNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanDeleteSelectedNodes)
		);

#if 0
		GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
			FExecuteAction::CreateSP(this, &FOptimusEditor::CopySelectedNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanCopyNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
			FExecuteAction::CreateSP(this, &FOptimusEditor::PasteNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanPasteNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
			FExecuteAction::CreateSP(this, &FOptimusEditor::CutSelectedNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanCutNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
			FExecuteAction::CreateSP(this, &FOptimusEditor::DuplicateNodes),
			FCanExecuteAction::CreateSP(this, &FOptimusEditor::CanDuplicateNodes)
		);

		// Graph Editor Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
			FExecuteAction::CreateSP(this, &FOptimusEditor::OnCreateComment)
		);
#endif

#if 0
		// Material specific commands
		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().UseCurrentTexture,
			FExecuteAction::CreateSP(this, &FOptimusEditor::OnUseCurrentTexture)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().ConvertObjects,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnConvertObjects)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().ConvertToTextureObjects,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnConvertTextures)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().ConvertToTextureSamples,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnConvertTextures)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().ConvertToConstant,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnConvertObjects)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().StopPreviewNode,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnPreviewNode)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().StartPreviewNode,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnPreviewNode)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().EnableRealtimePreviewNode,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnToggleRealtimePreview)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().DisableRealtimePreviewNode,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnToggleRealtimePreview)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().SelectDownstreamNodes,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnSelectDownstreamNodes)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().SelectUpstreamNodes,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnSelectUpstreamNodes)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().RemoveFromFavorites,
			FExecuteAction::CreateSP(this, &FMaterialEditor::RemoveSelectedExpressionFromFavorites)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().AddToFavorites,
			FExecuteAction::CreateSP(this, &FMaterialEditor::AddSelectedExpressionToFavorites)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().ForceRefreshPreviews,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnForceRefreshPreviews)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().CreateComponentMaskNode,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnCreateComponentMaskNode)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().GoToDocumentation,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnGoToDocumentation),
			FCanExecuteAction::CreateSP(this, &FMaterialEditor::CanGoToDocumentation)
		);

		GraphEditorCommands->MapAction(FMaterialEditorCommands::Get().PromoteToParameter,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnPromoteToParameter),
			FCanExecuteAction::CreateSP(this, &FMaterialEditor::OnCanPromoteToParameter)
		);
#endif

#if 0

		// Alignment Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesTop,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignTop)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesMiddle,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignMiddle)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesBottom,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignBottom)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesLeft,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignLeft)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesCenter,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignCenter)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesRight,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnAlignRight)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().StraightenConnections,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnStraightenConnections)
		);

		// Distribution Commands
		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesHorizontally,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnDistributeNodesH)
		);

		GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesVertically,
			FExecuteAction::CreateSP(this, &FMaterialEditor::OnDistributeNodesV)
		);
#endif
	}

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FOptimusEditor::OnSelectedNodesChanged);
	InEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &FOptimusEditor::OnNodeDoubleClicked);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FOptimusEditor::OnNodeTitleCommitted);
	InEvents.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &FOptimusEditor::OnVerifyNodeTextCommit);
	InEvents.OnSpawnNodeByShortcut = 
		SGraphEditor::FOnSpawnNodeByShortcut::CreateSP(
			this, &FOptimusEditor::OnSpawnGraphNodeByShortcut, 
			static_cast<UEdGraph *>(DeformerGraph));

	// Create the title bar widget
	//TSharedPtr<SWidget> TitleBarWidget = SNew(SMaterialEditorTitleBar)
	//	.TitleText(this, &FMaterialEditor::GetOriginalObjectName)
	//	.MaterialInfoList(&MaterialInfoList);

	return SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.IsEditable(true)
		// .TitleBar(TitleBarWidget)
		.Appearance(this, &FOptimusEditor::GetGraphAppearance)
		.GraphToEdit(DeformerGraph)
		.GraphEvents(InEvents)
		.ShowGraphStateOverlay(false);
}


FGraphAppearanceInfo FOptimusEditor::GetGraphAppearance() const
{
	FGraphAppearanceInfo Appearance;
	Appearance.CornerText = LOCTEXT("AppearanceCornerText_OptimusDeformer", "OPTIMUS DEFORMER");
	return Appearance;
}


#undef LOCTEXT_NAMESPACE
