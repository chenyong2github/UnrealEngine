// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAnimationCurvesView.h"
#include "AnimationProvider.h"
#include "TraceServices/Model/AnalysisSession.h"
#include "GameplayProvider.h"
#include "Styling/SlateIconFinder.h"
#include "TraceServices/Model/Frames.h"
#include "VariantTreeNode.h"

#if WITH_EDITOR
#include "Animation/AnimInstance.h"
#endif

#define LOCTEXT_NAMESPACE "SAnimationCurvesView"

void SAnimationCurvesView::GetVariantsAtFrame(const TraceServices::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutVariants) const
{
	const FGameplayProvider* GameplayProvider = AnalysisSession->ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	const FAnimationProvider* AnimationProvider = AnalysisSession->ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(GameplayProvider && AnimationProvider)
	{
		TraceServices::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

		TSharedRef<FVariantTreeNode> Header = OutVariants.Add_GetRef(FVariantTreeNode::MakeHeader(LOCTEXT("Animation Curves", "Animation Curves"), 0));

		AnimationProvider->ReadSkeletalMeshPoseTimeline(ObjectId, [&Header, &AnimationProvider, &InFrame](const FAnimationProvider::SkeletalMeshPoseTimeline& InTimeline, bool bInHasCurves)
		{
			InTimeline.EnumerateEvents(InFrame.StartTime, InFrame.EndTime, [&Header, &AnimationProvider, &InFrame](double InStartTime, double InEndTime, uint32 InDepth, const FSkeletalMeshPoseMessage& InMessage)
			{
				if(InStartTime >= InFrame.StartTime && InStartTime <= InFrame.EndTime)
				{
					AnimationProvider->EnumerateSkeletalMeshCurves(InMessage, [&Header, &AnimationProvider](const FSkeletalMeshNamedCurve& InCurve)
					{
						const TCHAR* CurveName = AnimationProvider->GetName(InCurve.Id);
						Header->AddChild(FVariantTreeNode::MakeFloat(FText::FromString(CurveName), InCurve.Value));
					});
				}
				return TraceServices::EEventEnumerate::Continue;
			});
		});
	}
}

FName FAnimationCurvesViewCreator::GetTargetTypeName() const
{
	static FName TargetTypeName = "SkeletalMeshComponent";
	return TargetTypeName;
}

static const FName AnimationCurvesName("AnimationCurves");

FName SAnimationCurvesView::GetName() const
{
	return AnimationCurvesName;
}

FName FAnimationCurvesViewCreator::GetName() const
{
	return AnimationCurvesName;
}

FText FAnimationCurvesViewCreator::GetTitle() const
{
	return LOCTEXT("Animation Curves", "Animation Curves");
}

FSlateIcon FAnimationCurvesViewCreator::GetIcon() const
{
#if WITH_EDITOR
	return FSlateIconFinder::FindIconForClass(UAnimInstance::StaticClass());
#else
	return FSlateIcon();
#endif
}

TSharedPtr<IRewindDebuggerView> FAnimationCurvesViewCreator::CreateDebugView(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& AnalysisSession) const
{
	return SNew(SAnimationCurvesView, ObjectId, CurrentTime, AnalysisSession);
}


#undef LOCTEXT_NAMESPACE
