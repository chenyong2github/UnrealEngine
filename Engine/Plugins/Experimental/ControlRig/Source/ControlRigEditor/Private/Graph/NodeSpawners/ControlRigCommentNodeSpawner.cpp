// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ControlRigCommentNodeSpawner.h"
#include "Classes/EditorStyleSettings.h"
#include "Editor/EditorEngine.h"
#include "ObjectEditorUtils.h"
#include "EditorCategoryUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphNode_Comment.h"
#include "BlueprintNodeTemplateCache.h"
#include "ControlRigBlueprintUtils.h"
#include "ControlRig.h"
#include "ControlRigBlueprint.h"
#include "Graph/ControlRigGraph.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

#define LOCTEXT_NAMESPACE "ControlRigCommentNodeSpawner"

UControlRigCommentNodeSpawner* UControlRigCommentNodeSpawner::Create()
{
	UControlRigCommentNodeSpawner* NodeSpawner = NewObject<UControlRigCommentNodeSpawner>(GetTransientPackage());
	NodeSpawner->NodeClass = UEdGraphNode_Comment::StaticClass();

	FBlueprintActionUiSpec& MenuSignature = NodeSpawner->DefaultMenuSignature;
	
	MenuSignature.MenuName = LOCTEXT("Add Comment", "Add Comment for Selection...");
	MenuSignature.Tooltip  = LOCTEXT("CommentTooltip", "Adds a comment box to the graph");
	MenuSignature.Category = FText();

	// add at least one character, so that PrimeDefaultUiSpec() doesn't 
	// attempt to query the template node
	//
	// @TODO: maybe UPROPERTY() fields should have keyword metadata like functions
	if (MenuSignature.Keywords.IsEmpty())
	{
		// want to set it to something so we won't end up back in this condition
		MenuSignature.Keywords = FText::FromString(TEXT(" "));
	}

	// @TODO: should use details customization-like extensibility system to provide editor only data like this
	MenuSignature.Icon = FSlateIcon("EditorStyle", "GraphEditor.Comment_16x");

	return NodeSpawner;
}

void UControlRigCommentNodeSpawner::Prime()
{
	// we expect that you don't need a node template to construct menu entries
	// from this, so we choose not to pre-cache one here
}

FBlueprintNodeSignature UControlRigCommentNodeSpawner::GetSpawnerSignature() const
{
	return FBlueprintNodeSignature(NodeClass);
}

FBlueprintActionUiSpec UControlRigCommentNodeSpawner::GetUiSpec(FBlueprintActionContext const& Context, FBindingSet const& Bindings) const
{
	UEdGraph* TargetGraph = (Context.Graphs.Num() > 0) ? Context.Graphs[0] : nullptr;
	FBlueprintActionUiSpec MenuSignature = PrimeDefaultUiSpec(TargetGraph);

	DynamicUiSignatureGetter.ExecuteIfBound(Context, Bindings, &MenuSignature);
	return MenuSignature;
}

UEdGraphNode* UControlRigCommentNodeSpawner::Invoke(UEdGraph* ParentGraph, FBindingSet const& Bindings, FVector2D const Location) const
{
#if WITH_EDITOR
	if (GEditor)
	{
		GEditor->CancelTransaction(0);
	}
#endif

	UBlueprint* Blueprint = CastChecked<UBlueprint>(ParentGraph->GetOuter());
	if (UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint))
	{
		bool const bIsTemplateNode = FBlueprintNodeTemplateCache::IsTemplateOuter(ParentGraph);

		// First create a backing member for our node
		FName MemberName = NAME_None;

		if (!bIsTemplateNode)
		{
			FVector2D AverageLocation = Location;
			FVector2D Size(400.f, 250.f);

			TArray<FControlRigModelNode> SelectedNodes = RigBlueprint->Model->SelectedNodes();
			if (SelectedNodes.Num() > 0)
			{
				FVector2D TopLeft(1000000.f, 1000000.f);
				FVector2D BottomRight(-1000000.f, -1000000.f);
				const float Margin = 20;

				UControlRigGraph* RigGraph = Cast<UControlRigGraph>(ParentGraph);
				for (const FControlRigModelNode& SelectedNode : SelectedNodes)
				{
					UEdGraphNode* EdNode = RigGraph->FindNodeFromPropertyName(SelectedNode.Name);
					if (EdNode)
					{
						TopLeft.X = FMath::Min<float>(TopLeft.X, float(EdNode->NodePosX) - Margin);
						TopLeft.Y = FMath::Min<float>(TopLeft.Y, float(EdNode->NodePosY) - Margin - 35.f);
						BottomRight.X = FMath::Max<float>(BottomRight.X, float(EdNode->NodePosX) + float(EdNode->NodeWidth) + Margin);
						BottomRight.Y = FMath::Max<float>(BottomRight.Y, float(EdNode->NodePosY) + float(EdNode->NodeHeight) + Margin);
					}
				}
				AverageLocation = TopLeft;
				Size = BottomRight - TopLeft;
			}

			if (RigBlueprint->ModelController->AddComment(TEXT("Comment"), TEXT("Comment"), AverageLocation, Size, FLinearColor::White, true))
			{
				MemberName = RigBlueprint->LastNameFromNotification;
				for (UEdGraphNode* Node : ParentGraph->Nodes)
				{
					if (UEdGraphNode_Comment* CommentNode = Cast<UEdGraphNode_Comment>(Node))
					{
						if (CommentNode->GetFName() == MemberName)
						{
							return CommentNode;
						}
					}
				}
			}
		}
		else
		{
			UEdGraphNode_Comment* NewNode = NewObject<UEdGraphNode_Comment>(ParentGraph, TEXT("Comment"));
			ParentGraph->AddNode(NewNode, false);
			return NewNode;
		}
	}

	return nullptr;
}

bool UControlRigCommentNodeSpawner::IsTemplateNodeFilteredOut(FBlueprintActionFilter const& Filter) const
{
	for (UBlueprint* Blueprint : Filter.Context.Blueprints)
	{
		UControlRigBlueprint* RigBlueprint = Cast<UControlRigBlueprint>(Blueprint);
		if (RigBlueprint == nullptr)
		{
			return true;
		}
	}

	return Super::IsTemplateNodeFilteredOut(Filter);
}

#undef LOCTEXT_NAMESPACE
