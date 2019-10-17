// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ObjectEventsTrack.h"
#include "Insights/ITimingViewDrawHelper.h"
#include "GameplayProvider.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Algo/Sort.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "GameplaySharedData.h"
#include "Insights/ViewModels/TimingEventSearch.h"

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
	FTimingEventSearchParameters SearchParameters(HoveredTimingEvent.StartTime, HoveredTimingEvent.EndTime, ETimingEventSearchFlags::StopAtFirstMatch);

	FindObjectEvent(SearchParameters, [this, &Tooltip, &HoveredTimingEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FObjectEventMessage& InMessage)
	{
		Tooltip.ResetContent();

		Tooltip.AddTitle(FText::FromString(FString(InMessage.Name)).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(HoveredTimingEvent.StartTime).ToString());

		Tooltip.UpdateLayout();
	});
}

bool FObjectEventsTrack::SearchTimingEvent(const FTimingEventSearchParameters& InSearchParameters, FTimingEvent& InOutTimingEvent) const
{
	return FindObjectEvent(InSearchParameters, [this, &InOutTimingEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FObjectEventMessage& InFoundMessage)
	{
		InOutTimingEvent = FTimingEvent(this, InFoundStartTime, InFoundEndTime, InFoundDepth);
	});
}

bool FObjectEventsTrack::FindObjectEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FObjectEventMessage&)> InFoundPredicate) const
{
	// Storage for the message we want to match (payload for an event)
	FObjectEventMessage MatchedMessage;

	return TTimingEventSearch<FObjectEventMessage>::Search(
		InParameters,

		// Search...
		[this](TTimingEventSearch<FObjectEventMessage>::FContext& InContext)
		{
			const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

			if(GameplayProvider)
			{
				Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

				GameplayProvider->ReadObjectEventsTimeline(GetGameplayTrack().GetObjectId(), [&InContext](const FGameplayProvider::ObjectEventsTimeline& InTimeline)
				{
					InTimeline.EnumerateEvents(InContext.GetParameters().StartTime, InContext.GetParameters().EndTime, [&InContext](double InEventStartTime, double InEventEndTime, uint32 InDepth, const FObjectEventMessage& InMessage)
					{
						InContext.Check(InEventStartTime, InEventEndTime, 0, InMessage);
					});
				});
			}
		},

		// Matched...
		[&MatchedMessage](double InStartTime, double InEndTime, uint32 InDepth, const FObjectEventMessage& InEvent)
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