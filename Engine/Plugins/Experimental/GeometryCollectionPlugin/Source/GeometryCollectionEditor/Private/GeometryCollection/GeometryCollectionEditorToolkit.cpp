// Copyright Epic Games, Inc. All Rights Reserved.

#include "GeometryCollection/GeometryCollectionEditorToolkit.h"

#include "Dataflow/DataflowEditorActions.h"
#include "Dataflow/DataflowEditorPlugin.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowObjectInterface.h"
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

#define LOCTEXT_NAMESPACE "GeometryCollectionEditorToolkit"

//DEFINE_LOG_CATEGORY_STATIC(FGeometryCollectionEditorToolkitLog, Log, All);


const FName FGeometryCollectionEditorToolkit::GraphCanvasTabId(TEXT("GeometryCollectionEditor_GraphCanvas"));
const FName FGeometryCollectionEditorToolkit::PropertiesTabId(TEXT("GeometryCollectionEditor_Properties"));

void FGeometryCollectionEditorToolkit::InitGeometryCollectionAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit)
{
	Dataflow = nullptr;
	GeometryCollection = CastChecked<UGeometryCollection>(ObjectToEdit);

	if (GeometryCollection != nullptr)
	{
		if (GeometryCollection->Dataflow == nullptr)
		{
			const FName NodeName = MakeUniqueObjectName(GeometryCollection, UDataflow::StaticClass(), FName("GeometryCollectionDataflowAsset"));
			GeometryCollection->Dataflow = NewObject<UDataflow>(GeometryCollection, NodeName);
		}
		Dataflow = GeometryCollection->Dataflow;
		GeometryCollection->Dataflow->Schema = UDataflowSchema::StaticClass();

		PropertiesEditor = CreatePropertiesEditorWidget(ObjectToEdit);
		GraphEditor = CreateGraphEditorWidget(Dataflow, PropertiesEditor);

		const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("GeometryCollectionDataflowEditor_Layout")
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
		FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FName(TEXT("GeometryCollectionEditorApp")), StandaloneDefaultLayout, bCreateDefaultToolbar, bCreateDefaultStandaloneMenu, ObjectToEdit);
	}
}

TSharedRef<SGraphEditor> FGeometryCollectionEditorToolkit::CreateGraphEditorWidget(UDataflow* DataflowToEdit, TSharedPtr<IDetailsView> InPropertiesEditor)
{
	ensure(DataflowToEdit);
	using namespace Dataflow;
	IDataflowEditorPlugin& DataflowEditorModule = FModuleManager::LoadModuleChecked<IDataflowEditorPlugin>(TEXT("DataflowEditor"));

	FDataflowEditorCommands::FGraphEvaluationCallback Evaluate = [&](Dataflow::FNode* Node, Dataflow::FConnection* Out)
	{
		float EvalTime = FGameTime::GetTimeSinceAppStart().GetRealTimeSeconds();
		return Node->Evaluate(FEngineContext(GeometryCollection, Dataflow, EvalTime, FName("UGeometryCollection")), Out);
	};

	return SNew(SDataflowGraphEditor, GeometryCollection)
		.GraphToEdit(DataflowToEdit)
		.DetailsView(InPropertiesEditor)
		.EvaluateGraph(Evaluate);
}

TSharedPtr<IDetailsView> FGeometryCollectionEditorToolkit::CreatePropertiesEditorWidget(UObject* ObjectToEdit)
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


TSharedRef<SDockTab> FGeometryCollectionEditorToolkit::SpawnTab_GraphCanvas(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == GraphCanvasTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("GeometryCollectionEditor_Dataflow_TabTitle", "Graph"))
		[
			GraphEditor.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FGeometryCollectionEditorToolkit::SpawnTab_Properties(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PropertiesTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("GeometryCollectionEditor_Properties_TabTitle", "Details"))
		[
			PropertiesEditor.ToSharedRef()
		];
}


void FGeometryCollectionEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_GeometryCollectionEditor", "Dataflow Editor"));

	InTabManager->RegisterTabSpawner(GraphCanvasTabId, FOnSpawnTab::CreateSP(this, &FGeometryCollectionEditorToolkit::SpawnTab_GraphCanvas))
		.SetDisplayName(LOCTEXT("DataflowTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(PropertiesTabId, FOnSpawnTab::CreateSP(this, &FGeometryCollectionEditorToolkit::SpawnTab_Properties))
		.SetDisplayName(LOCTEXT("PropertiesTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}


FName FGeometryCollectionEditorToolkit::GetToolkitFName() const
{
	return FName("GeometryCollectionEditor");
}

FText FGeometryCollectionEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Geometry Collection Editor");
}

FString FGeometryCollectionEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "GeometryCollection").ToString();
}

FLinearColor FGeometryCollectionEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FGeometryCollectionEditorToolkit::GetReferencerName() const
{
	return TEXT("GeometryCollectionEditorToolkit");
}

void FGeometryCollectionEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	if (Dataflow)
	{
		Collector.AddReferencedObject(Dataflow);
	}
	if (GeometryCollection)
	{
		Collector.AddReferencedObject(GeometryCollection);
	}
}
#undef LOCTEXT_NAMESPACE
