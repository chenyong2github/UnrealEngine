// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowEditorToolkit.h"

#include "Animation/Skeleton.h"
#include "Dataflow/DataflowCore.h"
#include "Dataflow/DataflowEditorCommands.h"
#include "Dataflow/DataflowEditorViewport.h"
#include "Dataflow/DataflowEdNode.h"
#include "Dataflow/DataflowGraphEditor.h"
#include "Dataflow/DataflowNodeFactory.h"
#include "Dataflow/DataflowObject.h"
#include "Dataflow/DataflowObjectInterface.h"
#include "Dataflow/DataflowSchema.h"
#include "DynamicMeshBuilder.h"
#include "EditorStyleSet.h"
#include "EditorViewportTabContent.h"
#include "EditorViewportLayout.h"
#include "EditorViewportCommands.h"
#include "Engine/SkeletalMesh.h"
#include "GraphEditorActions.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Commands/GenericCommands.h"
#include "Widgets/Docking/SDockTab.h"
#include "ISkeletonTree.h"
#include "ISkeletonEditorModule.h"
#include "IStructureDetailsView.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "DataflowEditorToolkit"

//DEFINE_LOG_CATEGORY_STATIC(FDataflowEditorToolkitLog, Log, All);

const FName FDataflowEditorToolkit::ViewportTabId(TEXT("DataflowEditor_Viewport"));
const FName FDataflowEditorToolkit::GraphCanvasTabId(TEXT("DataflowEditor_GraphCanvas"));
const FName FDataflowEditorToolkit::AssetDetailsTabId(TEXT("DataflowEditor_AssetDetails"));
const FName FDataflowEditorToolkit::NodeDetailsTabId(TEXT("DataflowEditor_NodeDetails"));
const FName FDataflowEditorToolkit::SkeletalTabId(TEXT("DataflowEditor_Skeletal"));
const FName FDataflowEditorToolkit::SelectionViewTabId(TEXT("DataflowEditor_SelectionView"));


UDataflow* GetDataflowFrom(UObject* InObject)
{
	if (UClass* Class = InObject->GetClass())
	{
		if (FProperty* Property = Class->FindPropertyByName(FName("DataflowAsset")))
		{
			return *Property->ContainerPtrToValuePtr<UDataflow*>(InObject);
		}
	}
	return nullptr;

}

USkeletalMesh* GetSkeletalMeshFrom(UObject* InObject)
{
	if (UClass* Class = InObject->GetClass())
	{
		if (FProperty* Property = Class->FindPropertyByName(FName("SkeletalMesh")))
		{
			return *Property->ContainerPtrToValuePtr<USkeletalMesh*>(InObject);
		}
	}
	return nullptr;

}

FString GetDataflowTerminalFrom(UObject* InObject)
{
	if (UClass* Class = InObject->GetClass())
	{
		if (FProperty* Property = Class->FindPropertyByName(FName("DataflowTerminal")))
		{
			return *Property->ContainerPtrToValuePtr<FString>(InObject);
		}
	}
	return FString();
}

bool FDataflowEditorToolkit::CanOpenDataflowEditor(UObject* ObjectToEdit)
{
	UDataflow* Dataflow = Cast<UDataflow>(ObjectToEdit);
	if (Dataflow == nullptr)
	{
		Dataflow = GetDataflowFrom(ObjectToEdit);
	}
	return Dataflow != nullptr;
}

FDataflowEditorToolkit::~FDataflowEditorToolkit()
{
	if (GraphEditor)
	{
		GraphEditor->OnSelectionChangedMulticast.Remove(OnSelectionChangedMulticastDelegateHandle);
		GraphEditor->OnNodeDeletedMulticast.Remove(OnNodeDeletedMulticastDelegateHandle);
	}
}

void FDataflowEditorToolkit::InitializeEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit)
{
	Asset = nullptr;
	Dataflow = Cast<UDataflow>(ObjectToEdit);
	if (Dataflow == nullptr)
	{
		Dataflow = GetDataflowFrom(ObjectToEdit);
		if (Dataflow)
		{
			Asset = ObjectToEdit;
			TerminalPath = GetDataflowTerminalFrom(ObjectToEdit);
		}
	}

	if (Dataflow != nullptr)
	{
		Context = TSharedPtr< Dataflow::FEngineContext>(new Dataflow::FAssetContext(Asset, Dataflow, FPlatformTime::Cycles64()));
		LastNodeTimestamp = Context->GetTimestamp();

		Dataflow->Schema = UDataflowSchema::StaticClass();

		NodeDetailsEditor = CreateNodeDetailsEditorWidget(ObjectToEdit);
		AssetDetailsEditor = CreateAssetDetailsEditorWidget(ObjectToEdit);
		GraphEditor = CreateGraphEditorWidget(Dataflow, NodeDetailsEditor);
		SkeletalEditor = CreateSkeletalEditorWidget(ObjectToEdit);

		const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Dataflow_Layout.V1")
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
								->AddTab(AssetDetailsTabId, ETabState::OpenedTab)
							)
						)
					)
				)
			);

		const bool bCreateDefaultStandaloneMenu = true;
		const bool bCreateDefaultToolbar = true;
		FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FName(TEXT("DataflowEditorApp")), StandaloneDefaultLayout, bCreateDefaultToolbar, bCreateDefaultStandaloneMenu, ObjectToEdit);


		AddEditingObject(Dataflow);
		if (Asset) 
		{
			AddEditingObject(Asset);
		}
	}
}

void FDataflowEditorToolkit::OnPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent)
{
	FDataflowEditorCommands::OnPropertyValueChanged(this->GetDataflow(), Context, LastNodeTimestamp, PropertyChangedEvent, PrevNodeSelection);
}

bool FDataflowEditorToolkit::OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* GraphNode, FText& OutErrorMessage)
{
	return FDataflowEditorCommands::OnNodeVerifyTitleCommit(NewText, GraphNode, OutErrorMessage);
}

void FDataflowEditorToolkit::OnNodeTitleCommitted(const FText& InNewText, ETextCommit::Type InCommitType, UEdGraphNode* GraphNode)
{
	FDataflowEditorCommands::OnNodeTitleCommitted(InNewText, InCommitType, GraphNode);
}

void FDataflowEditorToolkit::OnNodeSelectionChanged(const TSet<UObject*>& NewSelection)
{
	if (Dataflow)
	{
		// Only keep UDataflowEdNode from NewSelection
		TSet<UObject*> ValidatedSelection;

		for (UObject* Item : NewSelection)
		{
			if (UDataflowEdNode* Node = Cast<UDataflowEdNode>(Item))
			{
				ValidatedSelection.Add(Item);
			}
		}

		if (ValidatedSelection.Num() > 0)
		{
			TSet<UObject*> SelectionToUse, SelectionDifference;

			if (PrevNodeSelection.Num() > 0)
			{
				SelectionDifference = ValidatedSelection.Difference(PrevNodeSelection);

				if (SelectionDifference.Num() > 0)
				{
					SelectionToUse.Add(SelectionDifference.Array()[SelectionDifference.Num() - 1]);
				}
				else
				{
					SelectionToUse.Add(ValidatedSelection.Array()[ValidatedSelection.Num() - 1]);
				}
			}
			else
			{
				SelectionToUse.Add(ValidatedSelection.Array()[ValidatedSelection.Num() - 1]);
			}

			if (UDataflowEdNode* Node = Cast<UDataflowEdNode>(SelectionToUse.Array()[0]))
			{
				for (IDataflowViewListener* Listener : ViewListeners)
				{
					Listener->OnSelectedNodeChanged(Node);
				}
			}
		}
		else
		{
			for (IDataflowViewListener* Listener : ViewListeners)
			{
				Listener->OnSelectedNodeChanged(nullptr);
			}
		}
		
		PrevNodeSelection = ValidatedSelection;
	}
}

void FDataflowEditorToolkit::OnNodeDeleted(const TSet<UObject*>& NewSelection)
{
	for (UObject* Node : NewSelection)
	{
		if (PrevNodeSelection.Contains(Node))
		{
			PrevNodeSelection.Remove(Node);
		}
	}
}

void FDataflowEditorToolkit::Tick(float DeltaTime)
{
	if (Dataflow && Asset)
	{
		if (!Context)
		{
			Context = TSharedPtr< Dataflow::FEngineContext>(new Dataflow::FAssetContext(Asset, Dataflow, Dataflow::FTimestamp::Invalid));
			LastNodeTimestamp = Dataflow::FTimestamp::Invalid;
		}
		TerminalPath = GetDataflowTerminalFrom(Asset);
		FDataflowEditorCommands::EvaluateTerminalNode(*Context.Get(), LastNodeTimestamp, Dataflow, nullptr, nullptr, Asset, TerminalPath);
	}
}

TStatId FDataflowEditorToolkit::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FDataflowEditorToolkit, STATGROUP_Tickables);
}

TSharedRef<SDataflowGraphEditor> FDataflowEditorToolkit::CreateGraphEditorWidget(UDataflow* DataflowToEdit, TSharedPtr<IStructureDetailsView> InNodeDetailsEditor)
{
	ensure(DataflowToEdit);
	using namespace Dataflow;

	FDataflowEditorCommands::FGraphEvaluationCallback Evaluate = [&](FDataflowNode* Node, FDataflowOutput* Out)
	{
		if (!Context)
		{
			Context = TSharedPtr< Dataflow::FEngineContext>(new Dataflow::FAssetContext(Asset, Dataflow, Dataflow::FTimestamp::Invalid));
		}
		LastNodeTimestamp = Dataflow::FTimestamp::Invalid;

		FDataflowEditorCommands::EvaluateTerminalNode(*Context.Get(), LastNodeTimestamp, Dataflow, Node, Out, Asset, TerminalPath);
	};

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &FDataflowEditorToolkit::OnNodeVerifyTitleCommit);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FDataflowEditorToolkit::OnNodeTitleCommitted);

	TSharedRef<SDataflowGraphEditor> NewGraphEditor = SNew(SDataflowGraphEditor, DataflowToEdit)
		.GraphToEdit(DataflowToEdit)
		.GraphEvents(InEvents)
		.DetailsView(InNodeDetailsEditor)
		.EvaluateGraph(Evaluate);

	OnSelectionChangedMulticastDelegateHandle = NewGraphEditor->OnSelectionChangedMulticast.AddSP(this, &FDataflowEditorToolkit::OnNodeSelectionChanged);
	OnNodeDeletedMulticastDelegateHandle = NewGraphEditor->OnNodeDeletedMulticast.AddSP(this, &FDataflowEditorToolkit::OnNodeDeleted);

	return NewGraphEditor;
}

TSharedPtr<IStructureDetailsView> FDataflowEditorToolkit::CreateNodeDetailsEditorWidget(UObject* ObjectToEdit)
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
	DetailsView->GetOnFinishedChangingPropertiesDelegate().AddSP(this, &FDataflowEditorToolkit::OnPropertyValueChanged);

	return DetailsView;
}

TSharedPtr<IDetailsView> FDataflowEditorToolkit::CreateAssetDetailsEditorWidget(UObject* ObjectToEdit)
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

TSharedPtr<ISkeletonTree> FDataflowEditorToolkit::CreateSkeletalEditorWidget(UObject* ObjectToEdit)
{
	if (Dataflow)
	{
		if (!StubSkeletalMesh)
		{
			const FName NodeName = MakeUniqueObjectName(Dataflow, UDataflow::StaticClass(), FName("USkeleton"));
			StubSkeleton = NewObject<USkeleton>(Dataflow, NodeName);
			const FName NodeName2 = MakeUniqueObjectName(Dataflow, UDataflow::StaticClass(), FName("USkeleton"));
			StubSkeletalMesh = NewObject<USkeletalMesh>(Dataflow, NodeName2);
			StubSkeletalMesh->SetSkeleton(StubSkeleton);
		}

		FSkeletonTreeArgs SkeletonTreeArgs;
		//SkeletonTreeArgs.OnSelectionChanged = FOnSkeletonTreeSelectionChanged::CreateSP(this, &FAnimationBlueprintEditor::HandleSelectionChanged);
		//SkeletonTreeArgs.PreviewScene = GetPreviewScene();
		//SkeletonTreeArgs.ContextName = GetToolkitFName();

		USkeleton* Skeleton = StubSkeleton;
		if (Asset)
		{
			if (USkeletalMesh* SkeletalMesh = GetSkeletalMeshFrom(Asset))
			{
				Skeleton = SkeletalMesh->GetSkeleton();
			}
		}
		ISkeletonEditorModule& SkeletonEditorModule = FModuleManager::LoadModuleChecked<ISkeletonEditorModule>("SkeletonEditor");
		TSharedPtr<ISkeletonTree> SkeletonTree = SkeletonEditorModule.CreateSkeletonTree(Skeleton, SkeletonTreeArgs);
		return SkeletonTree;
	}
	return TSharedPtr<ISkeletonTree>(nullptr);
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == ViewportTabId);

	TSharedRef< SDockTab > DockableTab = SNew(SDockTab);
	ViewportEditor = MakeShareable(new FEditorViewportTabContent());
	TWeakPtr<FDataflowEditorToolkit> WeakSharedThis = SharedThis(this);

	const FString LayoutId = FString("DataflowEditorViewport");
	ViewportEditor->Initialize([WeakSharedThis](const FAssetEditorViewportConstructionArgs& InConstructionArgs)
		{
			return SNew(SDataflowEditorViewport)
				.DataflowEditorToolkit(WeakSharedThis);
		}, DockableTab, LayoutId);

	return DockableTab;
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

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_AssetDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == AssetDetailsTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_AssetDetails_TabTitle", "Details"))
		[
			AssetDetailsEditor->AsShared()
		];
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_NodeDetails(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == NodeDetailsTabId);

	return SNew(SDockTab)
		.Label(LOCTEXT("DataflowEditor_NodeDetails_TabTitle", "Node Details"))
		[
			NodeDetailsEditor->GetWidget()->AsShared()
		];
}

TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_Skeletal(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == SkeletalTabId);

	USkeletalMesh* SkeletalMesh = StubSkeletalMesh;
	if (Asset)
	{
		SkeletalMesh = GetSkeletalMeshFrom(Asset);
	}

	SkeletalEditor->SetSkeletalMesh(SkeletalMesh);

	return SNew(SDockTab)
		.Label(LOCTEXT("FleshEditorSkeletal_TabTitle", "Skeletal Hierarchy"))
		[
			SkeletalEditor.ToSharedRef()
		];
}


TSharedRef<SDockTab> FDataflowEditorToolkit::SpawnTab_SelectionView(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == SelectionViewTabId);

	DataflowSelectionView = MakeShared<FDataflowSelectionView>(FDataflowSelectionView());
	ViewListeners.Add(DataflowSelectionView.Get());

	TSharedPtr<SSelectionViewWidget> SelectionViewWidget;

	TSharedRef<SDockTab> DockableTab = SNew(SDockTab)
	[
		SAssignNew(SelectionViewWidget, SSelectionViewWidget)
	];

	if (SelectionViewWidget)
	{
		DataflowSelectionView->SetSelectionView(SelectionViewWidget);

		// Set the Context on the interface
		if (TSharedPtr<Dataflow::FContext> CurrentContext = this->GetContext())
		{
			DataflowSelectionView->SetContext(CurrentContext);
		}
	}

	return DockableTab;
}

void FDataflowEditorToolkit::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
{
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_DataflowEditor", "Dataflow Editor"));

	InTabManager->RegisterTabSpawner(ViewportTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("DataflowViewportTab", "Dataflow Viewport"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(GraphCanvasTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_GraphCanvas))
		.SetDisplayName(LOCTEXT("DataflowTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));

	InTabManager->RegisterTabSpawner(AssetDetailsTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_AssetDetails))
		.SetDisplayName(LOCTEXT("AssetDetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(NodeDetailsTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_NodeDetails))
		.SetDisplayName(LOCTEXT("NodeDetailsTab", "Node Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(SkeletalTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_Skeletal))
		.SetDisplayName(LOCTEXT("DataflowSkeletalTab", "Skeletal Hierarchy"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.SkeletalHierarchy"));

	InTabManager->RegisterTabSpawner(SelectionViewTabId, FOnSpawnTab::CreateSP(this, &FDataflowEditorToolkit::SpawnTab_SelectionView))
		.SetDisplayName(LOCTEXT("DataflowSelectionViewTab", "Selection View"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.SelectionView"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
}

FName FDataflowEditorToolkit::GetToolkitFName() const
{
	return FName("DataflowEditor");
}

FText FDataflowEditorToolkit::GetToolkitName() const
{
	if (Asset)
	{
		return  GetLabelForObject(Asset);
	}
	if (Dataflow)
	{
		return  GetLabelForObject(Dataflow);
	}
	return  LOCTEXT("ToolkitName", "Empty Dataflow Editor");
}

FText FDataflowEditorToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Dataflow Editor");
}

FText FDataflowEditorToolkit::GetToolkitToolTipText() const
{
	return  LOCTEXT("ToolkitToolTipText", "Dataflow Editor");
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
	if (Asset)
	{
		Collector.AddReferencedObject(Asset);
	}
}
#undef LOCTEXT_NAMESPACE
