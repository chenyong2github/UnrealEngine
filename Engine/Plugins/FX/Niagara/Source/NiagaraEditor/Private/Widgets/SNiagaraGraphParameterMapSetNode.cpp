// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraGraphParameterMapSetNode.h"
#include "NiagaraNodeParameterMapSet.h"
#include "Widgets/Input/SButton.h"
#include "GraphEditorSettings.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SBoxPanel.h"
#include "SGraphPin.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraScriptVariable.h"
#include "SDropTarget.h"
#include "NiagaraEditorStyle.h"


#define LOCTEXT_NAMESPACE "SNiagaraGraphParameterMapSetNode"


void SNiagaraGraphParameterMapSetNode::Construct(const FArguments& InArgs, UEdGraphNode* InGraphNode)
{

	GraphNode = InGraphNode; 
	RegisterNiagaraGraphNode(InGraphNode);

	UpdateGraphNode();
}

TSharedRef<SWidget> SNiagaraGraphParameterMapSetNode::CreateNodeContentArea()
{
	TSharedRef<SWidget> ExistingContent = SNiagaraGraphNode::CreateNodeContentArea();
	// NODE CONTENT AREA
	return 	SNew(SDropTarget)
		.OnDrop(this, &SNiagaraGraphParameterMapSetNode::OnDroppedOnTarget)
		.OnAllowDrop(this, &SNiagaraGraphParameterMapSetNode::OnAllowDrop)
		.HorizontalImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.DropTarget.BorderHorizontal"))
		.VerticalImage(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.DropTarget.BorderVertical"))
		.BackgroundColor(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.DropTarget.BackgroundColor"))
		.BackgroundColorHover(FNiagaraEditorStyle::Get().GetColor("NiagaraEditor.DropTarget.BackgroundColorHover"))
		.Content()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("NoBorder"))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(FMargin(0, 3))
			[
				ExistingContent
			]
		];
}

FReply SNiagaraGraphParameterMapSetNode::OnDroppedOnTarget(TSharedPtr<FDragDropOperation> DropOperation)
{
	UNiagaraNodeParameterMapBase* MapNode = Cast<UNiagaraNodeParameterMapBase>(GraphNode);
	if (MapNode != nullptr && MapNode->HandleDropOperation(DropOperation))
	{
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

bool SNiagaraGraphParameterMapSetNode::OnAllowDrop(TSharedPtr<FDragDropOperation> DragDropOperation)
{
	UNiagaraNodeParameterMapBase* MapNode = Cast<UNiagaraNodeParameterMapBase>(GraphNode);
	return MapNode != nullptr && MapNode->CanHandleDropOperation(DragDropOperation);
}

#undef LOCTEXT_NAMESPACE
