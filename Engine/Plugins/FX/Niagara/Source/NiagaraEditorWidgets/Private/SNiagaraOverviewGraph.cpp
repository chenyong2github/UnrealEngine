// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewGraph.h"
#include "NiagaraOverviewGraphNodeFactory.h"
#include "SNiagaraStack.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "NiagaraSystem.h"
#include "NiagaraOverviewNode.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraObjectSelection.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraEditorCommands.h"
#include "NiagaraEditorModule.h"
#include "GraphEditorActions.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "ScopedTransaction.h"
#include "Framework/Application/SlateApplication.h"
#include "Templates/SharedPointer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Framework/Commands/GenericCommands.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewGraph"

void SNiagaraOverviewGraph::Construct(const FArguments& InArgs, TSharedRef<FNiagaraOverviewGraphViewModel> InViewModel)
{
	ViewModel = InViewModel;
	ViewModel->GetNodeSelection()->OnSelectedObjectsChanged().AddSP(this, &SNiagaraOverviewGraph::ViewModelSelectionChanged);
	ViewModel->GetSystemViewModel()->OnPreClose().AddSP(this, &SNiagaraOverviewGraph::PreClose);

	bUpdatingViewModelSelectionFromGraph = false;
	bUpdatingGraphSelectionFromViewModel = false;

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SNiagaraOverviewGraph::GraphSelectionChanged);
	Events.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &SNiagaraOverviewGraph::OnCreateGraphActionMenu);
	Events.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &SNiagaraOverviewGraph::OnNodeTitleCommitted);

	FGraphAppearanceInfo AppearanceInfo;
	if (ViewModel->GetSystemViewModel()->GetEditMode() == ENiagaraSystemViewModelEditMode::EmitterAsset)
	{
		AppearanceInfo.CornerText = LOCTEXT("NiagaraOverview_AppearanceCornerTextEmitter", "EMITTER");

	}
	else if (ViewModel->GetSystemViewModel()->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		AppearanceInfo.CornerText = LOCTEXT("NiagaraOverview_AppearanceCornerTextSystem", "SYSTEM");
	}
	else
	{
		ensureMsgf(false, TEXT("Encountered unhandled SystemViewModel Edit Mode!"));
		AppearanceInfo.CornerText = LOCTEXT("NiagaraOverview_AppearanceCornerTextGeneric", "NIAGARA");
	}
	
	TSharedRef<SWidget> TitleBarWidget = SNew(SBorder)
	.BorderImage(FEditorStyle::GetBrush(TEXT("Graph.TitleBackground")))
	.HAlign(HAlign_Fill)
	[
		SNew(STextBlock)
		.Text(ViewModel.ToSharedRef(), &FNiagaraOverviewGraphViewModel::GetDisplayName)
		.TextStyle(FEditorStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
		.Justification(ETextJustify::Center)
	];

	TSharedRef<FUICommandList> Commands = ViewModel->GetCommands();
	Commands->MapAction(
		FGraphEditorCommands::Get().CreateComment,
		FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnCreateComment));
	Commands->MapAction(
		FNiagaraEditorModule::Get().GetCommands().ZoomToFit,
		FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::ZoomToFit));
	Commands->MapAction(
		FNiagaraEditorModule::Get().GetCommands().ZoomToFitAll,
		FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::ZoomToFitAll));
	// Alignment Commands
	Commands->MapAction(FGraphEditorCommands::Get().AlignNodesTop,
		FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnAlignTop)
	);

	Commands->MapAction(FGraphEditorCommands::Get().AlignNodesMiddle,
		FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnAlignMiddle)
	);

	Commands->MapAction(FGraphEditorCommands::Get().AlignNodesBottom,
		FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnAlignBottom)
	);

	// Distribution Commands
	Commands->MapAction(FGraphEditorCommands::Get().DistributeNodesHorizontally,
		FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnDistributeNodesH)
	);

	Commands->MapAction(FGraphEditorCommands::Get().DistributeNodesVertically,
		FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnDistributeNodesV)
	);


	GraphEditor = SNew(SGraphEditor)
		.AdditionalCommands(Commands)
		.Appearance(AppearanceInfo)
		.TitleBar(TitleBarWidget)
		.GraphToEdit(ViewModel->GetGraph())
		.GraphEvents(Events);

	GraphEditor->SetNodeFactory(MakeShared<FNiagaraOverviewGraphNodeFactory>());

	FNiagaraGraphViewSettings ViewSettings = ViewModel->GetViewSettings();
	if (ViewSettings.IsValid())
	{
		GraphEditor->SetViewLocation(ViewSettings.GetLocation(), ViewSettings.GetZoom());
		ZoomToFitFrameDelay = 0;
	}
	else
	{
		// When initialzing the graph control the stacks inside the nodes aren't actually available until two frames later due to
		// how the underlying list view works.  In order to zoom to fix correctly we have to delay for an extra fram so we use a
		// counter here instead of a simple bool.
		ZoomToFitFrameDelay = 2;
	}

	ChildSlot
	[
		GraphEditor.ToSharedRef()
	];
}

void SNiagaraOverviewGraph::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (ZoomToFitFrameDelay > 0)
	{
		ZoomToFitFrameDelay--;
		if(ZoomToFitFrameDelay == 0)
		{
			GraphEditor->ZoomToFit(false);
		}
	}
}

void SNiagaraOverviewGraph::ViewModelSelectionChanged()
{
	if (bUpdatingViewModelSelectionFromGraph == false)
	{
		if (FNiagaraEditorUtilities::SetsMatch(GraphEditor->GetSelectedNodes(), ViewModel->GetNodeSelection()->GetSelectedObjects()) == false)
		{
			TGuardValue<bool> UpdateGuard(bUpdatingGraphSelectionFromViewModel, true);
			GraphEditor->ClearSelectionSet();
			for (UObject* SelectedNode : ViewModel->GetNodeSelection()->GetSelectedObjects())
			{
				UEdGraphNode* GraphNode = Cast<UEdGraphNode>(SelectedNode);
				if (GraphNode != nullptr)
				{
					GraphEditor->SetNodeSelection(GraphNode, true);
				}
			}
		}
	}
}

void SNiagaraOverviewGraph::GraphSelectionChanged(const TSet<UObject*>& SelectedNodes)
{
	if (bUpdatingGraphSelectionFromViewModel == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingViewModelSelectionFromGraph, true);
		if (SelectedNodes.Num() == 0)
		{
			ViewModel->GetNodeSelection()->ClearSelectedObjects();
		}
		else
		{
			ViewModel->GetNodeSelection()->SetSelectedObjects(SelectedNodes);
		}
	}
}

void SNiagaraOverviewGraph::PreClose()
{
	if (ViewModel.IsValid() && GraphEditor.IsValid())
	{
		FVector2D Location;
		float Zoom;
		GraphEditor->GetViewLocation(Location, Zoom);
		ViewModel->SetViewSettings(FNiagaraGraphViewSettings(Location, Zoom));
	}
}

FActionMenuContent SNiagaraOverviewGraph::OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	if (ViewModel->GetSystemViewModel()->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		FMenuBuilder MenuBuilder(true, ViewModel->GetCommands());

		MenuBuilder.BeginSection(TEXT("NiagaraOverview_EditGraph"), LOCTEXT("EditGraph", "Edit Graph"));
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("EmitterAddLabel", "Add Emitter"),
				LOCTEXT("EmitterAddToolTip", "Add an existing emitter"),
				FNewMenuDelegate::CreateSP(this, &SNiagaraOverviewGraph::CreateAddEmitterMenuContent, InGraph));

			MenuBuilder.AddMenuEntry(
				LOCTEXT("CommentsLabel", "Add Comment"),
				LOCTEXT("CommentsToolTip", "Add a comment box"),
				FSlateIcon(),
				FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnCreateComment));

			MenuBuilder.AddMenuEntry(
				LOCTEXT("ClearIsolatedLabel", "Clear Isolated"),
				LOCTEXT("ClearIsolatedToolTip", "Clear the current set of isolated emitters."),
				FSlateIcon(),
				FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnClearIsolated));
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection(TEXT("NiagaraOverview_View"), LOCTEXT("View", "View"));
		{
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ZoomToFit);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ZoomToFitAll);
		}
		MenuBuilder.EndSection();

		MenuBuilder.BeginSection(TEXT("NiagaraOverview_Edit"), LOCTEXT("Edit", "Edit"));
		{
			MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste);
		}
		MenuBuilder.EndSection();

		TSharedRef<SWidget> ActionMenu = MenuBuilder.MakeWidget();

		return FActionMenuContent(ActionMenu, ActionMenu);
	}
	return FActionMenuContent(SNullWidget::NullWidget, SNullWidget::NullWidget);
}

void SNiagaraOverviewGraph::OnCreateComment()
{
	FNiagaraSchemaAction_NewComment CommentAction = FNiagaraSchemaAction_NewComment(GraphEditor);
	CommentAction.PerformAction(ViewModel->GetGraph(), nullptr, GraphEditor->GetPasteLocation(), false);
}

void SNiagaraOverviewGraph::OnClearIsolated()
{
	ViewModel->GetSystemViewModel()->IsolateEmitters(TArray<FGuid>());
}

void SNiagaraOverviewGraph::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{
	if (NodeBeingChanged)
	{
		// When you request rename on spawn but accept the value, we want to not add a transaction if they just hit "Enter".
		bool bRename = true;
		FText CurrentNodeTitleText = NodeBeingChanged->GetNodeTitle(ENodeTitleType::FullTitle);
		if (CurrentNodeTitleText.EqualTo(NewText))
		{
			return;
		}

		if (NodeBeingChanged->IsA(UNiagaraOverviewNode::StaticClass())) //@TODO System Overview: renaming system or emitters locally through this view
		{
			UNiagaraOverviewNode* OverviewNodeBeingChanged = Cast<UNiagaraOverviewNode>(NodeBeingChanged);
			TSharedPtr<FNiagaraEmitterHandleViewModel> NodeEmitterHandleViewModel = ViewModel->GetSystemViewModel()->GetEmitterHandleViewModelById(OverviewNodeBeingChanged->GetEmitterHandleGuid());
			if (ensureMsgf(NodeEmitterHandleViewModel.IsValid(), TEXT("Failed to find EmitterHandleViewModel with matching Emitter GUID to Overview Node!")))
			{
				NodeEmitterHandleViewModel->OnNameTextComitted(NewText, CommitInfo);
			}
			else
			{
				bRename = false;
			}
		}

		if (bRename)
		{
			const FScopedTransaction Transaction(LOCTEXT("RenameNode", "Rename Node"));
			NodeBeingChanged->Modify();
			NodeBeingChanged->OnRenameNode(NewText.ToString());
		}
	}
}

void SNiagaraOverviewGraph::CreateAddEmitterMenuContent(FMenuBuilder& MenuBuilder, UEdGraph* InGraph)
{
	FAssetPickerConfig AssetPickerConfig;
	{
		AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateLambda([this, InGraph](const FAssetData& AssetData)
		{
			FSlateApplication::Get().DismissAllMenus();
			ViewModel->GetSystemViewModel()->AddEmitterFromAssetData(AssetData);
		});
		AssetPickerConfig.bAllowNullSelection = false;
		AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
		AssetPickerConfig.Filter.ClassNames.Add(UNiagaraEmitter::StaticClass()->GetFName());
	}

	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	TSharedRef<SWidget> EmitterAddSubMenu =
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush("Menu.Background"))
		.Padding(5.0f)
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			.HeightOverride(300.0f)
			[
				ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
			]
		];

	MenuBuilder.AddWidget(EmitterAddSubMenu, FText());
}

void SNiagaraOverviewGraph::ZoomToFit()
{
	GraphEditor->ZoomToFit(true);
}

void SNiagaraOverviewGraph::ZoomToFitAll()
{
	GraphEditor->ZoomToFit(false);
}


void SNiagaraOverviewGraph::OnAlignTop()
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->OnAlignTop();
	}
}

void SNiagaraOverviewGraph::OnAlignMiddle()
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->OnAlignMiddle();
	}
}

void SNiagaraOverviewGraph::OnAlignBottom()
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->OnAlignBottom();
	}
}

void SNiagaraOverviewGraph::OnDistributeNodesH()
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->OnDistributeNodesH();
	}
}

void SNiagaraOverviewGraph::OnDistributeNodesV()
{
	if (GraphEditor.IsValid())
	{
		GraphEditor->OnDistributeNodesV();
	}
}

#undef LOCTEXT_NAMESPACE // "NiagaraOverviewGraph"