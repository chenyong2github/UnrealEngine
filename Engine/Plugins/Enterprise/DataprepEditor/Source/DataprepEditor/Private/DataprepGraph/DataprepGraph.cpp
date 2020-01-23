// Copyright Epic Games, Inc. All Rights Reserved.

#include "DataprepGraph.h"

#include "DataprepEditor.h"
#include "DataprepGraph/DataprepGraphSchema.h"
#include "DataprepGraph/DataprepGraphActionNode.h"
#include "Widgets/DataprepGraph/SDataprepGraphEditor.h"
#include "Widgets/DataprepGraph/SDataprepGraphTrackNode.h"

#include "DataprepAsset.h"

#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "SNodePanel.h"
#include "Widgets/Layout/SConstraintCanvas.h"

#define LOCTEXT_NAMESPACE "DataprepGraphEditor"

void UDataprepGraph::Initialize(UDataprepAsset* InDataprepAsset)
{
	DataprepAssetPtr = InDataprepAsset;

	// Add recipe graph editor node which will be used as a start point to populate 
	RecipeNode = TStrongObjectPtr<UDataprepGraphRecipeNode>( Cast<UDataprepGraphRecipeNode>(CreateNode(UDataprepGraphRecipeNode::StaticClass(), false)) );
	RecipeNode->SetEnabledState(ENodeEnabledState::Disabled, true);
}

void FDataprepEditor::CreateGraphEditor()
{
	if(UDataprepAsset* DataprepAsset = Cast<UDataprepAsset>(DataprepAssetInterfacePtr.Get()) )
	{
		if (!GraphEditorCommands.IsValid())
		{
			GraphEditorCommands = MakeShareable(new FUICommandList);

			GraphEditorCommands->MapAction(FGenericCommands::Get().Rename,
				FExecuteAction::CreateSP(this, &FDataprepEditor::OnRenameNode),
				FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanRenameNode)
			);

			GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
				FExecuteAction::CreateSP(this, &FDataprepEditor::SelectAllNodes),
				FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanSelectAllNodes)
			);

			GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
				FExecuteAction::CreateSP(this, &FDataprepEditor::DeleteSelectedPipelineNodes),
				FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanDeletePipelineNodes)
			);

			GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
				FExecuteAction::CreateSP(this, &FDataprepEditor::CopySelectedNodes),
				FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanCopyNodes)
			);

			GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
				FExecuteAction::CreateSP(this, &FDataprepEditor::CutSelectedNodes),
				FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanCutNodes)
			);

			GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
				FExecuteAction::CreateSP(this, &FDataprepEditor::PasteNodes),
				FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanPasteNodes)
			);

			GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
				FExecuteAction::CreateSP(this, &FDataprepEditor::DuplicateNodes),
				FCanExecuteAction::CreateSP(this, &FDataprepEditor::CanDuplicateNodes)
			);

			GraphEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
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
				.Text(LOCTEXT("DataprepRecipeEditor_TitleBar_Label", "Dataprep Recipe"))
			.TextStyle(FEditorStyle::Get(), TEXT("GraphBreadcrumbButtonText"))
			]
		];

		SGraphEditor::FGraphEditorEvents Events;
		//Events.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FDataprepEditor::OnPipelineEditorSelectionChanged);
		//Events.OnCreateActionMenu = SGraphEditor::FOnCreateActionMenu::CreateSP(this, &FDataprepEditor::OnCreatePipelineActionMenu);
		//Events.OnVerifyTextCommit = FOnNodeVerifyTextCommit::CreateSP(this, &FDataprepEditor::OnNodeVerifyTitleCommit);
		//Events.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FDataprepEditor::OnNodeTitleCommitted);

		FName UniqueGraphName = MakeUniqueObjectName( GetTransientPackage(), UWorld::StaticClass(), FName( *(LOCTEXT("DataprepGraph", "Graph").ToString()) ) );
		DataprepGraph = TStrongObjectPtr<UDataprepGraph>( NewObject< UDataprepGraph >(GetTransientPackage(), UniqueGraphName) );
		DataprepGraph->Schema = UDataprepGraphSchema::StaticClass();

		DataprepGraph->Initialize( DataprepAsset );

		GraphEditor = SNew(SDataprepGraphEditor, DataprepAsset)
			.AdditionalCommands(GraphEditorCommands)
			.Appearance(AppearanceInfo)
			.TitleBar(TitleBarWidget)
			.GraphToEdit(DataprepGraph.Get())
			.GraphEvents(Events);

		DataprepGraph->SetEditor(GraphEditor);
	}
}

#undef LOCTEXT_NAMESPACE