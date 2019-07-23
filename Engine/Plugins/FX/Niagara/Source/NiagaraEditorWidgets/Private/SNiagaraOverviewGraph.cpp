// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewGraph.h"
#include "SNiagaraStack.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemEditorData.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "ViewModels/NiagaraOverviewGraphViewModel.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraOverviewNodeStackItem.h"
#include "GraphEditorActions.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewGraph"

void SNiagaraOverviewGraph::Construct(const FArguments& InArgs, TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
{
	SystemViewModel = InSystemViewModel;
	bUpdatingOverviewSelectionFromGraph = false;
	bUpdatingGraphSelectionFromOverview = false;

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SNiagaraOverviewGraph::GraphSelectionChanged);
	Events.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &SNiagaraOverviewGraph::OnCreateGraphActionMenu);
	Events.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &SNiagaraOverviewGraph::OnNodeTitleCommitted);

	TSharedPtr<FNiagaraOverviewGraphViewModel> OverviewGraphViewModel = SystemViewModel->GetOverviewGraphViewModel();

	FGraphAppearanceInfo AppearanceInfo;
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::EmitterAsset)
	{
		AppearanceInfo.CornerText = LOCTEXT("NiagaraOverview_AppearanceCornerTextEmitter", "EMITTER");

	}
	else if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
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
		.Text(OverviewGraphViewModel.ToSharedRef(), &FNiagaraOverviewGraphViewModel::GetDisplayName)
		.TextStyle(FEditorStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
		.Justification(ETextJustify::Center)
	];

	TSharedRef<FUICommandList> Commands = OverviewGraphViewModel->GetCommands();
	Commands->MapAction(
		FGraphEditorCommands::Get().CreateComment,
		FExecuteAction::CreateRaw(this, &SNiagaraOverviewGraph::OnCreateComment));

	GraphEditor = SNew(SGraphEditor)
		.AdditionalCommands(Commands)
		.Appearance(AppearanceInfo)
		.TitleBar(TitleBarWidget)
		.GraphToEdit(SystemViewModel->GetEditorData().GetSystemOverviewGraph())
		.GraphEvents(Events);

	//GraphEditor->SetNodeFactory(MakeShared<FNiagaraOverviewGraphNodeFactory>());

	ChildSlot
	[
		SNew(SSplitter)
		+ SSplitter::Slot()
		.Value(.7f)
		[
			GraphEditor.ToSharedRef()
		]
		+ SSplitter::Slot()
		.Value(.3)
		[
			SNew(SBox)
			//SNew(SNiagaraStack, SystemViewModel->GetOverviewSelectionViewModel()->GetOverviewStackViewModel())
		]
	];

	//SystemViewModel->GetOverviewSelectionViewModel()->OnSelectionChanged().AddSP(this, &SNiagaraOverviewGraph::OverviewSelectionChanged);
}

UNiagaraStackEntry* GetStackEntryForSelectedNode(UObject* SelectedNode, TSharedPtr<FNiagaraSystemViewModel> SystemViewModel)
{
// 	UNiagaraOverviewNodeStackItem* OverviewStackNode = Cast<UNiagaraOverviewNodeStackItem>(SelectedNode);
// 	if (OverviewStackNode != nullptr)
// 	{
// 		if (OverviewStackNode->GetOwningSystem() != nullptr)
// 		{
// 			UNiagaraStackViewModel* StackViewModel = nullptr;
// 			if (OverviewStackNode->GetEmitterHandleGuid().IsValid())
// 			{
// 				TSharedPtr<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel = SystemViewModel->GetEmitterHandleViewModelById(OverviewStackNode->GetEmitterHandleGuid());
// 				StackViewModel = EmitterHandleViewModel->GetEmitterStackViewModel();
// 			}
// 			else
// 			{
// 				StackViewModel = SystemViewModel->GetSystemStackViewModel();
// 			}
// 			TArray<UNiagaraStackEntry*> RootEntries = StackViewModel->GetRootEntries();
// 			if (ensureMsgf(RootEntries.Num() == 1, TEXT("Only one root entry expected")))
// 			{
// 				return RootEntries[0];
// 			}
// 		}
// 	}
	return nullptr;
}

void SNiagaraOverviewGraph::GraphSelectionChanged(const TSet<UObject*>& SelectedNodes)
{
// 	if (bUpdatingGraphSelectionFromOverview == false)
// 	{
// 		TGuardValue<bool> UpdateGuard(bUpdatingOverviewSelectionFromGraph, true);
// 		TArray<UNiagaraStackEntry*> SelectedEntries;
// 		for (UObject* SelectedNode : SelectedNodes)
// 		{
// 			UNiagaraStackEntry* SelectedEntry = GetStackEntryForSelectedNode(SelectedNode, SystemViewModel);
// 			if(SelectedEntry != nullptr)
// 			{
// 				SelectedEntries.Add(SelectedEntry);
// 			}
// 		}
// 		bool bClearCurrentSelection = FSlateApplication::Get().GetModifierKeys().IsControlDown() == false;
// 		SystemViewModel->GetOverviewSelectionViewModel()->UpdateSelectedEntries(SelectedEntries, bClearCurrentSelection);
// 	}
}

void SNiagaraOverviewGraph::OverviewSelectionChanged()
{
// 	if (bUpdatingOverviewSelectionFromGraph == false)
// 	{
// 		TGuardValue<bool> UpdateGuard(bUpdatingGraphSelectionFromOverview, true);
// 
// 		TSet<UObject*> SelectedNodeObjects = GraphEditor->GetSelectedNodes();
// 		TArray<UNiagaraStackEntry*> SelectedOverviewEntries = SystemViewModel->GetOverviewSelectionViewModel()->GetSelectedEntries();
// 
// 		TArray<UEdGraphNode*> NodesToDeselect;
// 		for (UObject* SelectedNodeObject : SelectedNodeObjects)
// 		{
// 			UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(SelectedNodeObject);
// 			if (SelectedNode != nullptr)
// 			{
// 				UNiagaraStackEntry* SelectedEntry = GetStackEntryForSelectedNode(SelectedNode, SystemViewModel);
// 				if (SelectedEntry != nullptr && SelectedOverviewEntries.Contains(SelectedEntry) == false)
// 				{
// 					NodesToDeselect.Add(SelectedNode);
// 				}
// 			}
// 		}
// 
// 		for (UEdGraphNode* NodeToDeselect : NodesToDeselect)
// 		{
// 			GraphEditor->SetNodeSelection(NodeToDeselect, false);
// 		}
// 	}
}

FActionMenuContent SNiagaraOverviewGraph::OnCreateGraphActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	if (SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::SystemAsset)
	{
		FMenuBuilder MenuBuilder(true, NULL);
		MenuBuilder.BeginSection(TEXT("NiagaraOverview_EditGraph"), LOCTEXT("EditGraph", "Edit Graph"));

		MenuBuilder.AddSubMenu(
			LOCTEXT("EmitterAddLabel", "Add Emitter"),
			LOCTEXT("EmitterAddToolTip", "Add an existing emitter"),
			FNewMenuDelegate::CreateSP(this, &SNiagaraOverviewGraph::CreateAddEmitterMenuContent, InGraph));

		MenuBuilder.AddMenuEntry(
			LOCTEXT("CommentsLabel", "Add Comment"),
			LOCTEXT("CommentsToolTip", "Add a comment box"),
			FSlateIcon(),
			FExecuteAction::CreateSP(this, &SNiagaraOverviewGraph::OnCreateComment));

		MenuBuilder.EndSection();
		TSharedRef<SWidget> ActionMenu = MenuBuilder.MakeWidget();

		return FActionMenuContent(ActionMenu, ActionMenu);
	}
	return FActionMenuContent(SNullWidget::NullWidget, SNullWidget::NullWidget);
}

void SNiagaraOverviewGraph::OnCreateComment()
{
	FNiagaraSchemaAction_NewComment CommentAction = FNiagaraSchemaAction_NewComment(GraphEditor);
	CommentAction.PerformAction(SystemViewModel->GetEditorData().GetSystemOverviewGraph(), nullptr, GraphEditor->GetPasteLocation(), false);
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

		if (NodeBeingChanged->IsA(UNiagaraOverviewNodeStackItem::StaticClass())) //@TODO System Overview: renaming system or emitters locally through this view
		{
			UNiagaraOverviewNodeStackItem* OverviewNodeBeingChanged = Cast<UNiagaraOverviewNodeStackItem>(NodeBeingChanged);
			TSharedPtr<FNiagaraEmitterHandleViewModel> NodeEmitterHandleViewModel = SystemViewModel->GetEmitterHandleViewModelById(OverviewNodeBeingChanged->GetEmitterHandleGuid());
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
			SystemViewModel->AddEmitterFromAssetData(AssetData);
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

#undef LOCTEXT_NAMESPACE // "NiagaraOverviewGraph"
