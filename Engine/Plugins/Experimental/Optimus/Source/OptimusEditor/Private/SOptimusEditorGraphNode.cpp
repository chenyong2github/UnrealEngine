// Copyright Epic Games, Inc. All Rights Reserved.

#include "SOptimusEditorGraphNode.h"

#include "OptimusEditorGraph.h"
#include "OptimusEditorGraphNode.h"

#include "OptimusActionStack.h"
#include "OptimusNodeGraph.h"
#include "OptimusNode.h"


void SOptimusEditorGraphNode::Construct(const FArguments& InArgs)
{
	GraphNode = InArgs._GraphNode;

	SetCursor( EMouseCursor::CardinalCross );
	UpdateGraphNode();
}


void SOptimusEditorGraphNode::EndUserInteraction() const
{
	UOptimusEditorGraph* Graph = Cast<UOptimusEditorGraph>(GraphNode->GetGraph());
	if (ensure(Graph))
	{
#if WITH_EDITOR
		// Cancel the current transaction created by SNodePanel::OnMouseMove so that the
		// only transaction recorded is the one we place on the action stack.
		if (GEditor)
		{
			GEditor->CancelTransaction(0);
		}
#endif

		const TSet<UOptimusEditorGraphNode*> &SelectedNodes = Graph->GetSelectedNodes();

		if (SelectedNodes.Num() == 0)
		{
			return;
		}

		FString ActionTitle;
		if (SelectedNodes.Num() == 1)
		{
			ActionTitle = TEXT("Move Node");
		}
		else
		{
			ActionTitle = FString::Printf(TEXT("Move %d Nodes"), SelectedNodes.Num());
		}

		FOptimusActionScope Scope(*Graph->GetModelGraph()->GetActionStack(), ActionTitle);
		for (UOptimusEditorGraphNode* SelectedNode : SelectedNodes)
		{
			FVector2D Position(SelectedNode->NodePosX, SelectedNode->NodePosY);
			SelectedNode->ModelNode->SetGraphPosition(Position);
		}
	}
}
