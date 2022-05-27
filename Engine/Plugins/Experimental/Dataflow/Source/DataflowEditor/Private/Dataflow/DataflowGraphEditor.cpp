// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowGraphEditor.h"

#include "DataflowEditorToolkit.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowSNodeFactories.h"


#define LOCTEXT_NAMESPACE "DataflowGraphEditor"


void SDataflowGraphEditor::Construct(const FArguments& InArgs, UObject* InAssetOwner)
{
	check(InArgs._GraphToEdit);
	AssetOwner = InAssetOwner; // nullptr is valid
	DataflowAsset = Cast<UDataflow>(InArgs._GraphToEdit);
	DetailsView = InArgs._DetailsView;
	EvaluateGraphCallback = InArgs._EvaluateGraph;

	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = FText::FromString("Dataflow");

	FGraphEditorCommands::Register();
	FDataflowEditorCommands::Register();
	if (!GraphEditorCommands.IsValid())
	{
		GraphEditorCommands = MakeShareable(new FUICommandList);
		{
			GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
				FExecuteAction::CreateSP(this, &SDataflowGraphEditor::DeleteNode)
			);
			GraphEditorCommands->MapAction(FDataflowEditorCommands::Get().EvaluateNode,
				FExecuteAction::CreateSP(this, &SDataflowGraphEditor::EvaluateNode)
			);
		}
	}


	SGraphEditor::FArguments Arguments;
	Arguments._AdditionalCommands = GraphEditorCommands;
	Arguments._Appearance = AppearanceInfo;
	Arguments._GraphToEdit = InArgs._GraphToEdit;
	Arguments._GraphEvents = InArgs._GraphEvents;

	ensureMsgf(!Arguments._GraphEvents.OnSelectionChanged.IsBound(), TEXT("DataflowGraphEditor::OnSelectionChanged rebound during construction."));
	Arguments._GraphEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &SDataflowGraphEditor::OnSelectedNodesChanged);

	SGraphEditor::Construct(Arguments);

}


void SDataflowGraphEditor::EvaluateNode()
{
	float EvalTime = FGameTime::GetTimeSinceAppStart().GetRealTimeSeconds();
	if (EvaluateGraphCallback)
	{
		FDataflowEditorCommands::EvaluateNodes(GetSelectedNodes(), EvaluateGraphCallback);
	}
	else
	{
		FDataflowEditorCommands::FGraphEvaluationCallback LocalEvaluateCallback = [](Dataflow::FNode* Node, Dataflow::FConnection* Out)
		{
			float EvalTime = FGameTime::GetTimeSinceAppStart().GetRealTimeSeconds();
			return Node->Evaluate(Dataflow::FContext(EvalTime), Out);
		};

		FDataflowEditorCommands::EvaluateNodes(GetSelectedNodes(), LocalEvaluateCallback);
	}
}

void SDataflowGraphEditor::DeleteNode()
{
	if (DataflowAsset.Get())
	{
		FDataflowEditorCommands::DeleteNodes(DataflowAsset.Get(), GetSelectedNodes());
	}
}

void SDataflowGraphEditor::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
{
	if (DataflowAsset.Get() && DetailsView)
	{
		FDataflowEditorCommands::OnSelectedNodesChanged(DetailsView, AssetOwner.Get(), DataflowAsset.Get(), NewSelection);
	}
}

FReply SDataflowGraphEditor::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	//TSharedPtr<FDataprepDragDropOp> DragNodeOp = DragDropEvent.GetOperationAs<FDataprepDragDropOp>();
	//if (TrackGraphNodePtr.IsValid() && DragNodeOp.IsValid())
	//{
	//	return FReply::Handled().EndDragDrop();
	//}

	return SGraphEditor::OnDrop(MyGeometry, DragDropEvent);
}

#undef LOCTEXT_NAMESPACE
