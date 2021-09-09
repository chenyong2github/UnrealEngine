// Copyright Epic Games, Inc. All Rights Reserved.

#include "AnimationNodes/SGraphNodeBlendSpacePlayer.h"
#include "SBlendSpacePreview.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "SLevelOfDetailBranchNode.h"
#include "Widgets/Layout/SSpacer.h"

void SGraphNodeBlendSpacePlayer::Construct(const FArguments& InArgs, UAnimGraphNode_Base* InNode)
{
	this->GraphNode = InNode;

	this->SetCursor(EMouseCursor::CardinalCross);

	this->UpdateGraphNode();

	CachedSyncGroupName = NAME_None;

	SAnimationGraphNode::Construct(SAnimationGraphNode::FArguments(), InNode);

	RegisterActiveTimer(1.0f / 60.0f, FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime)
	{
		UpdateGraphSyncLabel();
		return EActiveTimerReturnType::Continue;
	}));
}

void SGraphNodeBlendSpacePlayer::CreateBelowPinControls(TSharedPtr<SVerticalBox> MainBox)
{
	SAnimationGraphNode::CreateBelowPinControls(MainBox);

	// Insert above the error reporting bar (but above the tag/functions)
	MainBox->InsertSlot(FMath::Max(0, MainBox->NumSlots() - DebugGridSlotReverseIndex))
	.AutoHeight()
	.VAlign(VAlign_Fill)
	.Padding(0.0f)
	[
		SNew(SLevelOfDetailBranchNode)
		.UseLowDetailSlot(this, &SGraphNodeBlendSpacePlayer::UseLowDetailNodeTitles)
		.LowDetail()
		[
			SNew(SSpacer)
			.Size(FVector2D(100.0f, 100.f))
		]
		.HighDetail()
		[
			SNew(SBlendSpacePreview, CastChecked<UAnimGraphNode_Base>(GraphNode))
		]
	];
}

void SGraphNodeBlendSpacePlayer::UpdateGraphSyncLabel()
{
	if (UAnimGraphNode_BlendSpacePlayer* VisualBlendSpacePlayer = Cast<UAnimGraphNode_BlendSpacePlayer>(GraphNode))
	{
		FName CurrentSyncGroupName = NAME_None;

		if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(FBlueprintEditorUtils::FindBlueprintForNode(GraphNode)))
		{
			if(UAnimBlueprintGeneratedClass* GeneratedClass = AnimBlueprint->GetAnimBlueprintGeneratedClass())
			{
				if (UObject* ActiveObject = AnimBlueprint->GetObjectBeingDebugged())
				{
					if(VisualBlendSpacePlayer->Node.GetGroupMethod() == EAnimSyncMethod::Graph)
					{
						int32 NodeIndex = GeneratedClass->GetNodeIndexFromGuid(VisualBlendSpacePlayer->NodeGuid);
						if(NodeIndex != INDEX_NONE)
						{
							if(const FName* SyncGroupNamePtr = GeneratedClass->GetAnimBlueprintDebugData().NodeSyncsThisFrame.Find(NodeIndex))
							{
								CurrentSyncGroupName = *SyncGroupNamePtr;
							}
						}
					}
				}
			}
		}

		if(CachedSyncGroupName != CurrentSyncGroupName)
		{
			// Invalidate the node title so we can dynamically display the sync group gleaned from the graph
			VisualBlendSpacePlayer->OnNodeTitleChangedEvent().Broadcast();
			CachedSyncGroupName = CurrentSyncGroupName;
		}
	}
}
