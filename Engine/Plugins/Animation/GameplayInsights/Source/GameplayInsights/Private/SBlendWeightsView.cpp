// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBlendWeightsView.h"
#include "AnimationProvider.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "GameplayProvider.h"
#include "Styling/SlateIconFinder.h"
#include "TraceServices/Model/Frames.h"
#include "VariantTreeNode.h"

#if WITH_EDITOR
#include "Animation/AnimInstance.h"
#endif

#define LOCTEXT_NAMESPACE "SBlendWeightsView"

void SBlendWeightsView::GetVariantsAtFrame(const TraceServices::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutVariants) const
{
	const FGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	const FAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(GameplayProvider && AnimationProvider)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		AnimationProvider->ReadTickRecordTimeline(ObjectId, [&GameplayProvider, &OutVariants, &InFrame](const FAnimationProvider::TickRecordTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InFrame.StartTime, InFrame.EndTime, [&GameplayProvider, &OutVariants, &InFrame](double InStartTime, double InEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
			{
				if(InStartTime >= InFrame.StartTime && InEndTime <= InFrame.EndTime)
				{
					const FClassInfo& ClassInfo = GameplayProvider->GetClassInfoFromObject(InMessage.AssetId);
					TSharedRef<FVariantTreeNode> Header = OutVariants.Add_GetRef(FVariantTreeNode::MakeObject(FText::FromString(ClassInfo.Name), InMessage.AssetId, InMessage.AssetId));
					
					Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("BlendWeight", "Blend Weight"), InMessage.BlendWeight));
					Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("PlaybackTime", "Playback Time"), InMessage.PlaybackTime));
					Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("RootMotionWeight", "Root Motion Weight"), InMessage.RootMotionWeight));
					Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("PlayRate", "Play Rate"), InMessage.PlayRate));
					if(InMessage.bIsBlendSpace)
					{
						Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("BlendSpacePositionX", "Blend Space Position X"), InMessage.BlendSpacePositionX));
						Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("BlendSpacePositionY", "Blend Space Position Y"), InMessage.BlendSpacePositionY));
						Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("BlendSpaceFilteredPositionX", "Blend Space Filtered Position X"), InMessage.BlendSpaceFilteredPositionX));
						Header->AddChild(FVariantTreeNode::MakeFloat(LOCTEXT("BlendSpaceFilteredPositionY", "Blend Space Filtered Position Y"), InMessage.BlendSpaceFilteredPositionY));
					}
				}
				return TraceServices::EEventEnumerate::Continue;
			});
		});
	}
}


static const FName BlendWeightsName("BlendWeights");

FName SBlendWeightsView::GetName() const
{
	return BlendWeightsName;
}

FName FBlendWeightsViewCreator::GetTargetTypeName() const
{
	static FName TargetTypeName = "AnimInstance";
	return TargetTypeName;
}

FName FBlendWeightsViewCreator::GetName() const
{
	return BlendWeightsName;
}

FText FBlendWeightsViewCreator::GetTitle() const
{
	return LOCTEXT("Blend Weights", "Blend Weights");
}

FSlateIcon FBlendWeightsViewCreator::GetIcon() const
{
#if WITH_EDITOR
	return FSlateIconFinder::FindIconForClass(UAnimInstance::StaticClass());
#else
	return FSlateIcon();
#endif
}

TSharedPtr<IRewindDebuggerView> FBlendWeightsViewCreator::CreateDebugView(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& AnalysisSession) const
{
	return SNew(SBlendWeightsView, ObjectId, CurrentTime, AnalysisSession);
}


#undef LOCTEXT_NAMESPACE
