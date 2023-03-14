// Copyright Epic Games, Inc. All Rights Reserved.

#include "Graph/SControlRigGraphNodeKnot.h"
#include "Graph/ControlRigGraphSchema.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "SControlRigGraphNodeKnot"

void SControlRigGraphNodeKnot::Construct(const FArguments& InArgs, UEdGraphNode* InKnot)
{
	SGraphNodeKnot::Construct(SGraphNodeKnot::FArguments(), InKnot);

	if (UControlRigGraphNode* CRNode = Cast<UControlRigGraphNode>(InKnot))
	{
		CRNode->OnNodeBeginRemoval().AddSP(this, &SControlRigGraphNodeKnot::HandleNodeBeginRemoval);
	}
}

void SControlRigGraphNodeKnot::EndUserInteraction() const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	if (GraphNode)
	{
		if (const UControlRigGraphSchema* RigSchema = Cast<UControlRigGraphSchema>(GraphNode->GetSchema()))
		{
			RigSchema->EndGraphNodeInteraction(GraphNode);
		}
	}

	SGraphNodeKnot::EndUserInteraction();
}

void SControlRigGraphNodeKnot::MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty)
{
	if (!NodeFilter.Find(SharedThis(this)))
	{
		if (GraphNode && !RequiresSecondPassLayout())
		{
			if (const UControlRigGraphSchema* RigSchema = Cast<UControlRigGraphSchema>(GraphNode->GetSchema()))
			{
				RigSchema->SetNodePosition(GraphNode, NewPosition, false);
			}
		}
	}
}

void SControlRigGraphNodeKnot::HandleNodeBeginRemoval()
{
	if(UControlRigGraphNode* RigNode = Cast<UControlRigGraphNode>(GraphNode))
	{
		RigNode->OnNodeBeginRemoval().RemoveAll(this);
	}
	
	for (const TSharedRef<SGraphPin>& GraphPin: InputPins)
	{
		GraphPin->SetPinObj(nullptr);
	}
	for (const TSharedRef<SGraphPin>& GraphPin: OutputPins)
	{
		GraphPin->SetPinObj(nullptr);
	}

	InputPins.Reset();
	OutputPins.Reset();
	
	InvalidateGraphData();
}

#undef LOCTEXT_NAMESPACE
