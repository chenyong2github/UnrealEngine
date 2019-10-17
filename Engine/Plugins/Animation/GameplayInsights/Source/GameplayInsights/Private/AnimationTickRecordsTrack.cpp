// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimationTickRecordsTrack.h"
#include "Insights/ITimingViewDrawHelper.h"
#include "GameplayProvider.h"
#include "AnimationProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "AnimationSharedData.h"
#include "Insights/ViewModels/TimingEventSearch.h"

#define LOCTEXT_NAMESPACE "AnimationTickRecordsTrack"

const FName FAnimationTickRecordsTrack::TypeName(TEXT("Animation"));
const FName FAnimationTickRecordsTrack::SubTypeName(TEXT("TickRecords"));

FAnimationTickRecordsTrack::FAnimationTickRecordsTrack(const FAnimationSharedData& InSharedData, uint64 InObjectID, uint64 InAssetId, const TCHAR* InName)
	: TGameplayTrackMixin<FTimingEventsTrack>(InObjectID, FAnimationTickRecordsTrack::TypeName, FAnimationTickRecordsTrack::SubTypeName, FText::FromString(FString(InName)))
	, SharedData(InSharedData)
	, AssetId(InAssetId)
{
}

void FAnimationTickRecordsTrack::Draw(ITimingViewDrawHelper& Helper) const
{
	FAnimationTickRecordsTrack& Track = *const_cast<FAnimationTickRecordsTrack*>(this);

	if (Helper.BeginTimeline(Track))
	{
		const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
		const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

		if(GameplayProvider && AnimationProvider)
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

			AnimationProvider->ReadTickRecordTimeline(GetGameplayTrack().GetObjectId(), GetAssetId(), [this, &GameplayProvider, &AnimationProvider, &Helper](const FAnimationProvider::TickRecordTimeline& InTimeline)
			{
				auto DrawEvents = [this, &Helper, &GameplayProvider](double InStartTime, double InEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
				{
					Helper.AddEvent(InStartTime, InEndTime, 0, *GetName());
				};

				if (FTimingEventsTrack::bUseDownSampling)
				{
					const double SecondsPerPixel = 1.0 / Helper.GetViewport().GetScaleX();
					InTimeline.EnumerateEventsDownSampled(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), SecondsPerPixel, DrawEvents);
				}
				else
				{
					InTimeline.EnumerateEvents(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), DrawEvents);
				}
			});
		}

		Helper.EndTimeline(Track);
	}
}

void FAnimationTickRecordsTrack::InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const
{
	FTimingEventSearchParameters SearchParameters(HoveredTimingEvent.StartTime, HoveredTimingEvent.EndTime, ETimingEventSearchFlags::StopAtFirstMatch);

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

bool FAnimationTickRecordsTrack::SearchTimingEvent(const FTimingEventSearchParameters& InSearchParameters, FTimingEvent& InOutTimingEvent) const
{
	return FindTickRecordMessage(InSearchParameters, [this, &InOutTimingEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FTickRecordMessage& InFoundMessage)
	{
		InOutTimingEvent = FTimingEvent(this, InFoundStartTime, InFoundEndTime, InFoundDepth);
	});
}

bool FAnimationTickRecordsTrack::FindTickRecordMessage(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FTickRecordMessage&)> InFoundPredicate) const
{
	// Storage for the message we want to match (payload for an event)
	FTickRecordMessage MatchedMessage;

	return TTimingEventSearch<FTickRecordMessage>::Search(
		InParameters,

		// Search...
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

		// Matched...
		[&MatchedMessage](double InStartTime, double InEndTime, uint32 InDepth, const FTickRecordMessage& InEvent)
		{
			MatchedMessage = InEvent;
		},

		// Found!
		[&InFoundPredicate, &MatchedMessage](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, MatchedMessage);
		});
}

#undef LOCTEXT_NAMESPACE