// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimationTickRecordsTrack.h"
#include "GameplayProvider.h"
#include "AnimationProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "AnimationSharedData.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/TooltipDrawState.h"

#define LOCTEXT_NAMESPACE "AnimationTickRecordsTrack"

const FName FAnimationTickRecordsTrack::TypeName(TEXT("Animation"));
const FName FAnimationTickRecordsTrack::SubTypeName(TEXT("TickRecords"));

FAnimationTickRecordsTrack::FAnimationTickRecordsTrack(const FAnimationSharedData& InSharedData, uint64 InObjectID, uint64 InAssetId, const TCHAR* InName)
	: TGameplayTrackMixin<FTimingEventsTrack>(InObjectID, FAnimationTickRecordsTrack::TypeName, FAnimationTickRecordsTrack::SubTypeName, FText::FromString(FString(InName)))
	, SharedData(InSharedData)
	, AssetId(InAssetId)
{
}

void FAnimationTickRecordsTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(GameplayProvider && AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		AnimationProvider->ReadTickRecordTimeline(GetGameplayTrack().GetObjectId(), GetAssetId(), [this, &GameplayProvider, &AnimationProvider, &Context, &Builder](const FAnimationProvider::TickRecordTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(Context.GetViewport().GetStartTime(), Context.GetViewport().GetEndTime(), [this, &Builder, &GameplayProvider](double InStartTime, double InEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
			{
				Builder.AddEvent(InStartTime, InEndTime, 0, *GetName());
			});
		});
	}
}

void FAnimationTickRecordsTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	DrawEvents(Context);
	GetGameplayTrack().DrawHeaderForTimingTrack(Context, *this);
}

void FAnimationTickRecordsTrack::InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const
{
	FTimingEventSearchParameters SearchParameters(HoveredTimingEvent.GetStartTime(), HoveredTimingEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch);

	FindTickRecordMessage(SearchParameters, [this, &Tooltip](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InMessage)
	{
		Tooltip.ResetContent();

		Tooltip.AddTitle(GetName());

		Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(InFoundStartTime).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("BlendWeight", "Blend Weight").ToString(), FText::AsNumber(InMessage.BlendWeight).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("PlaybackTime", "Playback Time").ToString(), FText::AsNumber(InMessage.PlaybackTime).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("RootMotionWeight", "Root Motion Weight").ToString(), FText::AsNumber(InMessage.RootMotionWeight).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("PlayRate", "Play Rate").ToString(), FText::AsNumber(InMessage.PlayRate).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("Looping", "Looping").ToString(), InMessage.Looping ? LOCTEXT("True", "True").ToString() : LOCTEXT("False", "False").ToString());

		Tooltip.UpdateLayout();
	});
}

const TSharedPtr<const ITimingEvent> FAnimationTickRecordsTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindTickRecordMessage(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InFoundMessage)
	{
		FoundEvent = MakeShared<const FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

void FAnimationTickRecordsTrack::FindTickRecordMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FTickRecordMessage&)> InFoundPredicate) const
{
	TTimingEventSearch<FTickRecordMessage>::Search(
		InParameters,

		[this](TTimingEventSearch<FTickRecordMessage>::FContext& InContext)
		{
			const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

			if(AnimationProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

				AnimationProvider->ReadTickRecordTimeline(GetGameplayTrack().GetObjectId(), GetAssetId(), [this, &InContext](const FAnimationProvider::TickRecordTimeline& InTimeline)
				{
					InTimeline.EnumerateEvents(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](double InEventStartTime, double InEventEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
					{
						InContext.Check(InEventStartTime, InEventEndTime, 0, InMessage);
					});
				});
			}
		},

		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		});
}

#undef LOCTEXT_NAMESPACE