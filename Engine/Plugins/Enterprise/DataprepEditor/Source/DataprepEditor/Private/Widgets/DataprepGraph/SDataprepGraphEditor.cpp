// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/DataprepGraph/SDataprepGraphEditor.h"

#include "DataprepAsset.h"
#include "DataprepEditorLogCategory.h"
#include "DataprepEditorStyle.h"
#include "DataprepGraph/DataprepGraph.h"
#include "DataprepGraph/DataprepGraphSchema.h"
#include "DataprepGraph/DataprepGraphActionNode.h"
#include "SchemaActions/DataprepAllMenuActionCollector.h"
#include "SchemaActions/DataprepDragDropOp.h"
#include "SchemaActions/IDataprepMenuActionCollector.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphActionStepNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"
#include "Widgets/DataprepWidgets.h"
#include "Widgets/SDataprepActionMenu.h"

#include "EdGraph/EdGraphSchema.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphUtilities.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "ScopedTransaction.h"
#include "SGraphPanel.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

#ifdef ACTION_NODE_MOCKUP
static int32 MockupActionCount = 2;
#endif

const float SDataprepGraphEditor::TopPadding = 60.f;
const float SDataprepGraphEditor::BottomPadding = 15.f;
const float SDataprepGraphEditor::HorizontalPadding = 20.f;

TSharedPtr<SDataprepGraphEditorNodeFactory> SDataprepGraphEditor::NodeFactory;

TSharedPtr<class SGraphNode> SDataprepGraphEditorNodeFactory::CreateNode(UEdGraphNode* Node) const
{
	if (UDataprepGraphRecipeNode* RecipeNode = Cast<UDataprepGraphRecipeNode>(Node))
	{
		return SNew(SDataprepGraphTrackNode, RecipeNode);
	}
	else if (UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(Node))
	{
		return SNew(SDataprepGraphActionNode, ActionNode);
	}

	return nullptr;
}

void SDataprepGraphEditor::RegisterFactories()
{
	if(!NodeFactory.IsValid())
	{
		NodeFactory  = MakeShareable( new SDataprepGraphEditorNodeFactory() );
		FEdGraphUtilities::RegisterVisualNodeFactory(NodeFactory);
	}
}

void SDataprepGraphEditor::UnRegisterFactories()
{
	if(NodeFactory.IsValid())
	{
		FEdGraphUtilities::UnregisterVisualNodeFactory(NodeFactory);
		NodeFactory.Reset();
	}
}

void SDataprepGraphEditor::Construct(const FArguments& InArgs, UDataprepAsset* InDataprepAsset)
{
	check(InDataprepAsset);
	DataprepAssetPtr = InDataprepAsset;

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnCreateNodeOrPinMenu = SGraphEditor::FOnCreateNodeOrPinMenu::CreateSP(this, &SDataprepGraphEditor::OnCreateNodeOrPinMenu);
	Events.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &SDataprepGraphEditor::OnCreateActionMenu);
	Events.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &SDataprepGraphEditor::OnNodeVerifyTitleCommit);
	Events.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &SDataprepGraphEditor::OnNodeTitleCommitted);

	BuildCommandList();

	SGraphEditor::FArguments Arguments;
	Arguments._AdditionalCommands = GraphEditorCommands;
	Arguments._TitleBar = InArgs._TitleBar;
	Arguments._GraphToEdit = InArgs._GraphToEdit;
	Arguments._GraphEvents = Events;

	SGraphEditor::Construct( Arguments );

	DataprepAssetPtr->GetOnActionChanged().AddSP(this, &SDataprepGraphEditor::OnDataprepAssetActionChanged);

	// #ueent_toremove: Temp code for the nodes development
	if(UBlueprint* RecipeBP = InDataprepAsset->GetRecipeBP())
	{
		RecipeBP->OnChanged().AddSP(this, &SDataprepGraphEditor::OnPipelineChanged);
	}
	// end of temp code for nodes development

	SetCanTick(true);

	bIsComplete = false;
	bMustRearrange = false;

	LastLocalSize = FVector2D::ZeroVector;
	LastLocation = FVector2D( 0.f, -TopPadding );
	LastZoomAmount = 1.f;

	FModifierKeysState ModifierKeyState = FSlateApplication::Get().GetModifierKeys();
	bCachedControlKeyDown = ModifierKeyState.IsControlDown() || ModifierKeyState.IsCommandDown();
}

// #ueent_toremove: Temp code for the nodes development
void SDataprepGraphEditor::OnPipelineChanged(UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		TrackGraphNodePtr.Reset();
		bIsComplete = false;
		NotifyGraphChanged();

		LastLocalSize = FVector2D::ZeroVector;
		//LastLocation = FVector2D( BIG_NUMBER );
		LastZoomAmount = 1.f;
	}
}

void SDataprepGraphEditor::OnDataprepAssetActionChanged(UObject* InObject, FDataprepAssetChangeType ChangeType)
{
	switch(ChangeType)
	{
		case FDataprepAssetChangeType::ActionAdded:
		case FDataprepAssetChangeType::ActionRemoved:
		{
			TArray<UEdGraphNode*> ToDelete;
			TArray<UEdGraphNode*>& Nodes = GetCurrentGraph()->Nodes;
			for(UEdGraphNode* NodeObject : Nodes)
			{
				if (UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(NodeObject))
				{
					ToDelete.Add(NodeObject);
				}
			}

			for(UEdGraphNode* NodeObject : ToDelete)
			{
				Nodes.Remove(NodeObject);
			}

			TrackGraphNodePtr.Reset();
			bIsComplete = false;
			NotifyGraphChanged();

			LastLocalSize = FVector2D::ZeroVector;
			LastLocation = FVector2D::ZeroVector;
			LastZoomAmount = 1.f;
			break;
		}

		case FDataprepAssetChangeType::ActionMoved:
		{
			if(SDataprepGraphTrackNode* TrackGraphNode = TrackGraphNodePtr.Pin().Get())
			{
				TrackGraphNode->OnActionsOrderChanged();
			}
			break;
		}
	}
}

void SDataprepGraphEditor::CacheDesiredSize(float InLayoutScaleMultiplier)
{
	SGraphEditor::CacheDesiredSize(InLayoutScaleMultiplier);

	if(!bIsComplete && !NeedsPrepass())
	{
		if(!TrackGraphNodePtr.IsValid())
		{
			// Get track SGraphNode and initialize it
			if(UDataprepAsset* DataprepAsset = DataprepAssetPtr.Get())
			{
				for(UEdGraphNode* EdGraphNode : GetCurrentGraph()->Nodes)
				{
					if(UDataprepGraphRecipeNode* TrackNode = Cast<UDataprepGraphRecipeNode>(EdGraphNode))
					{
						TrackGraphNodePtr = StaticCastSharedPtr<SDataprepGraphTrackNode>(TrackNode->GetWidget());
						break;
					}
				}
			}
		}

		if(TrackGraphNodePtr.IsValid())
		{
			bIsComplete = TrackGraphNodePtr.Pin()->RefreshLayout();
			bMustRearrange = true;
			// Force a change of viewpoint to update the canvas.
			SetViewLocation(FVector2D(0.f, -TopPadding), 1.f);
		}
	}
}

void SDataprepGraphEditor::UpdateBoundaries(const FVector2D& LocalSize, float ZoomAmount)
{
	if(SDataprepGraphTrackNode* TrackGraphNode = TrackGraphNodePtr.Pin().Get())
	{
		CachedTrackNodeSize = TrackGraphNode->Update(LocalSize, ZoomAmount);
	}

	ViewLocationRangeOnY.Set( -TopPadding, -TopPadding );

	const float DesiredVisualHeight = CachedTrackNodeSize.Y * ZoomAmount;
	if(LocalSize.Y < DesiredVisualHeight)
	{
		ViewLocationRangeOnY.Y = DesiredVisualHeight - LocalSize.Y;
	}
}

void SDataprepGraphEditor::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// Do not change the layout until all widgets have been created.
	// This happens after the first call to OnPaint on the editor
	if(bIsComplete)
	{
		if(SDataprepGraphTrackNode* TrackGraphNode = TrackGraphNodePtr.Pin().Get())
		{
			FModifierKeysState ModifierKeyState = FSlateApplication::Get().GetModifierKeys();
			bool bControlKeyDown = ModifierKeyState.IsControlDown() || ModifierKeyState.IsCommandDown();
			if(bControlKeyDown != bCachedControlKeyDown)
			{
				bCachedControlKeyDown = bControlKeyDown;
				TrackGraphNode->OnControlKeyChanged(bCachedControlKeyDown);
			}
		}

		FVector2D Location;
		float ZoomAmount = 1.f;
		GetViewLocation( Location, ZoomAmount );

		UpdateLayout( AllottedGeometry.GetLocalSize(), Location, ZoomAmount );
	}

	SGraphEditor::Tick( AllottedGeometry, InCurrentTime, InDeltaTime);
}

void SDataprepGraphEditor::UpdateLayout( const FVector2D& LocalSize, const FVector2D& Location, float ZoomAmount )
{
	if(LastZoomAmount != ZoomAmount)
	{
		UpdateBoundaries( LocalSize, ZoomAmount );
	}

	if( !LocalSize.Equals(LastLocalSize) )
	{
		bMustRearrange = true;

		UpdateBoundaries( LocalSize, ZoomAmount );

		LastLocalSize = LocalSize;

		// Force a re-compute of the view location
		LastLocation = -Location;
	}

	if( !Location.Equals(LastLocation) )
	{
		FVector2D ComputedLocation( LastLocation );

		if(Location.X != LastLocation.X)
		{
			const float ActualWidth = LocalSize.X / ZoomAmount;
			const float MaxInX = CachedTrackNodeSize.X > ActualWidth ? CachedTrackNodeSize.X - ActualWidth : 0.f;
			ComputedLocation.X = Location.X < 0.f ? 0.f : Location.X >= MaxInX ? MaxInX : Location.X;
		}

		if(Location.Y != LastLocation.Y)
		{
			// Keep same visual Y position if only zoom has changed
			// Assumption: user cannot zoom in or out and move the canvas at the same time
			if(LastZoomAmount != ZoomAmount)
			{
				ComputedLocation.Y = LastLocation.Y * LastZoomAmount / ZoomAmount;
			}
			else
			{
				const float ActualPositionInY = Location.Y * ZoomAmount;
				if(ActualPositionInY <= ViewLocationRangeOnY.X )
				{
					ComputedLocation.Y = ViewLocationRangeOnY.X / ZoomAmount;
				}
				else if( ActualPositionInY > ViewLocationRangeOnY.Y )
				{
					ComputedLocation.Y = ViewLocationRangeOnY.Y / ZoomAmount;
				}
				else
				{
					ComputedLocation.Y = Location.Y;
				}
			}
		}

		LastLocation = Location;

		if(ComputedLocation != Location)
		{
			SetViewLocation( ComputedLocation, ZoomAmount );
			LastLocation = ComputedLocation;
		}
	}

	LastZoomAmount = ZoomAmount;
}

void SDataprepGraphEditor::OnDragEnter(const FGeometry & MyGeometry, const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	{
		// Inform the Drag and Drop operation that we are hovering over this node.
		DragNodeOp->SetGraphPanel(TrackGraphNodePtr.Pin()->GetOwnerPanel());
	}

	SGraphEditor::OnDragEnter(MyGeometry, DragDropEvent);
}

FReply SDataprepGraphEditor::OnDragOver(const FGeometry & MyGeometry, const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	{
		TrackGraphNodePtr.Pin()->OnDragOver(MyGeometry, DragDropEvent);
	}

	return SGraphEditor::OnDragOver(MyGeometry, DragDropEvent);
}

void SDataprepGraphEditor::OnDragLeave(const FDragDropEvent & DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	{
		// Inform the Drag and Drop operation that we are hovering over this node.
		DragNodeOp->SetGraphPanel(TSharedPtr<SGraphPanel>());
	}

	SGraphEditor::OnDragLeave(DragDropEvent);
}

FReply SDataprepGraphEditor::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	{
		//const FVector2D NodeAddPosition = (MyGeometry.AbsoluteToLocal( DragDropEvent.GetScreenSpacePosition() ) / LastZoomAmount) + LastLocation;
		//return DragNodeOp->DroppedOnPanel(SharedThis(this), DragDropEvent.GetScreenSpacePosition(), NodeAddPosition, *GetCurrentGraph()).EndDragDrop();

		return FReply::Handled().EndDragDrop();
	}

	return SGraphEditor::OnDrop(MyGeometry, DragDropEvent);
}

void SDataprepGraphEditor::BuildCommandList()
{
	if (!GraphEditorCommands.IsValid())
	{
		GraphEditorCommands = MakeShareable(new FUICommandList);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Rename,
			FExecuteAction::CreateSP(this, &SDataprepGraphEditor::OnRenameNode),
			FCanExecuteAction::CreateSP(this, &SDataprepGraphEditor::CanRenameNode)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
			FExecuteAction::CreateSP(this, &SDataprepGraphEditor::SelectAllNodes),
			FCanExecuteAction::CreateSP(this, &SDataprepGraphEditor::CanSelectAllNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &SDataprepGraphEditor::DeleteSelectedNodes),
			FCanExecuteAction::CreateSP(this, &SDataprepGraphEditor::CanDeleteNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
			FExecuteAction::CreateSP(this, &SDataprepGraphEditor::CopySelectedNodes),
			FCanExecuteAction::CreateSP(this, &SDataprepGraphEditor::CanCopyNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
			FExecuteAction::CreateSP(this, &SDataprepGraphEditor::CutSelectedNodes),
			FCanExecuteAction::CreateSP(this, &SDataprepGraphEditor::CanCutNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
			FExecuteAction::CreateSP(this, &SDataprepGraphEditor::PasteNodes),
			FCanExecuteAction::CreateSP(this, &SDataprepGraphEditor::CanPasteNodes)
		);

		GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
			FExecuteAction::CreateSP(this, &SDataprepGraphEditor::DuplicateNodes),
			FCanExecuteAction::CreateSP(this, &SDataprepGraphEditor::CanDuplicateNodes)
		);
	}
}

void SDataprepGraphEditor::OnRenameNode()
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(*NodeIt);
		if (SelectedNode != NULL && SelectedNode->bCanRenameNode)
		{
			IsNodeTitleVisible(SelectedNode, true);
			break;
		}
	}
}

bool SDataprepGraphEditor::CanRenameNode() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	if ( SelectedNodes.Num() == 1)
	{
		if ( UDataprepGraphActionNode* SelectedNode = Cast<UDataprepGraphActionNode>(*SelectedNodes.CreateConstIterator()) )
		{
			return SelectedNode->bCanRenameNode;
		}
	}

	return false;
}

void SDataprepGraphEditor::DeleteSelectedNodes()
{
	FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	TArray<int32> ActionsToDelete;
	TSet<UDataprepActionAsset*> ActionAssets;
	for(UObject* NodeObject : SelectedNodes)
	{
		if (UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(NodeObject))
		{
			if (ActionNode->CanUserDeleteNode() && ActionNode->GetDataprepActionAsset())
			{
				ActionsToDelete.Add(ActionNode->GetExecutionOrder());
				ActionAssets.Add(ActionNode->GetDataprepActionAsset());
			}
		}
	}

	UEdGraph* EdGraph = GetCurrentGraph();

	TMap<UDataprepActionAsset*, TArray<int32>> StepsToDelete;
	for(UObject* NodeObject : SelectedNodes)
	{
		if (UDataprepGraphActionStepNode* ActionStepNode = Cast<UDataprepGraphActionStepNode>(NodeObject))
		{
			UDataprepActionAsset* ActionAsset = ActionStepNode->GetDataprepActionAsset();
			if (ActionStepNode->CanUserDeleteNode() && ActionAsset && !ActionAssets.Contains(ActionAsset))
			{
				TArray<int32>& ToDelete = StepsToDelete.FindOrAdd(ActionAsset);
				ToDelete.Add(ActionStepNode->GetStepIndex());

				// Delete action if all its steps are deleted
				if(ActionAsset->GetStepsCount() == ToDelete.Num())
				{
					StepsToDelete.Remove(ActionAsset);
					int32 Index = DataprepAssetPtr->GetActionIndex(ActionAsset);
					ensure(Index != INDEX_NONE);
					ActionsToDelete.Add(Index);

					TArray<class UEdGraphNode*>& Nodes = EdGraph->Nodes;
					for(Index = 0; Index < Nodes.Num(); ++Index)
					{
						if(UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(Nodes[Index]))
						{
							if(ActionNode->GetDataprepActionAsset() == ActionAsset)
							{
								SelectedNodes.Add(Nodes[Index]);
								break;
							}
						}
					}
				}
			}
		}
	}

	FScopedTransaction Transaction(FGenericCommands::Get().Delete->GetDescription());
	bool bTransactionSuccessful = true;

	if(ActionsToDelete.Num() > 0)
	{
		bTransactionSuccessful &= DataprepAssetPtr->RemoveActions(ActionsToDelete);
	}

	if(StepsToDelete.Num() > 0)
	{
		for(TPair<UDataprepActionAsset*, TArray<int32>>& Entry : StepsToDelete)
		{
			if(UDataprepActionAsset* ActionAsset = Entry.Key)
			{
				bTransactionSuccessful &= ActionAsset->RemoveSteps(Entry.Value);
			}
		}
	}

	TArray<UEdGraphNode*>& Nodes = EdGraph->Nodes;
	for(UObject* NodeObject : SelectedNodes)
	{
		if (UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(NodeObject))
		{
			Nodes.Remove(ActionNode);
		}
	}

	if (!bTransactionSuccessful)
	{
		Transaction.Cancel();
	}
}

bool SDataprepGraphEditor::CanDeleteNodes() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	for (UObject* NodeObject : SelectedNodes)
	{
		// If any nodes allow deleting, then do not disable the delete option
		UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObject);
		if (Node && Node->CanUserDeleteNode())
		{
			return true;
			break;
		}
	}

	return false;
}

void SDataprepGraphEditor::CopySelectedNodes()
{
	// Export the selected nodes and place the text on the clipboard
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	TSet<UObject*> ActionsToCopy;

	for (UObject* NodeObject : SelectedNodes)
	{
		if (UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(NodeObject))
		{
			UDataprepActionAsset* ActionAsset = ActionNode->GetDataprepActionAsset();
			// Temporarily set DataprepActionAsset's owner as ActionNode to serialize it with the EdGraphNode
			ActionAsset->Rename(nullptr, ActionNode, REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);

			ActionNode->PrepareForCopying();
			ActionsToCopy.Add(ActionNode);
		}
	}

	if(ActionsToCopy.Num() > 0)
	{
		FString ExportedText;

		FEdGraphUtilities::ExportNodesToText(ActionsToCopy, /*out*/ ExportedText);
		FPlatformApplicationMisc::ClipboardCopy(*ExportedText);

		// Restore DataprepActionAssets' owner to the DataprepAsset
		for (UObject* NodeObject : ActionsToCopy)
		{
			if (UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(NodeObject))
			{
				UDataprepActionAsset* ActionAsset = ActionNode->GetDataprepActionAsset();
				ActionAsset->Rename(nullptr, DataprepAssetPtr.Get(), REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			}
		}
	}
}

bool SDataprepGraphEditor::CanCopyNodes() const
{
	// If any of the nodes can be duplicated then we should allow copying
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (UObject* NodeObject : SelectedNodes)
	{
		UDataprepGraphActionNode* Node = Cast<UDataprepGraphActionNode>(NodeObject);
		if ((Node != NULL) && Node->CanDuplicateNode())
		{
			return true;
		}
	}
	return false;
}

void SDataprepGraphEditor::CutSelectedNodes()
{
	CopySelectedNodes();
	// Cut should only delete nodes that can be duplicated
	DeleteSelectedDuplicatableNodes();
}

bool SDataprepGraphEditor::CanCutNodes() const
{
	return CanCopyNodes() && CanDeleteNodes();
}

void SDataprepGraphEditor::PasteNodes()
{
	ClearSelectionSet();

	// Grab the text to paste from the clipboard.
	FString TextToImport;
	FPlatformApplicationMisc::ClipboardPaste(TextToImport);

	// Create temporary graph
	FName UniqueGraphName = MakeUniqueObjectName( GetTransientPackage(), UWorld::StaticClass(), FName( *(LOCTEXT("DataprepGraph", "TempGraph").ToString()) ) );
	TStrongObjectPtr<UDataprepGraph> DataprepGraph = TStrongObjectPtr<UDataprepGraph>( NewObject< UDataprepGraph >(GetTransientPackage(), UniqueGraphName) );
	DataprepGraph->Schema = UDataprepGraphSchema::StaticClass();

	// Import the nodes
	// #ueent_wip: The FEdGraphUtilities::ImportNodesFromTex could be replaced
	TSet<UEdGraphNode*> PastedNodes;
	FEdGraphUtilities::ImportNodesFromText(DataprepGraph.Get(), TextToImport, /*out*/ PastedNodes);

	TArray<const UDataprepActionAsset*> Actions;
	for(UEdGraphNode* Node : PastedNodes)
	{
		UDataprepGraphActionNode* ActionNode = Cast<UDataprepGraphActionNode>(Node);
		if (ActionNode && ActionNode->CanDuplicateNode() && ActionNode->GetDataprepActionAsset())
		{
			Actions.Add(ActionNode->GetDataprepActionAsset());
		}
	}

	if(Actions.Num() > 0)
	{
		FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());

		if(DataprepAssetPtr->AddActions(Actions) == INDEX_NONE)
		{
			Transaction.Cancel();
		}
	}
}

bool SDataprepGraphEditor::CanPasteNodes() const
{
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	return FEdGraphUtilities::CanImportNodesFromText(GetCurrentGraph(), ClipboardContent);
}

void SDataprepGraphEditor::DuplicateNodes()
{
	// Copy and paste current selection
	CopySelectedNodes();
	PasteNodes();
}

bool SDataprepGraphEditor::CanDuplicateNodes() const
{
	return CanCopyNodes();
}

void SDataprepGraphEditor::DeleteSelectedDuplicatableNodes()
{
	// Cache off the old selection
	const FGraphPanelSelectionSet OldSelectedNodes = GetSelectedNodes();

	// Clear the selection and only select the nodes that can be duplicated
	FGraphPanelSelectionSet CurrentSelection;
	ClearSelectionSet();

	FGraphPanelSelectionSet RemainingNodes;
	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(OldSelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
		if ((Node != NULL) && Node->CanDuplicateNode())
		{
			SetNodeSelection(Node, true);
		}
		else
		{
			RemainingNodes.Add(Node);
		}
	}

	// Delete the nodes which can be duplicated
	DeleteSelectedNodes();

	// Reselect whatever is left from the original selection after the deletion
	ClearSelectionSet();

	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(RemainingNodes); SelectedIter; ++SelectedIter)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter))
		{
			SetNodeSelection(Node, true);
		}
	}
}

bool SDataprepGraphEditor::OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* NodeBeingChanged, FText& OutErrorMessage)
{
	bool bValid(false);

	if (NodeBeingChanged && NodeBeingChanged->bCanRenameNode)
	{
		// Clear off any existing error message 
		NodeBeingChanged->ErrorMsg.Empty();
		NodeBeingChanged->bHasCompilerMessage = false;

		TSharedPtr<INameValidatorInterface> NameEntryValidator = NodeBeingChanged->MakeNameValidator();

		check( NameEntryValidator.IsValid() );

		EValidatorResult VResult = NameEntryValidator->IsValid(NewText.ToString(), true);
		if (VResult == EValidatorResult::Ok)
		{
			bValid = true;
		}
	}

	return bValid;
}

void SDataprepGraphEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{
	if (NodeBeingChanged)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("RenameNode", "RenameNode", "Rename Node"));
		NodeBeingChanged->Modify();
		NodeBeingChanged->OnRenameNode(NewText.ToString());
	}
}

FActionMenuContent SDataprepGraphEditor::OnCreateActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
{
	// bAutoExpand is voluntary ignored for now
	TUniquePtr<IDataprepMenuActionCollector> ActionCollector = MakeUnique<FDataprepAllMenuActionCollector>();

	TSharedRef<SDataprepActionMenu> ActionMenu =
		SNew (SDataprepActionMenu, MoveTemp(ActionCollector))
		.TransactionText(LOCTEXT("AddingANewActionNode","Add a Action Node"))
		.GraphObj(InGraph)
		.NewNodePosition(InNodePosition)
		.DraggedFromPins(InDraggedPins)
		.OnClosedCallback(InOnMenuClosed);

	return FActionMenuContent( ActionMenu, ActionMenu->GetFilterTextBox() );
}

FActionMenuContent SDataprepGraphEditor::OnCreateNodeOrPinMenu(UEdGraph* CurrentGraph, const UEdGraphNode* InGraphNode, const UEdGraphPin* InGraphPin, FMenuBuilder* MenuBuilder, bool bIsDebugging)
{
	if(CurrentGraph != GetCurrentGraph())
	{
		return FActionMenuContent();
	}

	// Open contextual menu for action node
	if(const UDataprepGraphActionNode* ActionNode = Cast<const UDataprepGraphActionNode>(InGraphNode))
	{
		MenuBuilder->BeginSection( FName( TEXT("CommonSection") ), LOCTEXT("CommonSection", "Common") );
		{
			MenuBuilder->AddMenuEntry(FGenericCommands::Get().Copy);
			MenuBuilder->AddMenuEntry(FGenericCommands::Get().Cut);
			MenuBuilder->AddMenuEntry(FGenericCommands::Get().Duplicate);
			MenuBuilder->AddMenuEntry(FGenericCommands::Get().Delete);
		}
		MenuBuilder->EndSection();

		return FActionMenuContent(MenuBuilder->MakeWidget());
	}

	// Create contextual for graph panel when on top of track node too
	return OnCreateActionMenu(CurrentGraph, GetPasteLocation(), TArray<UEdGraphPin*>(), true, SGraphEditor::FActionMenuClosed());
}

#undef LOCTEXT_NAMESPACE