// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataflowEditorToolkit.h"

#include "Dataflow/DataflowEditorActions.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowSchema.h"
#include "Dataflow/DataflowCore.h"
#include "EditorStyleSet.h"
#include "EditorViewportTabContent.h"
#include "EditorViewportLayout.h"
#include "EditorViewportCommands.h"
#include "GraphEditorActions.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Commands/GenericCommands.h"

#define LOCTEXT_NAMESPACE "DataflowEditorToolkit"

//DEFINE_LOG_CATEGORY_STATIC(FDataflowEditorToolkitLog, Log, All);


const FName FDataflowEditorToolkit::GraphCanvasTabId(TEXT("DataflowEditor_GraphCanvas"));
const FName FDataflowEditorToolkit::PropertiesTabId(TEXT("DataflowEditor_Properties"));

void FDataflowEditorToolkit::InitDataflowEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit)
{
	Dataflow = CastChecked<UDataflow>(ObjectToEdit);
	if (Dataflow != nullptr)
	{
		Dataflow->Schema = UDataflowSchema::StaticClass();

		GraphEditor = CreateGraphEditorWidget(Dataflow);
		PropertiesEditor = CreatePropertiesEditorWidget(ObjectToEdit);

		const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Dataflow_Layout")
			->AddArea
			(
				FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
				->Split
				(
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->Split
					(
						FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
						->SetSizeCoefficient(0.9f)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.6f)
							->AddTab(GraphCanvasTabId, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
							->SetSizeCoefficient(0.2f)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.7f)
								->AddTab(PropertiesTabId, ETabState::OpenedTab)
							)
						)
					)
				)
			);

		const bool bCreateDefaultStandaloneMenu = true;
		const bool bCreateDefaultToolbar = true;
		FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FName(TEXT("DataflowEditorApp")), StandaloneDefaultLayout, bCreateDefaultToolbar, bCreateDefaultStandaloneMenu, ObjectToEdit);
	}
}

void FDataflowEditorToolkit::EvaluateNode()
{
	if (UDataflow* Graph = dynamic_cast<UDataflow*>(GraphEditor->GetCurrentGraph()))
	{
		if (TSharedPtr<Dataflow::FGraph> DataflowGraph = Graph->GetDataflow())
		{
			for (UObject* Ode : GetSelectedNodes())
			{
				if (UDataflowEdNode* Node = dynamic_cast<UDataflowEdNode*>(Ode))
				{
					if (TSharedPtr<Dataflow::FNode> DataflowNode = DataflowGraph->FindBaseNode(Node->GetDataflowNodeGuid()))
					{
						for (Dataflow::FConnection* NodeOutput : DataflowNode->GetOutputs())
						{
							DataflowNode->Evaluate({0.f}, NodeOutput);
						}
					}
				}
			}
		}
	}
}

void FDataflowEditorToolkit::DeleteNode()
{
	if (UDataflow* Graph = dynamic_cast<UDataflow*>(GraphEditor->GetCurrentGraph()))
	{
		if (TSharedPtr<Dataflow::FGraph> DataflowGraph = Graph->GetDataflow())
		{
			for (UObject* Ode : GetSelectedNodes())
			{
				if (UDataflowEdNode* EdNode = dynamic_cast<UDataflowEdNode*>(Ode))
				{
					if (TSharedPtr<Dataflow::FNode> DataflowNode = DataflowGraph->FindBaseNode(EdNode->GetDataflowNodeGuid()))
					{
						Graph->RemoveNode(EdNode);
						DataflowGraph->RemoveNode(DataflowNode);
					}
				}
			}
		}
	}
}

FGraphPanelSelectionSet FDataflowEditorToolkit::GetSelectedNodes() const
{
	if (GraphEditor.IsValid())
	{
		return GraphEditor->GetSelectedNodes();
	}
	return FGraphPanelSelectionSet();
}

void FDataflowEditorToolkit::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
}



TSharedRef<SGraphEditor> FDataflowEditorToolkit::CreateGraphEditorWidget(UDataflow* DataflowToEdit)
{
	ensure(DataflowToEdit);

	FDataflowEditorCommands::Register();
	FGraphEditorCommands::Register();

	// No need to regenerate the commands.
	if (!GraphEditorCommands.IsValid())
	{
		GraphEditorCommands = MakeShareable(new FUICommandList);
		{
			GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
				FExecuteAction::CreateSP(this, &FDataflowEditorToolkit::DeleteNode)
			);
			GraphEditorCommands->MapAction(FDataflowEditorCommands::Get().EvaluateNode,
				FExecuteAction::CreateSP(this, &FDataflowEditorToolkit::EvaluateNode)
			);
		}
	}


	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_DataflowEditor", "Dataflow");

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FDataflowEditorToolkit::OnSelectedNodesChanged);

	return SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.IsEditable(true)
		.Appearance(AppearanceInfo)
		.GraphToEdit(DataflowToEdit)
		.GraphEvents(InEvents)
		.ShowGraphStateOverlay(false);
}

TSharedPtr<IDetailsView> FDataflowEditorToolkit::CreatePropertiesEditorWidget(UObject* ObjectToEdit)
{
	ensure(ObjectToEdit);
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bAllowSearch = true;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.NotifyHook = this;

	TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(ObjectToEdit);
	return DetailsView;

}


TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_GraphCanvas(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == GraphCanvasTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_Dataflow_TabTitle", "Graph"))
		[
			GraphEditor.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PropertiesTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_Properties_TabTitle", "Details"))
		[
			PropertiesEditor.ToSharedRef()
		];
}


void FDataflowEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_DataflowEditor", "Dataflow Editor"));

	InTabManager->RegisterTabSpawner(GraphCanvasTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_GraphCanvas))
		.SetDisplayName(LOCTEXT("DataflowTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("PropertiesTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}


FName FDataflowEditorToolkit::GetToolkitFName() const
{
	return FName("DataflowEditor");
}

FText FDataflowEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Dataflow Editor");
}

FString FDataflowEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Dataflow").ToString();
}

FLinearColor FDataflowEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FDataflowEditorToolkit::GetReferencerName() const
{
	return TEXT("DataflowEditorToolkit");
}

void FDataflowEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (Dataflow)
	{
		Collector.AddReferencedObject(Dataflow);
	}
}
#undef LOCTEXT_NAMESPACE
