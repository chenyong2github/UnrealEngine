// Copyright Epic Games, Inc. All Rights Reserved.

#include "SControlRigGraphNodeComment.h"
#include "EdGraphNode_Comment.h"
#include "Engine/Engine.h"
#include "PropertyPathHelpers.h"
#include "UObject/PropertyPortFlags.h"
#include "ControlRigBlueprint.h"
#include "Graph/ControlRigGraph.h"
#include "RigVMModel/RigVMController.h"
#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "SControlRigGraphNodeComment"

SControlRigGraphNodeComment::SControlRigGraphNodeComment()
	: SGraphNodeComment()
	, CachedNodeCommentColor(FLinearColor(-1.f, -1.f, -1.f, -1.f))
{
}

FReply SControlRigGraphNodeComment::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	FReply Reply = SGraphNodeComment::OnMouseButtonUp(MyGeometry, MouseEvent);
	if (Reply.IsEventHandled())
	{
		if (GraphNode)
		{
			UEdGraphNode_Comment* CommentNode = CastChecked<UEdGraphNode_Comment>(GraphNode);
			if (UControlRigGraph* Graph = Cast<UControlRigGraph>(CommentNode->GetOuter()))
			{
				if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Graph->GetOuter()))
				{
					FVector2D Position(CommentNode->NodePosX, CommentNode->NodePosY);
					FVector2D Size(CommentNode->NodeWidth, CommentNode->NodeHeight);
					Blueprint->Controller->OpenUndoBracket(TEXT("Resize Comment Box"));
					Blueprint->Controller->SetNodePositionByName(CommentNode->GetFName(), Position, true);
					Blueprint->Controller->SetNodeSizeByName(CommentNode->GetFName(), Size, true);
					Blueprint->Controller->CloseUndoBracket();
				}
			}
		}
	}
	return Reply;
}

void SControlRigGraphNodeComment::EndUserInteraction() const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	if (GraphNode)
	{
		UEdGraphNode_Comment* CommentNode = CastChecked<UEdGraphNode_Comment>(GraphNode);
		if (UControlRigGraph* Graph = Cast<UControlRigGraph>(CommentNode->GetOuter()))
		{
			if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Graph->GetOuter()))
			{
				if (CommentNode->MoveMode == ECommentBoxMode::GroupMovement)
				{
					Blueprint->Controller->OpenUndoBracket(TEXT("Move Comment Box"));

					for (FCommentNodeSet::TConstIterator NodeIt(CommentNode->GetNodesUnderComment()); NodeIt; ++NodeIt)
					{
						if (UEdGraphNode* EdNode = Cast<UEdGraphNode>(*NodeIt))
						{
							FName NodeName = EdNode->GetFName();
							if (URigVMNode* ModelNode = Blueprint->Model->FindNodeByName(NodeName))
							{
								if (!ModelNode->IsSelected())
								{
									FVector2D Position(EdNode->NodePosX, EdNode->NodePosY);
									Blueprint->Controller->SetNodePositionByName(NodeName, Position, true);
								}
							}
						}
					}
					FVector2D Position(CommentNode->NodePosX, CommentNode->NodePosY);
					Blueprint->Controller->SetNodePositionByName(CommentNode->GetFName(), Position, true);
					Blueprint->Controller->CloseUndoBracket();
				}
				else
				{
					FVector2D Position(CommentNode->NodePosX, CommentNode->NodePosY);
					Blueprint->Controller->SetNodePositionByName(CommentNode->GetFName(), Position, true);
				}
			}
		}
	}

	SGraphNodeComment::EndUserInteraction();
}

void SControlRigGraphNodeComment::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	// catch a renaming action!
	if (GraphNode)
	{
		UEdGraphNode_Comment* CommentNode = CastChecked<UEdGraphNode_Comment>(GraphNode);

		const FString CurrentCommentTitle = GetNodeComment();
		if (CurrentCommentTitle != CachedCommentTitle)
		{
			if (UControlRigGraph* Graph = Cast<UControlRigGraph>(CommentNode->GetOuter()))
			{
				if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Graph->GetOuter()))
				{
					Blueprint->Controller->SetCommentTextByName(CommentNode->GetFName(), CurrentCommentTitle, true);
				}
			}
		}

		if (CachedNodeCommentColor.R < -SMALL_NUMBER)
		{
			CachedNodeCommentColor = CommentNode->CommentColor;
		}
		else
		{
			FLinearColor CurrentNodeCommentColor = CommentNode->CommentColor;
			if (!FVector4(CachedNodeCommentColor - CurrentNodeCommentColor).IsNearlyZero3())
			{
				if (UControlRigGraph* Graph = Cast<UControlRigGraph>(CommentNode->GetOuter()))
				{
					if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Graph->GetOuter()))
					{
						// for now we won't use our undo for this kind of change
						Blueprint->Controller->SetNodeColorByName(GraphNode->GetFName(), CurrentNodeCommentColor, false, true);
						CachedNodeCommentColor = CurrentNodeCommentColor;
					}
				}
			}
		}
	}

	SGraphNodeComment::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}

/*
void SControlRigGraphNodeComment::OnCommentTextCommitted(const FText& NewComment, ETextCommit::Type CommitInfo)
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	if (GraphNode)
	{
		UEdGraphNode_Comment* CommentNode = CastChecked<UEdGraphNode_Comment>(GraphNode);
		if (UControlRigGraph* Graph = Cast<UControlRigGraph>(CommentNode->GetOuter()))
		{
			if (UControlRigBlueprint* Blueprint = Cast<UControlRigBlueprint>(Graph->GetOuter()))
			{
				//FVector2D Position(CommentNode->NodePosX, CommentNode->NodePosY);
				//Blueprint->ModelController->SetNodePosition(CommentNode->GetFName(), Position, true);
			}
		}
	}
}
*/


#undef LOCTEXT_NAMESPACE