// Copyright Epic Games, Inc. All Rights Reserved.

#include "SInertializationDetailsView.h"
#include "AnimationProvider.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "GameplayProvider.h"
#include "TraceServices/Model/Frames.h"
#include "VariantTreeNode.h"

#define LOCTEXT_NAMESPACE "SInertializationDetailsView"

struct FInertializationDetailsNodeItem
{
	const TCHAR* Type = nullptr;
	float ElapsedTime = 0.0f;
	float Duration = 0.0f;
	float MaxDuration = 0.0f;
	float InertializationWeight = 0.0f;
	bool bActive = false;
	const TCHAR* Request = nullptr;
};

void SInertializationDetailsView::GetVariantsAtFrame(const TraceServices::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutVariants) const
{
	const FGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	const FAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	TSortedMap<int32, FInertializationDetailsNodeItem, TInlineAllocator<8>> NodeMap;

	if(GameplayProvider && AnimationProvider)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		// Find all inertialization nodes within window

		AnimationProvider->ReadAnimNodesTimeline(ObjectId, [this, &NodeMap, &AnimationProvider, &InFrame](const FAnimationProvider::AnimNodesTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InFrame.StartTime, InFrame.EndTime, [this, &NodeMap, &AnimationProvider, &InFrame](double InStartTime, double InEndTime, uint32 InDepth, const FAnimNodeMessage& InMessage)
			{
				if (InStartTime >= InFrame.StartTime && InEndTime <= InFrame.EndTime)
				{
					if (bFilterSet && InMessage.NodeId != NodeIdFilter)
					{
						return TraceServices::EEventEnumerate::Continue;
					}

					for (const TCHAR* InertializationNodeType : { TEXT("AnimNode_DeadBlending"), TEXT("AnimNode_Inertialization") })
					{
						if (FCString::Strcmp(InertializationNodeType, InMessage.NodeTypeName) == 0)
						{
							FInertializationDetailsNodeItem& NodeItemRef = NodeMap.FindOrAdd(InMessage.NodeId);
							NodeItemRef.Type = InMessage.NodeName;
							break;
						}
					}
				}

				return TraceServices::EEventEnumerate::Continue;
			});
		});

		// Fill in Node Trace Details

		AnimationProvider->ReadAnimNodeValuesTimeline(ObjectId, [this, &NodeMap, &AnimationProvider, &InFrame](const FAnimationProvider::AnimNodeValuesTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InFrame.StartTime, InFrame.EndTime, [this, &NodeMap, &AnimationProvider, &InFrame](double InStartTime, double InEndTime, uint32 InDepth, const FAnimNodeValueMessage& InMessage)
			{
				if (InStartTime >= InFrame.StartTime && InEndTime <= InFrame.EndTime)
				{
					if (FInertializationDetailsNodeItem* NodeItem = NodeMap.Find(InMessage.NodeId))
					{
						if (FCString::Strcmp(InMessage.Key, TEXT("State")) == 0)
						{
							NodeItem->bActive = FCString::Strcmp(InMessage.Value.String.Value, TEXT("EInertializationState::Active")) == 0;
						}
						else if (FCString::Strcmp(InMessage.Key, TEXT("Elapsed Time")) == 0)
						{
							NodeItem->ElapsedTime = InMessage.Value.Float.Value;
						}
						else if (FCString::Strcmp(InMessage.Key, TEXT("Duration")) == 0)
						{
							NodeItem->Duration = InMessage.Value.Float.Value;
						}
						else if (FCString::Strcmp(InMessage.Key, TEXT("Max Duration")) == 0)
						{
							NodeItem->MaxDuration = InMessage.Value.Float.Value;
						}
						else if (FCString::Strcmp(InMessage.Key, TEXT("Inertialization Weight")) == 0)
						{
							NodeItem->InertializationWeight = InMessage.Value.Float.Value;
						}
						else if (FCString::Strcmp(InMessage.Key, TEXT("Request")) == 0)
						{
							NodeItem->Request = InMessage.Value.String.Value;
						}
					}
				}
				return TraceServices::EEventEnumerate::Continue;
			});
		});

		// Make UI Variant Items

		for (TPair<int32, FInertializationDetailsNodeItem>& NodePair : NodeMap)
		{
			if (NodePair.Value.bActive)
			{
				TSharedRef<FVariantTreeNode> Header = OutVariants.Add_GetRef(FVariantTreeNode::MakeHeader(NodePair.Value.Type ? FText::FromString(NodePair.Value.Type) : LOCTEXT("UnknownNode", "Unknown Node"), NodePair.Key));
				Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("ElapsedTime", "Elapsed Time"), NodePair.Value.ElapsedTime));
				Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("Duration", "Duration"), NodePair.Value.Duration));
				Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("MaxDuration", "Max Duration"), NodePair.Value.MaxDuration));
				Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("InertializationWeight", "Inertialization Weight"), NodePair.Value.InertializationWeight));
				Header->AddChild(FVariantTreeNode::MakeString(LOCTEXT("RequestNode", "Request Node"), NodePair.Value.Request));
			}
		}
	}
}

static const FName InertializationDetailsName("Inertialization");

FName SInertializationDetailsView::GetName() const
{
	return InertializationDetailsName;
}

#undef LOCTEXT_NAMESPACE
