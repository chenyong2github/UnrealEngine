// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraScriptGraph.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeInput.h"
#include "Toolkits/AssetEditorManager.h"
#include "GraphEditor.h"
#include "EditorStyleSet.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Framework/Text/TextLayout.h"
#include "Widgets/Notifications/SErrorText.h"
#include "Widgets/Text/STextBlock.h"
#include "NiagaraEditorSettings.h"
#include "EdGraphSchema_Niagara.h"
#include "ScopedTransaction.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraNodeFactory.h"
#include "NiagaraEditorCommands.h"
#include "NiagaraNodeOutput.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Layout/WidgetPath.h"
#include "Framework/Application/SlateApplication.h"
#include "GraphEditorActions.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptGraph"

void SNiagaraScriptGraph::Construct(const FArguments& InArgs, TSharedRef<FNiagaraScriptGraphViewModel> InViewModel)
{
	ViewModel = InViewModel;
	ViewModel->GetNodeSelection()->OnSelectedObjectsChanged().AddSP(this, &SNiagaraScriptGraph::ViewModelSelectedNodesChanged);
	ViewModel->OnNodesPasted().AddSP(this, &SNiagaraScriptGraph::NodesPasted);
	ViewModel->OnGraphChanged().AddSP(this, &SNiagaraScriptGraph::GraphChanged);
	bUpdatingGraphSelectionFromViewModel = false;

	GraphTitle = InArgs._GraphTitle;

	GraphEditor = ConstructGraphEditor();

	ChildSlot
	[
		GraphEditor.ToSharedRef()
	];

	CurrentFocusedSearchMatchIndex = 0;
}

TSharedRef<SGraphEditor> SNiagaraScriptGraph::ConstructGraphEditor()
{
	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText", "NIAGARA");

	const FSearchBoxStyle& Style = FCoreStyle::Get().GetWidgetStyle<FSearchBoxStyle>("SearchBox");

	TSharedRef<SWidget> TitleBarWidget =
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush(TEXT("Graph.TitleBackground")))
		.HAlign(HAlign_Fill)
		[
			SNew(SOverlay)
			+SOverlay::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 3.0f, 0.0f)
				[
					SNew(SErrorText)
					.Visibility(ViewModel.ToSharedRef(), &FNiagaraScriptGraphViewModel::GetGraphErrorTextVisible)
					.BackgroundColor(ViewModel.ToSharedRef(), &FNiagaraScriptGraphViewModel::GetGraphErrorColor)
					.ToolTipText(ViewModel.ToSharedRef(), &FNiagaraScriptGraphViewModel::GetGraphErrorMsgToolTip)
					.ErrorText(ViewModel->GetGraphErrorText())
				]
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(ViewModel.ToSharedRef(), &FNiagaraScriptGraphViewModel::GetDisplayName)
					.TextStyle(FEditorStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
					.Justification(ETextJustify::Center)
				]
			]
			+SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Fill)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				.MaxWidth(400.0f) // Limit max search box width to avoid extending over titlebar
				[
					SAssignNew(SearchBox, SSearchBox)
					.HintText(LOCTEXT("GraphSearchBoxHint", "Search Nodes and Pins in Graph"))
					.SearchResultData(this, &SNiagaraScriptGraph::GetSearchResultData)
					.OnTextChanged(this, &SNiagaraScriptGraph::OnSearchTextChanged)
					.OnTextCommitted(this, &SNiagaraScriptGraph::OnSearchBoxTextCommitted)
					.DelayChangeNotificationsWhileTyping(true)
					.OnSearch(this, &SNiagaraScriptGraph::OnSearchBoxSearch)
					.Visibility(this, &SNiagaraScriptGraph::GetGraphSearchBoxVisibility)
					.OnKeyDownHandler(this, &SNiagaraScriptGraph::HandleGraphSearchBoxKeyDown)
				]
				+SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					SNew(SBorder)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Fill)
					.BorderImage(&Style.TextBoxStyle.BackgroundImageHovered)
					.BorderBackgroundColor(Style.TextBoxStyle.BackgroundColor)
					.ForegroundColor(Style.TextBoxStyle.ForegroundColor)
					.Padding(0)
					[
						SNew(SButton)
						.ButtonStyle(FCoreStyle::Get(), "NoBorder")
						.ContentPadding(0)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.IsFocusable(false)
						.ToolTipText(LOCTEXT("CloseGraphSearchBox", "Close Graph search box"))
						.Visibility(this, &SNiagaraScriptGraph::GetGraphSearchBoxVisibility)
						.OnClicked(this, &SNiagaraScriptGraph::CloseGraphSearchBoxPressed)
						.Content()
						[
 							SNew(SImage)
 							.Image(FEditorStyle::GetBrush("Symbols.X"))
							.ColorAndOpacity(FSlateColor::UseForeground())
						]
					]
				]
			]
		];

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SNiagaraScriptGraph::GraphEditorSelectedNodesChanged);
	Events.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &SNiagaraScriptGraph::OnNodeDoubleClicked);
	Events.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &SNiagaraScriptGraph::OnNodeTitleCommitted);
	Events.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &SNiagaraScriptGraph::OnVerifyNodeTextCommit);
	Events.OnSpawnNodeByShortcut = SGraphEditor::FOnSpawnNodeByShortcut::CreateSP(this, &SNiagaraScriptGraph::OnSpawnGraphNodeByShortcut);

	Commands = MakeShared<FUICommandList>();
	Commands->Append(ViewModel->GetCommands());
	Commands->MapAction(
		FNiagaraEditorCommands::Get().FindInCurrentView,
		FExecuteAction::CreateRaw(this, &SNiagaraScriptGraph::FocusGraphSearchBox));
	Commands->MapAction(
		FGraphEditorCommands::Get().CreateComment,
		FExecuteAction::CreateRaw(this, &SNiagaraScriptGraph::OnCreateComment));
	
	TSharedRef<SGraphEditor> CreatedGraphEditor = SNew(SGraphEditor)
		.AdditionalCommands(Commands.ToSharedRef())
		.Appearance(AppearanceInfo)
		.TitleBar(TitleBarWidget)
		.GraphToEdit(ViewModel->GetGraph())
		.GraphEvents(Events);

	// Set a niagara node factory.
	CreatedGraphEditor->SetNodeFactory(MakeShareable(new FNiagaraNodeFactory()));

	return CreatedGraphEditor;
}

void SNiagaraScriptGraph::ViewModelSelectedNodesChanged()
{
	if (FNiagaraEditorUtilities::SetsMatch(GraphEditor->GetSelectedNodes(), ViewModel->GetNodeSelection()->GetSelectedObjects()) == false)
	{
		bUpdatingGraphSelectionFromViewModel = true;
		GraphEditor->ClearSelectionSet();
		for (UObject* SelectedNode : ViewModel->GetNodeSelection()->GetSelectedObjects())
		{
			UEdGraphNode* GraphNode = Cast<UEdGraphNode>(SelectedNode);
			if (GraphNode != nullptr)
			{
				GraphEditor->SetNodeSelection(GraphNode, true);
			}
		}
		bUpdatingGraphSelectionFromViewModel = false;
	}
}

void SNiagaraScriptGraph::GraphEditorSelectedNodesChanged(const TSet<UObject*>& SelectedNodes)
{
	if (bUpdatingGraphSelectionFromViewModel == false)
	{
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

void SNiagaraScriptGraph::OnNodeDoubleClicked(UEdGraphNode* ClickedNode)
{
	UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(ClickedNode);
	if (NiagaraNode != nullptr)
	{
		UObject* ReferencedAsset = NiagaraNode->GetReferencedAsset();
		if (ReferencedAsset != nullptr)
		{
			FAssetEditorManager::Get().OpenEditorForAsset(ReferencedAsset);
		}
	}
}

void SNiagaraScriptGraph::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{
	if (NodeBeingChanged)
	{
		// When you request rename on spawn but accept the value, we want to not add a transaction if they just hit "Enter".
		bool bRename = true;
		if (NodeBeingChanged->IsA(UNiagaraNodeInput::StaticClass()))
		{
			FName CurrentName = Cast<UNiagaraNodeInput>(NodeBeingChanged)->Input.GetName();
			if (CurrentName.ToString().Equals(NewText.ToString(), ESearchCase::CaseSensitive))
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

bool SNiagaraScriptGraph::OnVerifyNodeTextCommit(const FText& NewText, UEdGraphNode* NodeBeingChanged, FText& OutErrorMessage)
{
	bool bValid = true;
	UNiagaraNodeInput* InputNodeBeingChanged = Cast<UNiagaraNodeInput>(NodeBeingChanged);
	if (InputNodeBeingChanged != nullptr)
	{
		return FNiagaraEditorUtilities::VerifyNameChangeForInputOrOutputNode(*InputNodeBeingChanged, InputNodeBeingChanged->Input.GetName(), *NewText.ToString(), OutErrorMessage);
	}
	return bValid;
}

FReply SNiagaraScriptGraph::OnSpawnGraphNodeByShortcut(FInputChord InChord, const FVector2D& InPosition)
{
	if (ViewModel.Get() == nullptr)
	{
		return FReply::Unhandled();
	}
	UNiagaraGraph* Graph = ViewModel->GetGraph();
	if (Graph == nullptr)
	{
		return FReply::Unhandled();
	}

	const UNiagaraEditorSettings* Settings = GetDefault<UNiagaraEditorSettings>();
	if (Settings == nullptr)
	{
		return FReply::Unhandled();
	}

	for (int32 i = 0; i < Settings->GraphCreationShortcuts.Num(); i++)
	{
		if (Settings->GraphCreationShortcuts[i].Input.GetRelationship(InChord) == FInputChord::Same)
		{
			const UEdGraphSchema_Niagara* Schema = GetDefault<UEdGraphSchema_Niagara>();

			UEdGraph* OwnerOfTemporaries = NewObject<UEdGraph>((UObject*)GetTransientPackage());
			TArray<UObject*> SelectedObjects;
			TArray<TSharedPtr<FNiagaraSchemaAction_NewNode> > Actions = Schema->GetGraphContextActions(Graph, SelectedObjects, nullptr, OwnerOfTemporaries);

			for (int32 ActionIdx = 0; ActionIdx < Actions.Num(); ActionIdx++)
			{
				TSharedPtr<FNiagaraSchemaAction_NewNode> NiagaraAction = Actions[ActionIdx];
				if (!NiagaraAction.IsValid())
				{
					continue;
				}

				bool bMatch = false;
				if (NiagaraAction->InternalName.ToString().Equals(Settings->GraphCreationShortcuts[i].Name, ESearchCase::IgnoreCase))
				{
					bMatch = true;
				}
				if (!bMatch && NiagaraAction->GetMenuDescription().ToString().Equals(Settings->GraphCreationShortcuts[i].Name, ESearchCase::IgnoreCase))
				{
					bMatch = true;
				}
				if (bMatch)
				{
					FScopedTransaction Transaction(LOCTEXT("AddNode", "Add Node"));
					TArray<UEdGraphPin*> Pins;
					NiagaraAction->PerformAction(Graph, Pins, InPosition);
					return FReply::Handled();
				}					
			}
		}
	}

	return FReply::Unhandled();

	/*
	if (Graph == nullptr)
	{
	return FReply::Handled();
	}

	FScopedTransaction Transaction(LOCTEXT("AddNode", "Add Node"));

	TArray<UEdGraphNode*> OutNodes;
	FVector2D NodeSpawnPos = InPosition;
	FBlueprintSpawnNodeCommands::Get().GetGraphActionByChord(InChord, InGraph, NodeSpawnPos, OutNodes);

	TSet<const UEdGraphNode*> NodesToSelect;

	for (UEdGraphNode* CurrentNode : OutNodes)
	{
	NodesToSelect.Add(CurrentNode);
	}

	// Do not change node selection if no actions were performed
	if(OutNodes.Num() > 0)
	{
	Graph->SelectNodeSet(NodesToSelect, true);
}
	else
	{
		Transaction.Cancel();
	}

	return FReply::Handled();
	*/
}

void SNiagaraScriptGraph::NodesPasted(const TSet<UEdGraphNode*>& PastedNodes)
{
	if (PastedNodes.Num() != 0)
	{
		PositionPastedNodes(PastedNodes);
		GraphEditor->NotifyGraphChanged();
	}
}

void SNiagaraScriptGraph::PositionPastedNodes(const TSet<UEdGraphNode*>& PastedNodes)
{
	FVector2D AvgNodePosition(0.0f, 0.0f);

	for (UEdGraphNode* PastedNode : PastedNodes)
	{
		AvgNodePosition.X += PastedNode->NodePosX;
		AvgNodePosition.Y += PastedNode->NodePosY;
	}

	float InvNumNodes = 1.0f / float(PastedNodes.Num());
	AvgNodePosition.X *= InvNumNodes;
	AvgNodePosition.Y *= InvNumNodes;

	FVector2D PasteLocation = GraphEditor->GetPasteLocation();
	for (UEdGraphNode* PastedNode : PastedNodes)
	{
		PastedNode->NodePosX = (PastedNode->NodePosX - AvgNodePosition.X) + PasteLocation.X;
		PastedNode->NodePosY = (PastedNode->NodePosY - AvgNodePosition.Y) + PasteLocation.Y;

		PastedNode->SnapToGrid(16);
	}
}

void SNiagaraScriptGraph::GraphChanged()
{
	TSharedPtr<SGraphEditor> NewGraphEditor;
	TSharedPtr<SWidget> NewChildWidget;

	if (ViewModel->GetGraph() != nullptr)
	{
		NewGraphEditor = ConstructGraphEditor();
		NewChildWidget = NewGraphEditor;
	}
	else
	{
		NewGraphEditor = nullptr;
		NewChildWidget = SNullWidget::NullWidget;
	}
	
	GraphEditor = NewGraphEditor;
	ChildSlot
	[
		NewChildWidget.ToSharedRef()
	];
}

void SNiagaraScriptGraph::FocusGraphElement(const INiagaraScriptGraphFocusInfo* FocusInfo)
{
	checkf(FocusInfo->GetFocusType() != INiagaraScriptGraphFocusInfo::ENiagaraScriptGraphFocusInfoType::None, TEXT("Failed to assign focus type to FocusInfo parameter!"));

	if (FocusInfo->GetFocusType() == INiagaraScriptGraphFocusInfo::ENiagaraScriptGraphFocusInfoType::Node)
	{
		const FNiagaraScriptGraphNodeToFocusInfo* NodeFocusInfo = static_cast<const FNiagaraScriptGraphNodeToFocusInfo*>(FocusInfo);
		const FGuid& NodeGuidToMatch = NodeFocusInfo->GetNodeGuidToFocus();
		UEdGraphNode* const* NodeToFocus = ViewModel->GetGraph()->Nodes.FindByPredicate([&NodeGuidToMatch](const UEdGraphNode* Node) {return Node->NodeGuid == NodeGuidToMatch; });
		if (NodeToFocus != nullptr && *NodeToFocus != nullptr)
		{
			GetGraphEditor()->JumpToNode(*NodeToFocus);
			return;
		}
		ensureMsgf(false, TEXT("Failed to find Node with matching GUID when focusing graph element. Was the graph edited out from underneath us?"));
		return;
	}
	else if (FocusInfo->GetFocusType() == INiagaraScriptGraphFocusInfo::ENiagaraScriptGraphFocusInfoType::Pin)
	{
		const FNiagaraScriptGraphPinToFocusInfo* PinFocusInfo = static_cast<const FNiagaraScriptGraphPinToFocusInfo*>(FocusInfo);
		const FGuid& PinGuidToMatch = PinFocusInfo->GetPinGuidToFocus();
		for (const UEdGraphNode* Node : ViewModel->GetGraph()->Nodes)
		{
			const UEdGraphPin* const* PinToFocus = Node->Pins.FindByPredicate([&PinGuidToMatch](const UEdGraphPin* Pin) {return Pin->PersistentGuid == PinGuidToMatch; });
			if (PinToFocus != nullptr && *PinToFocus != nullptr)
			{
				GetGraphEditor()->JumpToPin(*PinToFocus);
				return;
			}
		}
		ensureMsgf(false, TEXT("Failed to find Pin with matching GUID when focusing graph element. Was the graph edited out from underneath us?"));
		return;
	}
	checkf(false, TEXT("Requested focus for a graph element without specifying a Node or Pin to focus!"));
}

void SNiagaraScriptGraph::OnSearchTextChanged(const FText& SearchText)
{
	if (!CurrentSearchText.EqualTo(SearchText))
	{
		CurrentSearchResults.Empty();
		CurrentSearchText = SearchText;
		TArray<UNiagaraNode*> Nodes;
		ViewModel->GetGraph()->GetNodesOfClass<UNiagaraNode>(Nodes);
		for (UNiagaraNode* Node : Nodes)
		{
			if (Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString().Contains(SearchText.ToString()))
			{	
				CurrentSearchResults.Add(MakeShared<FNiagaraScriptGraphNodeToFocusInfo>(Node->NodeGuid));
			}
			
			if (Node->IsA<UNiagaraNodeOutput>() == false) 
			{
				for (UEdGraphPin* Pin : Node->GetAllPins())
				{
					if (Pin->GetDisplayName().ToString().Contains(SearchText.ToString()))
					{
						CurrentSearchResults.Add(MakeShared<FNiagaraScriptGraphPinToFocusInfo>(Pin->PersistentGuid));
					}
				}
			}
		}

		CurrentFocusedSearchMatchIndex = 0;
		if (CurrentSearchResults.Num() > 0)
		{
			FocusGraphElement(CurrentSearchResults[0].Get());
		}
	}
}

void SNiagaraScriptGraph::OnSearchBoxTextCommitted(const FText& NewText, ETextCommit::Type CommitInfo)
{
	if (SearchBox->HasKeyboardFocus())
	{
		OnSearchBoxSearch(SSearchBox::Next);
	}
}

TOptional<SSearchBox::FSearchResultData> SNiagaraScriptGraph::GetSearchResultData() const
{
	if (CurrentSearchText.IsEmpty() || CurrentSearchResults.Num() == 0)
	{
		return TOptional<SSearchBox::FSearchResultData>();
	}
	return TOptional<SSearchBox::FSearchResultData>({ CurrentSearchResults.Num(), CurrentFocusedSearchMatchIndex + 1 });
}

FReply SNiagaraScriptGraph::CloseGraphSearchBoxPressed()
{
	bGraphSearchBoxActive = false;
	return FReply::Handled();
}

FReply SNiagaraScriptGraph::HandleGraphSearchBoxKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		CloseGraphSearchBoxPressed();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

void SNiagaraScriptGraph::FocusGraphSearchBox()
{
	bGraphSearchBoxActive = true;

	if (SearchBox.IsValid())
	{
		FWidgetPath WidgetToFocusPath;
		FSlateApplication::Get().GeneratePathToWidgetUnchecked(SearchBox.ToSharedRef(), WidgetToFocusPath, EVisibility::All);
		FSlateApplication::Get().SetKeyboardFocus(WidgetToFocusPath, EFocusCause::SetDirectly);
	}
}

void SNiagaraScriptGraph::OnCreateComment()
{
	FNiagaraSchemaAction_NewComment CommentAction = FNiagaraSchemaAction_NewComment(GraphEditor);
	CommentAction.PerformAction(ViewModel->GetGraph(), nullptr, GraphEditor->GetPasteLocation(), false);
}

void SNiagaraScriptGraph::OnSearchBoxSearch(SSearchBox::SearchDirection Direction)
{
	if (CurrentSearchResults.Num() > 0)
	{
		if (Direction == SSearchBox::Next)
		{
			CurrentFocusedSearchMatchIndex = CurrentFocusedSearchMatchIndex < CurrentSearchResults.Num() - 1 ? CurrentFocusedSearchMatchIndex + 1 : 0;
			FocusGraphElement(CurrentSearchResults[CurrentFocusedSearchMatchIndex].Get());
		}
		else if (Direction == SSearchBox::Previous)
		{
			CurrentFocusedSearchMatchIndex = CurrentFocusedSearchMatchIndex > 0 ? CurrentFocusedSearchMatchIndex - 1 : CurrentSearchResults.Num() - 1;
			FocusGraphElement(CurrentSearchResults[CurrentFocusedSearchMatchIndex].Get());
		}
	}
}

#undef LOCTEXT_NAMESPACE // "NiagaraScriptGraph"
