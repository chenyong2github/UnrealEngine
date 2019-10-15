// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ObjectEventsTrack.h"
#include "Insights/ITimingViewDrawHelper.h"
#include "GameplayProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Algo/Sort.h"
#include "Insights/ViewModels/TooltipDrawState.h"

#define LOCTEXT_NAMESPACE "ObjectEventsTrack"

const FName FObjectEventsTrack::TypeName(TEXT("Gameplay"));
const FName FObjectEventsTrack::SubTypeName(TEXT("ObjectEvents"));

FObjectEventsTrack::FObjectEventsTrack(const FGameplaySharedData& InSharedData, uint64 InObjectID, const TCHAR* InName)
	: TGameplayTrackMixin<FTimingEventsTrack>(InObjectID, TypeName, SubTypeName, FText::Format(LOCTEXT("ObjectEventsTrackName", "{0}"), FText::FromString(FString(InName))))
	, SharedData(InSharedData)
{
}

void FObjectEventsTrack::Draw(ITimingViewDrawHelper& Helper) const
{
	FObjectEventsTrack& Track = *const_cast<FObjectEventsTrack*>(this);

	if (Helper.BeginTimeline(Track))
	{
		const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

		if(GameplayProvider)
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

			// object events
			GameplayProvider->ReadObjectEventsTimeline(GetGameplayTrack().GetObjectId(), [&Helper](const FGameplayProvider::ObjectEventsTimeline& InTimeline)
			{
				InTimeline.EnumerateEvents(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), [&Helper](double InStartTime, double InEndTime, uint32 InDepth, const FObjectEventMessage& InMessage)
				{
					Helper.AddEvent(InStartTime, InEndTime, 0, InMessage.Name);
				});
			});
		}

		Helper.EndTimeline(Track);
	}
}

void FObjectEventsTrack::InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const
{
	Tooltip.ResetContent();

	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);
	if(GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		GameplayProvider->ReadObjectEventsTimeline(GetGameplayTrack().GetObjectId(), [&Tooltip, &HoveredTimingEvent](const FGameplayProvider::ObjectEventsTimeline& InTimeline)
		{
			if(HoveredTimingEvent.TypeId < InTimeline.GetEventCount())
			{
				const FObjectEventMessage& Message = InTimeline.GetEvent(HoveredTimingEvent.TypeId);
				Tooltip.AddTitle(FText::FromString(FString(Message.Name)).ToString());
				Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(HoveredTimingEvent.StartTime).ToString());
			}
		});
	}

	Tooltip.UpdateLayout();
}

bool FObjectEventsTrack::SearchTimingEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent) const
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

	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(GameplayProvider)
	{
		FSearchTimingEventContext Context(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent);

		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		GameplayProvider->ReadObjectEventsTimeline(GetGameplayTrack().GetObjectId(), [&Context, &InStartTime, &InEndTime](const FGameplayProvider::ObjectEventsTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(InStartTime, InEndTime, [&Context](double InEventStartTime, double InEventEndTime, uint32 InDepth, const FObjectEventMessage& InMessage)
			{
				Context.CheckMessage(InEventStartTime, InEventEndTime, 0, InMessage.MessageId);
			});
		});
	}

	return false;
}

#undef LOCTEXT_NAMESPACE