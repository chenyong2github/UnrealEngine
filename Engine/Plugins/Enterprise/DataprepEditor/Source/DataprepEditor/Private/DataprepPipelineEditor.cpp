// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DataprepEditor.h"

#include "BlueprintNodes/K2Node_DataprepAction.h"
#include "DataprepEditorModule.h"
#include "DataprepRecipe.h"
#include "DataprepEditorActions.h"
#include "SchemaActions/DataprepAllMenuActionCollector.h"
#include "SchemaActions/IDataprepMenuActionCollector.h"
#include "Widgets/SDataprepActionMenu.h"

#include "BlueprintEditorSettings.h"
#include "Components/ActorComponent.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphNode_Comment.h"
#include "EdGraphSchema_K2_Actions.h"
#include "EdGraphUtilities.h"
#include "EditorStyleSet.h"
#include "Engine/SCS_Node.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IMessageLogListing.h"
#include "K2Node_AddComponent.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Logging/MessageLog.h"
#include "Misc/UObjectToken.h"
#include "PropertyEditorDelegates.h"
#include "PropertyEditorModule.h"
#include "SGraphNode.h"
#include "SKismetInspector.h"
#include "ScopedTransaction.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/Anchors.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Notifications/SErrorText.h"

#define LOCTEXT_NAMESPACE "DataprepPipelineEditor"

namespace DataprepEditorUtils
{
	/**
	 * Searches through a blueprint, looking for the most severe error'ing node.
	 *
	 * @param  Blueprint	The blueprint to search through.
	 * @param  Severity		Defines the severity of the error/warning to search for.
	 * @return The first node found with the specified error.
	 * Inspired by BlueprintEditorImpl::FindNodeWithError
	 */
	UEdGraphNode* FindNodeWithError(FCompilerResultsLog const& ErrorLog, EMessageSeverity::Type Severity = EMessageSeverity::Error);
}

void FDataprepEditor::CreatePipelineEditor()
{
	if (!PipelineEditorCommands.IsValid())
	{
		PipelineEditorCommands = MakeShareable(new FUICommandList);

		PipelineEditorCommands->MapAction(FGenericCommands::Get().Rename,
			FExecuteAction::CreateSP(this, &FDataprepEditor::OnRenameNode),
			FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanRenameNode)
		);

		PipelineEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
			FExecuteAction::CreateSP(this, &FDataprepEditor::SelectAllNodes),
			FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanSelectAllNodes)
		);

		PipelineEditorCommands->MapAction(FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &FDataprepEditor::DeleteSelectedPipelineNodes),
			FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanDeletePipelineNodes)
		);

		PipelineEditorCommands->MapAction(FGenericCommands::Get().Copy,
			FExecuteAction::CreateSP(this, &FDataprepEditor::CopySelectedNodes),
			FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanCopyNodes)
		);

		PipelineEditorCommands->MapAction(FGenericCommands::Get().Cut,
			FExecuteAction::CreateSP(this, &FDataprepEditor::CutSelectedNodes),
			FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanCutNodes)
		);

		PipelineEditorCommands->MapAction(FGenericCommands::Get().Paste,
			FExecuteAction::CreateSP(this, &FDataprepEditor::PasteNodes),
			FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanPasteNodes)
		);

		PipelineEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
			FExecuteAction::CreateSP(this, &FDataprepEditor::DuplicateNodes),
			FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanDuplicateNodes)
		);

		PipelineEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
			FExecuteAction::CreateSP(this, &FDataprepEditor::OnCreateComment)
		);
	}

	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText", "DATAPREP");

	// Create the title bar widget
	TSharedRef<SWidget> TitleBarWidget =
		SNew(SBorder)
		.BorderImage(FEditorStyle::GetBrush(TEXT("Graph.TitleBackground")))
		.HAlign(HAlign_Fill)
		.Padding( 4.f )
		[
			SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Anchors( FAnchors(0.5) )
			.Alignment( FVector2D(0.5,0.5) )
			.AutoSize( true )
			[
				SNew(STextBlock)
				.Text(LOCTEXT("DataprepPipelineEditor_TitleBar_Label", "Dataprep Graph"))
				.TextStyle(FEditorStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
			]
		];

	SGraphEditor::FGraphEditorEvents Events;
	Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FDataprepEditor::OnPipelineEditorSelectionChanged);
	Events.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &FDataprepEditor::OnCreatePipelineActionMenu);
	Events.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &FDataprepEditor::OnNodeVerifyTitleCommit);
	Events.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FDataprepEditor::OnNodeTitleCommitted);

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(DataprepRecipeBPPtr.Get());

	PipelineView = SNew(SGraphEditor)
		.AdditionalCommands(PipelineEditorCommands)
		.Appearance(AppearanceInfo)
		.TitleBar(TitleBarWidget)
		.GraphToEdit(EventGraph)
		.GraphEvents(Events);

	DataprepRecipeBPPtr->OnChanged().AddSP(this, &FDataprepEditor::OnPipelineChanged);
	DataprepRecipeBPPtr->OnCompiled().AddSP(this, &FDataprepEditor::OnPipelineCompiled);

	CompilerResultsListing = FCompilerResultsLog::GetBlueprintMessageLog(DataprepRecipeBPPtr.Get());
	CompilerResultsListing->OnMessageTokenClicked().AddSP(this, &FDataprepEditor::OnLogTokenClicked);

}

FActionMenuContent FDataprepEditor::OnCreatePipelineActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed)
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

bool FDataprepEditor::IsPipelineEditable() const
{
	return !FSlateApplication::Get().InKismetDebuggingMode() &&
		   (DataprepRecipeBPPtr.IsValid()/* && DataprepRecipeBPPtr->CanRecompileWhilePlayingInEditor()*/) &&
		   !FBlueprintEditorUtils::IsGraphReadOnly(GetPipelineGraph());
}

UEdGraph* FDataprepEditor::GetPipelineGraph() const
{
	return PipelineView.IsValid() ? PipelineView->GetCurrentGraph() : nullptr;
}

TSet<UObject*> FDataprepEditor::GetSelectedPipelineNodes() const
{
	TSet<UObject*> CurrentSelection;
	if (PipelineView.IsValid())
	{
		CurrentSelection = PipelineView->GetSelectedNodes();
	}
	return CurrentSelection;
}

// Copied from FBlueprintEditor::DeleteSelectedNodes
void FDataprepEditor::DeleteSelectedPipelineNodes()
{
	if (!PipelineView.IsValid())
	{
		return;
	}

	// DATAPREP_TODO: Add undo/redo
	FScopedTransaction Transaction(FGenericCommands::Get().Delete->GetDescription());
	PipelineView->GetCurrentGraph()->Modify();

	bool bDidSomeModification = false;
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedPipelineNodes();

	for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(*NodeIt))
		{
			if (Node->CanUserDeleteNode())
			{
				FBlueprintEditorUtils::RemoveNode(DataprepRecipeBPPtr.Get(), Node, true);
				bDidSomeModification = true;
			}
		}
	}

	if (bDidSomeModification)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(DataprepRecipeBPPtr.Get());
	}
	else
	{
		Transaction.Cancel();
	}

	//@TODO: Reselect items that were not deleted
}

// Temporary implementation from the blueprint editor
void FDataprepEditor::OnRenameNode()
{
	if (PipelineView.IsValid())
	{
		const FGraphPanelSelectionSet SelectedNodes = GetSelectedPipelineNodes();
		for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
		{
			UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(*NodeIt);
			if (SelectedNode != NULL && SelectedNode->bCanRenameNode)
			{
				PipelineView->IsNodeTitleVisible(SelectedNode, true);
				break;
			}
		}
	}
}

bool FDataprepEditor::CanRenameNode() const
{
	if ( IsPipelineEditable() )
	{
		const FGraphPanelSelectionSet SelectedNodes = GetSelectedPipelineNodes();
		if ( SelectedNodes.Num() == 1)
		{
			if ( UEdGraphNode* SelectedNode = Cast<UEdGraphNode>(*SelectedNodes.CreateConstIterator()) )
			{
				return SelectedNode->bCanRenameNode;
			}
		}
	}

	return false;
}

void FDataprepEditor::SelectAllNodes()
{
	if (PipelineView.IsValid())
	{
		PipelineView->SelectAllNodes();
	}
}

bool FDataprepEditor::CanSelectAllNodes() const
{
	return true;
}

bool FDataprepEditor::CanDeletePipelineNodes() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedPipelineNodes();

	bool bCanUserDeleteNode = false;

	if (IsPipelineEditable() && SelectedNodes.Num() > 0)
	{
		for (UObject* NodeObject : SelectedNodes)
		{
			// If any nodes allow deleting, then do not disable the delete option
			UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObject);
			if (Node->CanUserDeleteNode())
			{
				bCanUserDeleteNode = true;
				break;
			}
		}
	}

	return bCanUserDeleteNode;
}

void FDataprepEditor::CopySelectedNodes()
{
	// Export the selected nodes and place the text on the clipboard
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedPipelineNodes();

	FString ExportedText;

	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		if (UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter))
		{
			Node->PrepareForCopying();
		}
	}

	FEdGraphUtilities::ExportNodesToText(SelectedNodes, /*out*/ ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);
}

bool FDataprepEditor::CanCopyNodes() const
{
	// If any of the nodes can be duplicated then we should allow copying
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedPipelineNodes();
	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
		if ((Node != NULL) && Node->CanDuplicateNode())
		{
			return true;
		}
	}
	return false;
}

void FDataprepEditor::CutSelectedNodes()
{
	CopySelectedNodes();
	// Cut should only delete nodes that can be duplicated
	DeleteSelectedDuplicatableNodes();
}

bool FDataprepEditor::CanCutNodes() const
{
	return IsPipelineEditable() && CanCopyNodes() && CanDeletePipelineNodes();
}

void FDataprepEditor::PasteNodes()
{
	// Find the graph editor with focus

	if (!PipelineView.IsValid())
	{
		return;
	}

	PasteNodesHere(PipelineView->GetCurrentGraph(), PipelineView->GetPasteLocation());
}

bool FDataprepEditor::CanPasteNodes() const
{
	// Find the graph editor with focus
	if (!PipelineView.IsValid() && !IsPipelineEditable() )
	{
		return false;
	}

	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

	return FEdGraphUtilities::CanImportNodesFromText(PipelineView->GetCurrentGraph(), ClipboardContent);
}

void FDataprepEditor::DuplicateNodes()
{
	// Copy and paste current selection
	CopySelectedNodes();
	PasteNodes();
}

bool FDataprepEditor::CanDuplicateNodes() const
{
	return IsPipelineEditable() && CanCopyNodes();
}

void FDataprepEditor::DeleteSelectedDuplicatableNodes()
{
	// Cache off the old selection
	const FGraphPanelSelectionSet OldSelectedNodes = GetSelectedPipelineNodes();

	// Clear the selection and only select the nodes that can be duplicated
	FGraphPanelSelectionSet CurrentSelection;
	if (PipelineView.IsValid())
	{
		PipelineView->ClearSelectionSet();

		FGraphPanelSelectionSet RemainingNodes;
		for (FGraphPanelSelectionSet::TConstIterator SelectedIter(OldSelectedNodes); SelectedIter; ++SelectedIter)
		{
			UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
			if ((Node != NULL) && Node->CanDuplicateNode())
			{
				PipelineView->SetNodeSelection(Node, true);
			}
			else
			{
				RemainingNodes.Add(Node);
			}
		}

		// Delete the duplicatable nodes
		DeleteSelectedPipelineNodes();

		// Reselect whatever's left from the original selection after the deletion
		PipelineView->ClearSelectionSet();

		for (FGraphPanelSelectionSet::TConstIterator SelectedIter(RemainingNodes); SelectedIter; ++SelectedIter)
		{
			if (UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter))
			{
				PipelineView->SetNodeSelection(Node, true);
			}
		}
	}
}

void FDataprepEditor::PasteNodesHere(class UEdGraph* DestinationGraph, const FVector2D& GraphLocation)
{
	// Find the graph editor with focus
	if (!PipelineView.IsValid())
	{
		return;
	}
	// Select the newly pasted stuff
	bool bNeedToModifyStructurally = false;
	{
		const FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());
		DestinationGraph->Modify();
		PipelineView->ClearSelectionSet();

		// Grab the text to paste from the clipboard.
		FString TextToImport;
		FPlatformApplicationMisc::ClipboardPaste(TextToImport);

		// Import the nodes
		TSet<UEdGraphNode*> PastedNodes;
		FEdGraphUtilities::ImportNodesFromText(DestinationGraph, TextToImport, /*out*/ PastedNodes);

		//Average position of nodes so we can move them while still maintaining relative distances to each other
		FVector2D AvgNodePosition(0.0f, 0.0f);

		for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
		{
			UEdGraphNode* Node = *It;
			AvgNodePosition.X += Node->NodePosX;
			AvgNodePosition.Y += Node->NodePosY;
		}

		float InvNumNodes = 1.0f / float(PastedNodes.Num());
		AvgNodePosition.X *= InvNumNodes;
		AvgNodePosition.Y *= InvNumNodes;

		for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
		{
			UEdGraphNode* Node = *It;
			PipelineView->SetNodeSelection(Node, true);

			Node->NodePosX = (Node->NodePosX - AvgNodePosition.X) + GraphLocation.X;
			Node->NodePosY = (Node->NodePosY - AvgNodePosition.Y) + GraphLocation.Y;

			Node->SnapToGrid(SNodePanel::GetSnapGridSize());

			// Give new node a different Guid from the old one
			Node->CreateNewGuid();

			UK2Node* K2Node = Cast<UK2Node>(Node);
			if (K2Node && K2Node->NodeCausesStructuralBlueprintChange())
			{
				bNeedToModifyStructurally = true;
			}

			// The only useful K2Node for this editor are our DataprepAction node
			if (K2Node)
			{
				if (!K2Node->IsA<UK2Node_DataprepAction>())
				{
					FBlueprintEditorUtils::RemoveNode(DataprepRecipeBPPtr.Get(), Node, true);
				}
			}
			
		}
	}

	if (bNeedToModifyStructurally)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(DataprepRecipeBPPtr.Get());
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(DataprepRecipeBPPtr.Get());
	}

	// Update UI
	PipelineView->NotifyGraphChanged();
}

void FDataprepEditor::OnCreateComment()
{
	if (PipelineView.IsValid())
	{
		if (UEdGraph* Graph = PipelineView->GetCurrentGraph())
		{
			if (const UEdGraphSchema* Schema = Graph->GetSchema())
			{
				// Add menu item for creating comment boxes
				UEdGraphNode_Comment* CommentTemplate = NewObject<UEdGraphNode_Comment>();

				FVector2D SpawnLocation = PipelineView->GetPasteLocation();

				FSlateRect Bounds;
				if ( PipelineView->GetBoundsForSelectedNodes(Bounds, 50.0f) )
				{
					CommentTemplate->SetBounds(Bounds);
					SpawnLocation.X = CommentTemplate->NodePosX;
					SpawnLocation.Y = CommentTemplate->NodePosY;
				}

				UEdGraphNode* NewNode = FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(Graph, CommentTemplate, SpawnLocation, /** bSelectNewNode */ true);

				// Mark Blueprint as structurally modified since
				// UK2Node_Comment::NodeCausesStructuralBlueprintChange used to return true
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(DataprepRecipeBPPtr.Get());
			}
		}
	}
}


bool FDataprepEditor::OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* NodeBeingChanged, FText& OutErrorMessage)
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
		else if (PipelineView.IsValid())
		{
			EValidatorResult Valid = NameEntryValidator->IsValid(NewText.ToString(), false);

			NodeBeingChanged->bHasCompilerMessage = true;
			NodeBeingChanged->ErrorMsg = NameEntryValidator->GetErrorString(NewText.ToString(), Valid);
			NodeBeingChanged->ErrorType = EMessageSeverity::Error;
		}
	}

	return bValid;
}

void FDataprepEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{
	if (NodeBeingChanged)
	{
		const FScopedTransaction Transaction(NSLOCTEXT("RenameNode", "RenameNode", "Rename Node"));
		NodeBeingChanged->Modify();
		NodeBeingChanged->OnRenameNode(NewText.ToString());
	}
}

// Inspired by FBlueprintEditor::OnBlueprintCompiled
void FDataprepEditor::OnPipelineCompiled(UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		check(InBlueprint == DataprepRecipeBPPtr.Get());
		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(DataprepRecipeBPPtr.Get());
		for (const UEdGraphNode* Node : EventGraph->Nodes)
		{
			if (Node)
			{
				TSharedPtr<SGraphNode> Widget = Node->DEPRECATED_NodeWidget.Pin();
				if (Widget.IsValid())
				{
					Widget->RefreshErrorInfo();
				}
			}
		}

		OnPipelineChanged(InBlueprint);
	}
}

// Inspired by FBlueprintEditor::OnBlueprintChangedImpl
void FDataprepEditor::OnPipelineChanged(UBlueprint* InBlueprint)
{
	if (InBlueprint)
	{
		check(InBlueprint == DataprepRecipeBPPtr.Get());

		// Notify that the blueprint has been changed (update Content browser, etc)
		InBlueprint->PostEditChange();

		// Call PostEditChange() on any Actors that are based on this Blueprint 
		FBlueprintEditorUtils::PostEditChangeBlueprintActors(InBlueprint);
	}

	if ( PipelineView )
	{
		PipelineView->NotifyGraphChanged();
	}
}

// Inspired by FBlueprintEditor::Compile
void FDataprepEditor::OnCompile()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FDataprepEditor::OnCompile);

	if (DataprepRecipeBPPtr.IsValid())
	{
		FMessageLog BlueprintLog("BlueprintLog");

		FFormatNamedArguments Arguments;
		Arguments.Add(TEXT("BlueprintName"), FText::FromString(DataprepRecipeBPPtr->GetName()));
		BlueprintLog.NewPage(FText::Format(LOCTEXT("CompilationPageLabel", "Compile {BlueprintName}"), Arguments));

		FCompilerResultsLog LogResults;
		LogResults.SetSourcePath(DataprepRecipeBPPtr->GetPathName());
		LogResults.BeginEvent(TEXT("Compile"));
		LogResults.bLogDetailedResults = GetDefault<UBlueprintEditorSettings>()->bShowDetailedCompileResults;
		LogResults.EventDisplayThresholdMs = GetDefault<UBlueprintEditorSettings>()->CompileEventDisplayThresholdMs;
		EBlueprintCompileOptions CompileOptions = EBlueprintCompileOptions::None;
		if (bSaveIntermediateBuildProducts)
		{
			CompileOptions |= EBlueprintCompileOptions::SaveIntermediateProducts;
		}
		FKismetEditorUtilities::CompileBlueprint(DataprepRecipeBPPtr.Get(), CompileOptions, &LogResults);

		LogResults.EndEvent();

		const bool bForceMessageDisplay = ((LogResults.NumWarnings > 0) || (LogResults.NumErrors > 0)) && !DataprepRecipeBPPtr->bIsRegeneratingOnLoad;
		{
			CompilerResultsListing->ClearMessages();

			// Note we don't mirror to the output log here as the compiler already does that
			CompilerResultsListing->AddMessages(LogResults.Messages, false);

			// DATAPREP_TODO: Create tab for compiling error
			//if (!bEditorMarkedAsClosed && bForceMessageDisplay && GetCurrentMode() == FBlueprintEditorApplicationModes::StandardBlueprintEditorMode)
			//{
			//	TabManager->InvokeTab(FBlueprintEditorTabs::CompilerResultsID);
			//}
		}

		UBlueprintEditorSettings const* BpEditorSettings = GetDefault<UBlueprintEditorSettings>();
		if ((LogResults.NumErrors > 0) && BpEditorSettings->bJumpToNodeErrors)
		{
			if (UEdGraphNode* NodeWithError = DataprepEditorUtils::FindNodeWithError(LogResults))
			{
				if (PipelineView.IsValid())
				{
					PipelineView->JumpToNode(NodeWithError, false);
				}
			}
		}

		if (DataprepRecipeBPPtr->UpgradeNotesLog.IsValid())
		{
			CompilerResultsListing->AddMessages(DataprepRecipeBPPtr->UpgradeNotesLog->Messages);
		}
	}
}

// Copied from FBlueprintEditorToolbar::GetStatusImage
FSlateIcon FDataprepEditor::GetPipelineCompileButtonImage() const
{
	switch (DataprepRecipeBPPtr->Status)
	{
		case BS_Error:
			return FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Status.Error");
		case BS_UpToDate:
			return FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Status.Good");
		case BS_UpToDateWithWarnings:
			return FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Status.Warning");
	}

	return FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Status.Unknown");
}

// Copied from FBlueprintEditorToolbar::GetStatusTooltip
FText FDataprepEditor::GetPipelineCompileButtonTooltip() const
{
	switch (DataprepRecipeBPPtr->Status)
	{
	case BS_Dirty:
		return LOCTEXT("Dirty_Status", "Dataprep graph is dirty; needs to be recompiled");
	case BS_Error:
		return LOCTEXT("CompileError_Status", "There was an error during compilation, see Dataprep graph viewport for details");
	case BS_UpToDate:
		return LOCTEXT("GoodToGo_Status", "Dataprep is ready");
	case BS_UpToDateWithWarnings:
		return LOCTEXT("GoodToGoWarning_Status", "There was a warning during compilation, see Dataprep graph viewport for details");
	}

	return LOCTEXT("Recompile_Status", "Unknown status; should recompile Dataprep graph");
}

void FDataprepEditor::OnLogTokenClicked(const TSharedRef<IMessageToken>& Token)
{
	if (Token->GetType() == EMessageToken::Object)
	{
		const TSharedRef<FUObjectToken> ObjectToken = StaticCastSharedRef<FUObjectToken>(Token);
		if (ObjectToken->GetObject().IsValid() && PipelineView.IsValid())
		{
			const UEdGraphNode* Node = Cast<const UEdGraphNode>(ObjectToken->GetObject().Get());
			PipelineView->JumpToNode(Node, false);
		}
	}
	else if (Token->GetType() == EMessageToken::EdGraph)
	{
		const TSharedRef<FEdGraphToken> EdGraphToken = StaticCastSharedRef<FEdGraphToken>(Token);
		const UEdGraphPin* PinBeingReferenced = EdGraphToken->GetPin();
		const UObject* ObjectBeingReferenced = EdGraphToken->GetGraphObject();
		if (PinBeingReferenced && PipelineView.IsValid())
		{
			PipelineView->JumpToPin(PinBeingReferenced);
		}
		else if (ObjectBeingReferenced && PipelineView.IsValid())
		{
			const UEdGraphNode* Node = Cast<const UEdGraphNode>(ObjectBeingReferenced);
			PipelineView->JumpToNode(Node, false);
		}
	}
}

UEdGraphNode* DataprepEditorUtils::FindNodeWithError(FCompilerResultsLog const& ErrorLog, EMessageSeverity::Type Severity/* = EMessageSeverity::Error*/)
{
	UEdGraphNode* ChoiceNode = nullptr;
	for (TWeakObjectPtr<UEdGraphNode> NodePtr : ErrorLog.AnnotatedNodes)
	{
		UEdGraphNode* Node = NodePtr.Get();
		if ((Node != nullptr) && (Node->ErrorType <= Severity))
		{
			if ((ChoiceNode == nullptr) || (Node->ErrorType < ChoiceNode->ErrorType))
			{
				ChoiceNode = Node;
				if (ChoiceNode->ErrorType == 0)
				{
					break;
				}
			}
		}
	}

	return ChoiceNode;
}

#undef LOCTEXT_NAMESPACE
