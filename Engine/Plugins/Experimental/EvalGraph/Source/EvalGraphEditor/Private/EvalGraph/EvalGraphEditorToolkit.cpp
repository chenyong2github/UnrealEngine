// Copyright Epic Games, Inc. All Rights Reserved.

#include "EvalGraphEditorToolkit.h"

#include "EvalGraph/EvalGraphEditorActions.h"
#include "EvalGraph/EvalGraphEdNode.h"
#include "EvalGraph/EvalGraphNodeFactory.h"
#include "EvalGraph/EvalGraphObject.h"
#include "EvalGraph/EvalGraphSchema.h"
#include "EvalGraph/EvalGraph.h"
#include "EditorStyleSet.h"
#include "EditorViewportTabContent.h"
#include "EditorViewportLayout.h"
#include "EditorViewportCommands.h"
#include "GraphEditorActions.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateStyleRegistry.h"

#define LOCTEXT_NAMESPACE "EvalGraphEditorToolkit"

//DEFINE_LOG_CATEGORY_STATIC(FEvalGraphEditorToolkitLog, Log, All);


const FName FEvalGraphEditorToolkit::GraphCanvasTabId(TEXT("EvalGraphEditor_GraphCanvas"));
const FName FEvalGraphEditorToolkit::PropertiesTabId(TEXT("EvalGraphEditor_Properties"));

void FEvalGraphEditorToolkit::InitEvalGraphEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit)
{
	EvalGraph = CastChecked<UEvalGraph>(ObjectToEdit);
	if (EvalGraph != nullptr)
	{
		EvalGraph->Schema = UEvalGraphSchema::StaticClass();

		GraphEditor = CreateGraphEditorWidget(EvalGraph);
		PropertiesEditor = CreatePropertiesEditorWidget(ObjectToEdit);

		const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("EvalGraph_Layout")
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
		FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FName(TEXT("EvalGraphEditorApp")), StandaloneDefaultLayout, bCreateDefaultToolbar, bCreateDefaultStandaloneMenu, ObjectToEdit);
	}
}

void FEvalGraphEditorToolkit::EvaluateNode()
{
	if (UEvalGraph* Graph = dynamic_cast<UEvalGraph*>(GraphEditor->GetCurrentGraph()))
	{
		for (UObject* Ode : GetSelectedNodes())
		{
			if (UEvalGraphEdNode* Node = dynamic_cast<UEvalGraphEdNode*>(Ode))
			{
				//UE_LOG(FEvalGraphEditorToolkitLog, Verbose, TEXT("FEvalGraphEditorToolkit::EvaluateNode(%s)"), *Ode->GetFullName());
				//Graph->Evaluate(Node);
			}
		}
	}
}
void FEvalGraphEditorToolkit::CreateNode(FName RegisteredNode)
{
	//UE_LOG(FEvalGraphEditorToolkitLog, Verbose, TEXT("FEvalGraphEditorToolkit::CreateNode"), RegisteredNode);
	if (UEvalGraph* Graph = dynamic_cast<UEvalGraph*>(GraphEditor->GetCurrentGraph()))
	{
		if (UEvalGraph * Asset = GetEvalGraph())
		{
			//TSharedPtr<FNewManagedArrayCollectionNode> Node1 = AddNode(new FNewManagedArrayCollectionNode({ FName("Node1") }));
		}
	}
}


FGraphPanelSelectionSet FEvalGraphEditorToolkit::GetSelectedNodes() const
{
	if (GraphEditor.IsValid())
	{
		return GraphEditor->GetSelectedNodes();
	}
	return FGraphPanelSelectionSet();
}

void FEvalGraphEditorToolkit::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
}



TSharedRef<SGraphEditor> FEvalGraphEditorToolkit::CreateGraphEditorWidget(UEvalGraph* EvalGraphToEdit)
{
	ensure(EvalGraphToEdit);

	FEvalGraphEditorCommands::Register();
	FGraphEditorCommands::Register();

	// No need to regenerate the commands.
	if (!GraphEditorCommands.IsValid())
	{
		GraphEditorCommands = MakeShareable(new FUICommandList);
		{
			//if (Eg::FNodeFactory* Factory = Eg::FNodeFactory::GetInstance())
			//{
			//	for (FName NodeName : Factory->RegisteredNodes())
			//	{
			//		if (FEvalGraphEditorCommands::Get().CreateNodesMap.Contains(NodeName))
			//		{
			//			GraphEditorCommands->MapAction(FEvalGraphEditorCommands::Get().CreateNodesMap[NodeName],
			//				FExecuteAction::CreateSP(this, &FEvalGraphEditorToolkit::CreateNode, NodeName));
			//		}
//
	//			}
	//		}

			GraphEditorCommands->MapAction(FEvalGraphEditorCommands::Get().EvaluateNode,
				FExecuteAction::CreateSP(this, &FEvalGraphEditorToolkit::EvaluateNode)
			);
		}
	}


	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_EvalGraphEditor", "EvalGraph Graph");

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FEvalGraphEditorToolkit::OnSelectedNodesChanged);

	return SNew(SGraphEditor)
		.AdditionalCommands(GraphEditorCommands)
		.IsEditable(true)
		.Appearance(AppearanceInfo)
		.GraphToEdit(EvalGraphToEdit)
		.GraphEvents(InEvents)
		.ShowGraphStateOverlay(false);
}

TSharedPtr<IDetailsView> FEvalGraphEditorToolkit::CreatePropertiesEditorWidget(UObject* ObjectToEdit)
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


TSharedRef<SDockTab> FEvalGraphEditorToolkit::SpawnTab_GraphCanvas(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == GraphCanvasTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("EvalGraphEditor_EvalGraph_TabTitle", "Graph"))
		[
			GraphEditor.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FEvalGraphEditorToolkit::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PropertiesTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("EvalGraphEditor_Properties_TabTitle", "Details"))
		[
			PropertiesEditor.ToSharedRef()
		];
}


void FEvalGraphEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_EvalGraphEditor", "EvalGraph Editor"));

	InTabManager->RegisterTabSpawner(GraphCanvasTabId, FOnSpawnTab::CreateSP(this, &FEvalGraphEditorToolkit::SpawnTab_GraphCanvas))
		.SetDisplayName(LOCTEXT("EvalGraphTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FEvalGraphEditorToolkit::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("PropertiesTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}


FName FEvalGraphEditorToolkit::GetToolkitFName() const
{
	return FName("EvalGraphEditor");
}

FText FEvalGraphEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "EvalGraph Editor");
}

FString FEvalGraphEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "EvalGraph").ToString();
}

FLinearColor FEvalGraphEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FEvalGraphEditorToolkit::GetReferencerName() const
{
	return TEXT("EvalGraphEditorToolkit");
}

void FEvalGraphEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (EvalGraph)
	{
		Collector.AddReferencedObject(EvalGraph);
	}
}
#undef LOCTEXT_NAMESPACE
