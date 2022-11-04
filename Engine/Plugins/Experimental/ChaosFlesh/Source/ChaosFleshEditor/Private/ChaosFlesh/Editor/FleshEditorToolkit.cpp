// Copyright Epic Games, Inc. All Rights Reserved.

#include "FleshEditorToolkit.h"

#include "ChaosFlesh/Asset/ActorFactoryFlesh.h"
#include "ChaosFlesh/FleshAsset.h"
#include "Dataflow/DataflowEditor.h"
#include "Dataflow/DataflowEditorActions.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "Dataflow/DataflowNodeParameters.h"
#include "EditorViewportTabContent.h"
#include "EditorViewportLayout.h"
#include "EditorViewportCommands.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "FleshEditorViewport.h"
#include "Framework/Commands/GenericCommands.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "Engine/DataTable.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateStyleRegistry.h"

#include "ISkeletonTree.h"
#include "ISkeletonEditorModule.h"
#include "IStructureDetailsView.h"

DEFINE_LOG_CATEGORY_STATIC(LogChaosFleshEditorToolkit, Log, All);

int32 bDataflowAssetEditorFleshLiveEvaluationEnableCVar = 1;
FAutoConsoleVariableRef CVarDataflowAssetEditorFleshEnableLiveEvaluation(TEXT("p.Dataflow.AssetEditor.Flesh.LiveEvaluation.Enable"),
	bDataflowAssetEditorFleshLiveEvaluationEnableCVar,
	TEXT("Enable live evaluation of specified output on the FleshAsset within the Dataflow Editor.[def:true]"));


#define LOCTEXT_NAMESPACE "FleshEditorToolkit"

const FName FFleshEditorToolkit::ViewportTabId(TEXT("FleshEditor_Viewport"));
const FName FFleshEditorToolkit::GraphCanvasTabId(TEXT("FleshEditor_GraphCanvas"));
const FName FFleshEditorToolkit::AssetDetailsTabId(TEXT("FleshEditor_AssetDetails"));
const FName FFleshEditorToolkit::NodeDetailsTabId(TEXT("FleshEditor_NodeDetails"));
const FName FFleshEditorToolkit::SkeletalTabId(TEXT("FleshEditor_Skeletal"));


void FFleshEditorToolkit::InitFleshAssetEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit)
{
	FleshAsset = CastChecked<UFleshAsset>(ObjectToEdit);
	if (FleshAsset != nullptr)
	{
		if (FleshAsset->Dataflow == nullptr)
		{
			const FName NodeName = MakeUniqueObjectName(FleshAsset, UDataflow::StaticClass(), FName("DataflowFleshAsset"));
			FleshAsset->Dataflow = NewObject<UDataflow>(FleshAsset, NodeName);
		}
		FleshAsset->Dataflow->Schema = UDataflowSchema::StaticClass();
		Dataflow = FleshAsset->Dataflow;

		NodeDetailsEditor = CreateNodeDetailsEditorWidget(ObjectToEdit);
		AssetDetailsEditor = CreateAssetDetailsEditorWidget(FleshAsset);
		GraphEditor = CreateGraphEditorWidget(FleshAsset->Dataflow, NodeDetailsEditor);
		SkeletalEditor = CreateSkeletalEditorWidget(FleshAsset->SkeletalMesh);

		Context = TSharedPtr< Dataflow::FEngineContext>(new Dataflow::FEngineContext(FleshAsset, Dataflow, FPlatformTime::Cycles64(), FString("UFleshAsset")));
		LastNodeTimestamp = Context->GetTimestamp();

		const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("FleshAsset_Layout.V1")
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
							->SetHideTabWell(true)
						)
						->Split
						(
							FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
							->SetSizeCoefficient(0.2f)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.7f)
								->AddTab(NodeDetailsTabId, ETabState::OpenedTab)
							)
						)
					)
				)
			);
			
		const bool bCreateDefaultStandaloneMenu = true;
		const bool bCreateDefaultToolbar = true;
		FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FName(TEXT("FleshEditorApp")), StandaloneDefaultLayout, bCreateDefaultToolbar, bCreateDefaultStandaloneMenu, ObjectToEdit);
	}
}

void FFleshEditorToolkit::OnPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	FDataflowEditorCommands::OnPropertyValueChanged(this->GetDataflow(), Context, LastNodeTimestamp, PropertyChangedEvent);
}

bool FFleshEditorToolkit::OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* GraphNode, FText& OutErrorMessage)
{
	return FDataflowEditorCommands::OnNodeVerifyTitleCommit(NewText, GraphNode, OutErrorMessage);
}

void FFleshEditorToolkit::OnNodeTitleCommitted(const FText& InNewText, ETextCommit::Type InCommitType, UEdGraphNode* GraphNode)
{
	FDataflowEditorCommands::OnNodeTitleCommitted(InNewText, InCommitType, GraphNode);
}

void FFleshEditorToolkit::Tick(float DeltaTime)
{
	if (bDataflowAssetEditorFleshLiveEvaluationEnableCVar)
	{
		if (Dataflow && FleshAsset)
		{
			if (!Context)
			{
				Context = TSharedPtr< Dataflow::FEngineContext>(new Dataflow::FEngineContext(FleshAsset, Dataflow, Dataflow::FTimestamp::Invalid, FString("UFleshAsset")));
				LastNodeTimestamp = Dataflow::FTimestamp::Invalid;
			}

			FDataflowEditorCommands::EvaluateNode(*Context.Get(), LastNodeTimestamp, Dataflow, nullptr, nullptr, FleshAsset->Terminal);
		}
	}
}

TStatId FFleshEditorToolkit::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FFleshEditorToolkit, STATGROUP_Tickables);
}


TSharedRef<SGraphEditor> FFleshEditorToolkit::CreateGraphEditorWidget(UDataflow* DataflowToEdit, TSharedPtr<IStructureDetailsView> InNodeDetailsEditor)
{
	ensure(DataflowToEdit);
	IDataflowEditorPlugin& DataflowEditorModule = FModuleManager::LoadModuleChecked<IDataflowEditorPlugin>(TEXT("DataflowEditor"));

	FDataflowEditorCommands::FGraphEvaluationCallback Evaluate = [&](const FDataflowNode* Node, const FDataflowOutput* Out)
	{
		if (Dataflow && FleshAsset)
		{
			if (!Context)
			{
				Context = TSharedPtr< Dataflow::FEngineContext>(new Dataflow::FEngineContext(FleshAsset, Dataflow, Dataflow::FTimestamp::Invalid, FString("UFleshAsset")));
			}
			LastNodeTimestamp = Dataflow::FTimestamp::Invalid;

			FDataflowEditorCommands::EvaluateNode(*Context.Get(),LastNodeTimestamp,Dataflow,Node,Out);
		}
	};

	FDataflowEditorCommands::FOnDragDropEventCallback  DragDropEvent = [&](const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
	{
		TSharedPtr<FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode> Action = 
			FAssetSchemaAction_Dataflow_CreateNode_DataflowEdNode::CreateAction(Dataflow, "SkeletalMeshBone");
		Action->PerformAction(Dataflow, nullptr, FVector2D(0,0), true);
	};

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &FFleshEditorToolkit::OnNodeVerifyTitleCommit);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FFleshEditorToolkit::OnNodeTitleCommitted);

	UE_LOG(LogChaosFleshEditorToolkit, All, TEXT("FFleshEditorToolkit::CreateGraphEditorWidget"));
	return SNew(SDataflowGraphEditor, FleshAsset)
		.GraphToEdit(DataflowToEdit)
		.GraphEvents(InEvents)
		.DetailsView(InNodeDetailsEditor)
		.EvaluateGraph(Evaluate)
		.OnDragDropEvent(DragDropEvent);
}

TSharedPtr<IStructureDetailsView> FFleshEditorToolkit::CreateNodeDetailsEditorWidget(UObject* ObjectToEdit)
{
	ensure(ObjectToEdit);
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = false;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bSearchInitialKeyFocus = true;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.NotifyHook = nullptr;
		DetailsViewArgs.bShowOptions = true;
		DetailsViewArgs.bShowModifiedPropertiesOption = false;
		DetailsViewArgs.bShowScrollBar = false;
	}

	FStructureDetailsViewArgs StructureViewArgs;
	{
		StructureViewArgs.bShowObjects = true;
		StructureViewArgs.bShowAssets = true;
		StructureViewArgs.bShowClasses = true;
		StructureViewArgs.bShowInterfaces = true;
	}

	TSharedPtr<IStructureDetailsView> DetailsView = PropertyEditorModule.CreateStructureDetailView(DetailsViewArgs, StructureViewArgs, nullptr);
	DetailsView->GetDetailsView()->SetObject(ObjectToEdit);
	DetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &FFleshEditorToolkit::OnPropertyValueChanged);

	return DetailsView;

}


TSharedPtr<IDetailsView> FFleshEditorToolkit::CreateAssetDetailsEditorWidget(UObject* ObjectToEdit)
{
	ensure(ObjectToEdit);
	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bLockable = false;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.NotifyHook = this;
	}

	TSharedPtr<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(ObjectToEdit);
	return DetailsView;
}


TSharedPtr<ISkeletonTree> FFleshEditorToolkit::CreateSkeletalEditorWidget(USkeletalMesh* ObjectToEdit)
{
	check(FleshAsset);

	if (!StubSkeletalMesh)
	{
		const FName NodeName = MakeUniqueObjectName(FleshAsset, UDataflow::StaticClass(), FName("USkeleton"));
		StubSkeleton = NewObject<USkeleton>(FleshAsset, NodeName);
		const FName NodeName2 = MakeUniqueObjectName(FleshAsset, UDataflow::StaticClass(), FName("USkeleton"));
		StubSkeletalMesh = NewObject<USkeletalMesh>(FleshAsset, NodeName2);
		StubSkeletalMesh->SetSkeleton(StubSkeleton);
	}

	FSkeletonTreeArgs SkeletonTreeArgs;
	//SkeletonTreeArgs.OnSelectionChanged = FOnSkeletonTreeSelectionChanged::CreateSP(this, &FAnimationBlueprintEditor::HandleSelectionChanged);
	//SkeletonTreeArgs.PreviewScene = GetPreviewScene();
	//SkeletonTreeArgs.ContextName = GetToolkitFName();

	USkeleton* Skeleton = StubSkeleton;
	if (FleshAsset && FleshAsset->SkeletalMesh && FleshAsset->SkeletalMesh->GetSkeleton())
		Skeleton = FleshAsset->SkeletalMesh->GetSkeleton();

	ISkeletonEditorModule& SkeletonEditorModule = FModuleManager::LoadModuleChecked<ISkeletonEditorModule>("SkeletonEditor");
	TSharedPtr<ISkeletonTree> SkeletonTree = SkeletonEditorModule.CreateSkeletonTree(Skeleton, SkeletonTreeArgs);
	//AddApplicationMode(AnimationEditorModes::AnimationEditorMode,MakeShareable(new FAnimationEditorMode(SharedThis(this), SkeletonTree.ToSharedRef())));
	return SkeletonTree;
}

TSharedRef<SDockTab> FFleshEditorToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == ViewportTabId);

	TSharedRef< SDockTab > DockableTab = SNew(SDockTab);
	ViewportEditor = MakeShareable(new FEditorViewportTabContent());
	TWeakPtr<FFleshEditorToolkit> WeakSharedThis = SharedThis(this);

	const FString LayoutId = FString("FleshEditorViewport");
	ViewportEditor->Initialize([WeakSharedThis](const FAssetEditorViewportConstructionArgs& InConstructionArgs)
		{
			return SNew(SFleshEditorViewport)
				.FleshEditorToolkit(WeakSharedThis);
		}, DockableTab, LayoutId);

	return DockableTab;
}

TSharedRef<SDockTab> FFleshEditorToolkit::SpawnTab_GraphCanvas(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == GraphCanvasTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("FleshDataflowEditor_Dataflow_TabTitle", "Graph"))
		[
			GraphEditor.ToSharedRef()
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FFleshEditorToolkit::SpawnTab_NodeDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == NodeDetailsTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("FleshEditorNodeDetails_TabTitle", "Details"))
		[
			NodeDetailsEditor->GetWidget()->AsShared()
		];
}

TSharedRef<SDockTab> FFleshEditorToolkit::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == AssetDetailsTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("FleshEditorAssetDetails_TabTitle", "Asset Details"))
		[
			AssetDetailsEditor.ToSharedRef()
		];
}

TSharedRef<SDockTab> FFleshEditorToolkit::SpawnTab_Skeletal(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == SkeletalTabId);

	USkeletalMesh* SkeletalMesh = StubSkeletalMesh;
	if (FleshAsset && FleshAsset->SkeletalMesh)
		SkeletalMesh = FleshAsset->SkeletalMesh;

	SkeletalEditor->SetSkeletalMesh(SkeletalMesh);

	return SNew(SDockTab)
		.Label(LOCTEXT("FleshEditorSkeletal_TabTitle", "Skeletal Hierarchy"))
		[
			SkeletalEditor.ToSharedRef()
		];
}


void FFleshEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_FleshEditorEditor", "Flesh Editor"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateSP(this, &FFleshEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("FleshViewportTab", "Flesh Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(GraphCanvasTabId, FOnSpawnTab::CreateSP(this, &FFleshEditorToolkit::SpawnTab_GraphCanvas))
		.SetDisplayName(LOCTEXT("FleshDataflowTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(NodeDetailsTabId, FOnSpawnTab::CreateSP(this, &FFleshEditorToolkit::SpawnTab_NodeDetails))
		.SetDisplayName(LOCTEXT("FleshNodeDetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(AssetDetailsTabId, FOnSpawnTab::CreateSP(this, &FFleshEditorToolkit::SpawnTab_AssetDetails))
		.SetDisplayName(LOCTEXT("FleshAssetDetailsTab", "Asset Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.AssetDetails"));

	InTabManager->RegisterTabSpawner(SkeletalTabId, FOnSpawnTab::CreateSP(this, &FFleshEditorToolkit::SpawnTab_Skeletal))
		.SetDisplayName(LOCTEXT("FleshSkeletalTab", "Skeletal Hierarchy"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.SkeletalHierarchy"));
}


void FFleshEditorToolkit::GetSaveableObjects(TArray<UObject*>& OutObjects) const
{
	if(FleshAsset)
		OutObjects.Add(static_cast<UObject*>(FleshAsset));
	if (Dataflow)
		OutObjects.Add(static_cast<UObject*>(Dataflow));
}


FName FFleshEditorToolkit::GetToolkitFName() const
{
	return FName("FleshEditor");
}

FText FFleshEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Flesh Editor");
}

FString FFleshEditorToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Flesh").ToString();
}

FLinearColor FFleshEditorToolkit::GetWorldCentricTabColorScale() const
{
	return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
}

FString FFleshEditorToolkit::GetReferencerName() const
{
	return TEXT("FFleshEditorToolkit");
}

void FFleshEditorToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(FleshAsset);
}
#undef LOCTEXT_NAMESPACE
