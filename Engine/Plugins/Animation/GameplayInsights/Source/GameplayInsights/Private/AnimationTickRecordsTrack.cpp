// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "AnimationTickRecordsTrack.h"
#include "Insights/ITimingViewDrawHelper.h"
#include "GameplayProvider.h"
#include "AnimationProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
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
	Tooltip.ResetContent();

	Tooltip.AddTitle(GetName());

	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);
	if(AnimationProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		AnimationProvider->ReadTickRecordMessage(GetGameplayTrack().GetObjectId(), GetAssetId(), HoveredTimingEvent.TypeId, [this, &Tooltip, &HoveredTimingEvent](const FTickRecordMessage& InMessage)
		{
			Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(HoveredTimingEvent.StartTime).ToString());
			Tooltip.AddNameValueTextLine(LOCTEXT("BlendWeight", "Blend Weight").ToString(), FText::AsNumber(InMessage.BlendWeight).ToString());
			Tooltip.AddNameValueTextLine(LOCTEXT("PlaybackTime", "Playback Time").ToString(), FText::AsNumber(InMessage.PlaybackTime).ToString());
			Tooltip.AddNameValueTextLine(LOCTEXT("RootMotionWeight", "Root Motion Weight").ToString(), FText::AsNumber(InMessage.RootMotionWeight).ToString());
			Tooltip.AddNameValueTextLine(LOCTEXT("PlayRate", "Play Rate").ToString(), FText::AsNumber(InMessage.PlayRate).ToString());
			Tooltip.AddNameValueTextLine(LOCTEXT("Looping", "Looping").ToString(), InMessage.Looping ? LOCTEXT("True", "True").ToString() : LOCTEXT("False", "False").ToString());
		});
	}

	Tooltip.UpdateLayout();
}

bool FAnimationTickRecordsTrack::SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const
{
	struct FSearchTimingEventContext
	{
		const double StartTime;
		const double EndTime;
		TFunctionRef<bool(double, double, uint32)> Predicate;
		FTimingEvent& TimingEvent;
		const bool bStopAtFirstMatch;
		const bool bSearchForLargestEvent;
		mutable bool bFound;
		mutable bool bContinueSearching;
		mutable double LargestDuration;

		FSearchTimingEventContext(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent)
			: StartTime(InStartTime)
			, EndTime(InEndTime)
			, Predicate(InPredicate)
			, TimingEvent(InOutTimingEvent)
			, bStopAtFirstMatch(bInStopAtFirstMatch)
			, bSearchForLargestEvent(bInSearchForLargestEvent)
			, bFound(false)
			, bContinueSearching(true)
			, LargestDuration(-1.0)
		{
		}

		void CheckMessage(double EventStartTime, double EventEndTime, uint32 EventDepth, uint64 InMessageId)
		{
			if (bContinueSearching && Predicate(EventStartTime, EventEndTime, EventDepth))
			{
				if (!bSearchForLargestEvent || EventEndTime - EventStartTime > LargestDuration)
				{
					LargestDuration = EventEndTime - EventStartTime;

					TimingEvent.TypeId = InMessageId;
					TimingEvent.Depth = EventDepth;
					TimingEvent.StartTime = EventStartTime;
					TimingEvent.EndTime = EventEndTime;

					bFound = true;
					bContinueSearching = !bStopAtFirstMatch || bSearchForLargestEvent;
				}
			}
		}
	};

	const FAnimationProvider* AnimationProvider = SharedData.GetAnalysisSession().ReadProvider<FAnimationProvider>(FAnimationProvider::ProviderName);

	if(AnimationProvider)
	{
		FSearchTimingEventContext Context(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent);

		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		AnimationProvider->ReadTickRecordTimeline(GetGameplayTrack().GetObjectId(), GetAssetId(), [this, &Context, &InStartTime, &InEndTime](const FAnimationProvider::TickRecordTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InStartTime, InEndTime, [&Context](double InEventStartTime, double InEventEndTime, uint32 InDepth, const FTickRecordMessage& InMessage)
			{
				Context.CheckMessage(InEventStartTime, InEventEndTime, 0, InMessage.MessageId);
			});
		});
	}

	return false;
}

#undef LOCTEXT_NAMESPACE