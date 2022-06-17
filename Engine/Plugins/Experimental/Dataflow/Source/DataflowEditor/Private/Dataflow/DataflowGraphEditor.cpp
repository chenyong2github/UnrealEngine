// Copyright Epic Games, Inc. All Rights Reserved.

#include "Dataflow/DataflowGraphEditor.h"

#include "DataflowEditorToolkit.h"
#include "Dataflow/DataflowEngine.h"
#include "Dataflow/DataflowSNodeFactories.h"
#include "BoneDragDropOp.h"
#include "IStructureDetailsView.h"

DEFINE_LOG_CATEGORY_STATIC(SDataflowGraphEditorLog, Log, All);

#define LOCTEXT_NAMESPACE "DataflowGraphEditor"


void SDataflowGraphEditor::Construct(const FArguments& InArgs, UObject* InAssetOwner)
{
	check(InArgs._GraphToEdit);
	AssetOwner = InAssetOwner; // nullptr is valid
	DataflowAsset = Cast<UDataflow>(InArgs._GraphToEdit);
	DetailsView = InArgs._DetailsView;
	EvaluateGraphCallback = InArgs._EvaluateGraph;
	OnDragDropEventCallback = InArgs._OnDragDropEvent;

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
		FDataflowEditorCommands::FGraphEvaluationCallback LocalEvaluateCallback = [](FDataflowNode* Node, FDataflowOutput* Out)
		{
			Dataflow::FContext Context(FGameTime::GetTimeSinceAppStart().GetRealTimeSeconds());
			Node->Evaluate(Context, Out);
		};

		FDataflowEditorCommands::EvaluateNodes(GetSelectedNodes(), LocalEvaluateCallback);
	}
}

void SDataflowGraphEditor::DeleteNode()
{
	if (DataflowAsset.Get())
	{
		if (DetailsView)
		{
			DetailsView->SetStructureData(nullptr);
		}

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

FReply SDataflowGraphEditor::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	UE_LOG(SDataflowGraphEditorLog, All, TEXT("SDataflowGraphEditor::OnDragOver"));
	if (TSharedPtr<FBoneDragDropOp> SchemeDragDropOp = DragDropEvent.GetOperationAs<FBoneDragDropOp>())
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}


FReply SDataflowGraphEditor::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	if (TSharedPtr<FBoneDragDropOp> SchemeDragDropOp = DragDropEvent.GetOperationAs<FBoneDragDropOp>())
	{
		if (OnDragDropEventCallback)
		{
			UE_LOG(SDataflowGraphEditorLog, All, TEXT("SDataflowGraphEditor::OnDrop"));
			OnDragDropEventCallback(MyGeometry,DragDropEvent);
		}
	}
	return SGraphEditor::OnDrop(MyGeometry, DragDropEvent);
}

/*
void SDataflowGraphEditor::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{

	TSharedPtr<FGraphSchemaActionDragDropAction> SchemeDragDropOp = DragDropEvent.GetOperationAs<FGraphSchemaActionDragDropAction>();
	{
	//	FDataprepSchemaActionContext ActionContext;
	//	ActionContext.DataprepActionPtr = StepData->DataprepActionPtr;
	//	ActionContext.DataprepActionStepPtr = StepData->DataprepActionStepPtr;
	//	ActionContext.StepIndex = StepData->StepIndex;
	//	DataprepDragDropOp->SetHoveredDataprepActionContext(ActionContext);
	}
	UE_LOG(SDataflowGraphEditorLog, All, TEXT("SDataflowGraphEditor::OnDragEnter"));

}
void SDataflowGraphEditor::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	TSharedPtr<FGraphSchemaActionDragDropAction> SchemeDragDropOp = DragDropEvent.GetOperationAs<FGraphSchemaActionDragDropAction>();
	{
		// DataflowDragDropOp->SetHoveredDataprepActionContext(TOptional<FDataprepSchemaActionContext>());
	}
	UE_LOG(SDataflowGraphEditorLog, All, TEXT("SDataflowGraphEditor::OnDragLeave"));
}
*/

#undef LOCTEXT_NAMESPACE
