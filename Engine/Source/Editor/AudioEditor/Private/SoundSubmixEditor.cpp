// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundSubmixEditor.h"

#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AudioEditorModule.h"
#include "EdGraph/EdGraph.h"
#include "Editor.h"
#include "GraphEditor.h"
#include "GraphEditAction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "Editor/PropertyEditor/Public/IDetailsView.h"
#include "Editor/WorkspaceMenuStructure/Public/WorkspaceMenuStructureModule.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Docking/TabManager.h"
#include "Factories/SoundSubmixFactory.h"
#include "IAssetTools.h"
#include "ScopedTransaction.h"
#include "Sound/SoundSubmix.h"
#include "SoundSubmixGraph/SoundSubmixGraph.h"
#include "SoundSubmixGraph/SoundSubmixGraphNode.h"
#include "SoundSubmixGraph/SoundSubmixGraphSchema.h"
#include "SSoundSubmixActionMenu.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/Package.h"
#include "Widgets/Docking/SDockTab.h"


#define LOCTEXT_NAMESPACE "SoundSubmixEditor"
DEFINE_LOG_CATEGORY_STATIC(LogSoundSubmixEditor, Log, All);

const FName FSoundSubmixEditor::GraphCanvasTabId(TEXT("SoundSubmixEditor_GraphCanvas"));
const FName FSoundSubmixEditor::PropertiesTabId(TEXT("SoundSubmixEditor_Properties"));


class SSoundSubmixGraphEditor : public SGraphEditor
{
private:
	TWeakPtr<FSoundSubmixEditor> SubmixEditor;

public:
	SSoundSubmixGraphEditor()
		: SGraphEditor()
		, SubmixEditor(nullptr)
	{
	}

	void Construct(const FArguments& InArgs, TSharedPtr<FSoundSubmixEditor> InEditor)
	{
		SubmixEditor = InEditor;
		SGraphEditor::Construct(InArgs);
	}

	void OnGraphChanged(const FEdGraphEditAction& InAction) override
	{
		if (SubmixEditor.IsValid())
		{
			TSharedPtr<FSoundSubmixEditor> PinnedEditor = SubmixEditor.Pin();
			if (InAction.Graph && InAction.Graph == PinnedEditor->GetGraph())
			{
				PinnedEditor->AddMissingEditableSubmixes();
			}
		}
		SGraphEditor::OnGraphChanged(InAction);
	}
};

void FSoundSubmixEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_SoundSubmixEditor", "Sound Submix Editor"));
	auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(GraphCanvasTabId, FOnSpawnTab::CreateSP(this, &FSoundSubmixEditor::SpawnTab_GraphCanvas))
		.SetDisplayName(LOCTEXT("GraphCanvasTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FSoundSubmixEditor::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("PropertiesTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));
}

void FSoundSubmixEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner( GraphCanvasTabId );
	InTabManager->UnregisterTabSpawner( PropertiesTabId );
}

void FSoundSubmixEditor::AddEditableSubmixChildren(USoundSubmix* RootSubmix)
{
	if (!RootSubmix)
	{
		return;
	}

	RootSubmix->SetFlags(RF_Transactional);
	for (USoundSubmix* Child : RootSubmix->ChildSubmixes)
	{
		if (Child)
		{
			Child->SoundSubmixGraph = RootSubmix->SoundSubmixGraph;
			AddEditingObject(Child);
			AddEditableSubmixChildren(Child);
		}
	}
}

void FSoundSubmixEditor::Init(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost >& InitToolkitHost, UObject* ObjectToEdit)
{
	USoundSubmix* SoundSubmix = CastChecked<USoundSubmix>(ObjectToEdit);

	while (SoundSubmix->ParentSubmix)
	{
		SoundSubmix = SoundSubmix->ParentSubmix;
	}

	GEditor->RegisterForUndo(this);

	ToolkitCommands->MapAction(
		FGenericCommands::Get().Undo,
		FExecuteAction::CreateSP(this, &FSoundSubmixEditor::UndoGraphAction));

	ToolkitCommands->MapAction(
		FGenericCommands::Get().Redo,
		FExecuteAction::CreateSP(this, &FSoundSubmixEditor::RedoGraphAction));

	USoundSubmixGraph* SoundSubmixGraph = CastChecked<USoundSubmixGraph>(FBlueprintEditorUtils::CreateNewGraph(SoundSubmix, NAME_None, USoundSubmixGraph::StaticClass(), USoundSubmixGraphSchema::StaticClass()));
	SoundSubmixGraph->SetRootSoundSubmix(SoundSubmix);

	SoundSubmix->SoundSubmixGraph = SoundSubmixGraph;
	SoundSubmixGraph->RebuildGraph();

	CreateInternalWidgets(SoundSubmix);

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_SoundSubmixEditor_Layout_v2")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
				->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			)
			->Split
			(
				FTabManager::NewSplitter()
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.2f)
					->AddTab(PropertiesTabId, ETabState::OpenedTab)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.8f)
					->AddTab(GraphCanvasTabId, ETabState::OpenedTab)
				)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, TEXT("SoundSubmixEditorApp"), StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, SoundSubmix);

	// Must be called after super class initialization
	AddEditableSubmixChildren(SoundSubmix);

	IAudioEditorModule* AudioEditorModule = &FModuleManager::LoadModuleChecked<IAudioEditorModule>("AudioEditor");
	AddMenuExtender(AudioEditorModule->GetSoundSubmixMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	AddToolbarExtender(AudioEditorModule->GetSoundSubmixToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	GraphEditor->SelectAllNodes();
	for (UObject* SelectedNode : GraphEditor->GetSelectedNodes())
	{
		USoundSubmixGraphNode* GraphNode = CastChecked<USoundSubmixGraphNode>(SelectedNode);
		if (GraphNode->SoundSubmix == ObjectToEdit)
		{
			GraphEditor->ClearSelectionSet();
			GraphEditor->SetNodeSelection(GraphNode, true);
			DetailsView->SetObject(ObjectToEdit);
			break;
		}
	}
}

FSoundSubmixEditor::~FSoundSubmixEditor()
{
	GEditor->UnregisterForUndo(this);
	DetailsView.Reset();
}

void FSoundSubmixEditor::AddReferencedObjects(FReferenceCollector& Collector)
{
	for (UObject* Obj : GetEditingObjects())
	{
		Collector.AddReferencedObject(Obj);
	}
}

TSharedRef<SDockTab> FSoundSubmixEditor::SpawnTab_GraphCanvas(const FSpawnTabArgs& Args)
{
	check( Args.GetTabId() == GraphCanvasTabId );

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("GraphCanvasTitle", "Graph"))
		[
			GraphEditor.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FSoundSubmixEditor::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check( Args.GetTabId() == PropertiesTabId );

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon( FEditorStyle::GetBrush("SoundSubmixEditor.Tabs.Properties") )
		.Label( LOCTEXT( "SoundSubmixPropertiesTitle", "Details" ) )
		[
			DetailsView.ToSharedRef()
		];
	
	return SpawnedTab;
}

FName FSoundSubmixEditor::GetToolkitFName() const
{
	return FName("SoundSubmixEditor");
}

FText FSoundSubmixEditor::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Sound Submix Editor");
}

FText FSoundSubmixEditor::GetToolkitToolTipText() const
{
	return GetToolTipTextForObject(GetEditingObjects()[0]);
}

FString FSoundSubmixEditor::GetWorldCentricTabPrefix() const
{
	return LOCTEXT( "WorldCentricTabPrefix", "Sound Submix " ).ToString();
}

FLinearColor FSoundSubmixEditor::GetWorldCentricTabColorScale() const
{
	return FLinearColor( 0.2f, 0.4f, 0.8f, 0.5f );
}

void FSoundSubmixEditor::CreateInternalWidgets(USoundSubmix* InSoundSubmix)
{
	GraphEditor = CreateGraphEditorWidget(InSoundSubmix);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	const FDetailsViewArgs DetailsViewArgs(false, false, true, FDetailsViewArgs::ObjectsUseNameArea, false);
	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(InSoundSubmix);
}

TSharedRef<SGraphEditor> FSoundSubmixEditor::CreateGraphEditorWidget(USoundSubmix* InSoundSubmix)
{
	if (!GraphEditorCommands.IsValid())
	{
		GraphEditorCommands = MakeShareable(new FUICommandList);

		// Editing commands
		GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
			FExecuteAction::CreateSP(this, &FSoundSubmixEditor::SelectAllNodes),
			FCanExecuteAction::CreateSP(this, &FSoundSubmixEditor::CanSelectAllNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &FSoundSubmixEditor::RemoveSelectedNodes),
			FCanExecuteAction::CreateSP(this, &FSoundSubmixEditor::CanRemoveNodes)
		);
	}

	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_SoundSubmix", "SOUND SUBMIX");

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FSoundSubmixEditor::OnSelectedNodesChanged);
	InEvents.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &FSoundSubmixEditor::OnCreateGraphActionMenu);

	return SNew(SSoundSubmixGraphEditor, SharedThis(this))
		.AdditionalCommands(GraphEditorCommands)
		.IsEditable(true)
		.Appearance(AppearanceInfo)
		.GraphToEdit(InSoundSubmix->SoundSubmixGraph)
		.GraphEvents(InEvents)
		.ShowGraphStateOverlay(false);
}

void FSoundSubmixEditor::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
	if(NewSelection.Num() > 0)
	{
		TArray<UObject*> Selection;
		for (UObject* Obj : NewSelection)
		{
			USoundSubmixGraphNode* GraphNode = CastChecked<USoundSubmixGraphNode>(Obj);
			Selection.Add(GraphNode->SoundSubmix);
		}
		DetailsView->SetObjects(Selection);
	}
	else
	{
		DetailsView->SetObject(GetEditingObjects()[0]);
	}
}

FActionMenuContent FSoundSubmixEditor::OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	TSharedRef<SSoundSubmixActionMenu> ActionMenu = 
		SNew(SSoundSubmixActionMenu)
		.GraphObj(InGraph)
		.NewNodePosition(InNodePosition)
		.DraggedFromPins(InDraggedPins)
		.AutoExpandActionMenu(bAutoExpand)
		.OnClosedCallback(InOnMenuClosed);

	return FActionMenuContent( ActionMenu, ActionMenu );
}

void FSoundSubmixEditor::SelectAllNodes()
{
	GraphEditor->SelectAllNodes();
}

void FSoundSubmixEditor::SelectSubmixes(TSet<USoundSubmix*>& InSubmixes)
{
	TArray<UObject*> ObjectsToSelect;

	GraphEditor->SelectAllNodes();
	const TSet<UObject*> SelectedNodes = GraphEditor->GetSelectedNodes();
	GraphEditor->ClearSelectionSet();

	for (UObject* SelectedNode : SelectedNodes)
	{
		if (SelectedNode)
		{
			USoundSubmixGraphNode* GraphNode = CastChecked<USoundSubmixGraphNode>(SelectedNode);
			if (USoundSubmix* Submix = GraphNode->SoundSubmix)
			{
				if (InSubmixes.Contains(Submix))
				{
					ObjectsToSelect.Add(Submix);
					GraphEditor->SetNodeSelection(GraphNode, true /* bSelect */);
				}
			}
		}
	}

	DetailsView->SetObjects(ObjectsToSelect);
}

bool FSoundSubmixEditor::CanSelectAllNodes() const
{
	return true;
}

void FSoundSubmixEditor::RemoveSelectedNodes()
{
	const FScopedTransaction Transaction(LOCTEXT("SoundSubmixEditorRemoveSelectedNode", "Sound Submix Editor: Remove Selected SoundSubmixes from editor"));

	int32 NumObjectsRemoved = 0;

	const TSet<UObject*> SelectedNodes = GraphEditor->GetSelectedNodes();
	for (UObject* SelectedNode : SelectedNodes)
	{
		USoundSubmixGraphNode* Node = Cast<USoundSubmixGraphNode>(SelectedNode);
		if (Node && Node->SoundSubmix && Node->CanUserDeleteNode())
		{
			NumObjectsRemoved++;
			RemoveEditingObject(Node->SoundSubmix);
		}
	}

	if (NumObjectsRemoved > 0)
	{
		USoundSubmixGraph* Graph = CastChecked<USoundSubmixGraph>(GraphEditor->GetCurrentGraph());
		Graph->RecursivelyRemoveNodes(SelectedNodes);
		GraphEditor->ClearSelectionSet();
	}
}

bool FSoundSubmixEditor::CanRemoveNodes() const
{
	return GraphEditor->GetSelectedNodes().Num() > 0;
}

void FSoundSubmixEditor::UndoGraphAction()
{
	GEditor->UndoTransaction();
}

void FSoundSubmixEditor::RedoGraphAction()
{
	// Clear selection, to avoid holding refs to nodes that go away
	GraphEditor->ClearSelectionSet();

	GEditor->RedoTransaction();
}

void FSoundSubmixEditor::CreateSoundSubmix(UEdGraphPin* FromPin, const FVector2D Location, const FString& Name)
{
	if (Name.IsEmpty())
	{
		return;
	}

	for (UObject* Obj : GetEditingObjects())
	{
		if (Obj->GetName() == Name)
		return;
	}

	// Derive new package path from existing asset's path
	USoundSubmix* SoundSubmix = CastChecked<USoundSubmix>(GetEditingObjects()[0]);
	FString PackagePath = SoundSubmix->GetPathName();
	FString AssetName = FString::Printf(TEXT("/%s.%s"), *SoundSubmix->GetName(), *SoundSubmix->GetName());
	PackagePath.RemoveFromEnd(AssetName);

	// Create a sound submix factory to create a new sound class
	USoundSubmixFactory* SoundSubmixFactory = NewObject<USoundSubmixFactory>();

	// Load asset tools to create the asset properly
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	USoundSubmix* NewSoundSubmix = Cast<USoundSubmix>(AssetToolsModule.Get().CreateAsset(Name, PackagePath, USoundSubmix::StaticClass(), SoundSubmixFactory, FName("SoundSubmixEditorNewAsset")));

	if (NewSoundSubmix)
	{
		CastChecked<USoundSubmixGraph>(SoundSubmix->SoundSubmixGraph)->AddNewSoundSubmix(FromPin, NewSoundSubmix, Location.X, Location.Y);

		AddEditingObject(NewSoundSubmix);
		NewSoundSubmix->PostEditChange();
		NewSoundSubmix->MarkPackageDirty();
	}
}

UEdGraph* FSoundSubmixEditor::GetGraph()
{
	return GraphEditor->GetCurrentGraph();
}

FText FSoundSubmixEditor::GetToolkitName() const
{
	UObject* EditObject = GetEditingObjects()[0];
	return GetLabelForObject(EditObject);
}

void FSoundSubmixEditor::AddMissingEditableSubmixes()
{
	if (UEdGraph* Graph = GraphEditor->GetCurrentGraph())
	{
		bool bChanged = false;
		if (Graph->Nodes.Num() > GetEditingObjects().Num())
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				USoundSubmixGraphNode* GraphNode = CastChecked<USoundSubmixGraphNode>(Node);
				USoundSubmix* UntrackedSubmix = GraphNode->SoundSubmix;
				if (UntrackedSubmix && !GetEditingObjects().Contains(UntrackedSubmix))
				{
					bChanged = true;
					AddEditingObject(UntrackedSubmix);
				}
			}
		}

		if (bChanged)
		{
			GraphEditor->NotifyGraphChanged();
		}
	}
}

void FSoundSubmixEditor::PostUndo(bool bSuccess)
{
	GraphEditor->ClearSelectionSet();
	GraphEditor->NotifyGraphChanged();
}

#undef LOCTEXT_NAMESPACE
