// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ObjectEventsTrack.h"
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

void FObjectEventsTrack::BuildDrawState(ITimingEventsTrackDrawStateBuilder& Builder, const ITimingTrackUpdateContext& Context)
{
	const FGameplayProvider* GameplayProvider = SharedData.GetAnalysisSession().ReadProvider<FGameplayProvider>(FGameplayProvider::ProviderName);

	if(GameplayProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(SharedData.GetAnalysisSession());

		// object events
		GameplayProvider->ReadObjectEventsTimeline(GetGameplayTrack().GetObjectId(), [&Context, &Builder](const FGameplayProvider::ObjectEventsTimeline& InTimeline)
		{
			InTimeline.EnumerateEvents(Context.GetViewport().GetStartTime(), Context.GetViewport().GetEndTime(), [&Builder](double InStartTime, double InEndTime, uint32 InDepth, const FObjectEventMessage& InMessage)
			{
				Builder.AddEvent(InStartTime, InEndTime, 0, InMessage.Name);
			});
		});
	}
}

void FObjectEventsTrack::Draw(const ITimingTrackDrawContext& Context) const
{
	DrawEvents(Context);
	GetGameplayTrack().DrawHeaderForTimingTrack(Context, *this);
}

void FObjectEventsTrack::InitTooltip(FTooltipDrawState& Tooltip, const ITimingEvent& HoveredTimingEvent) const
{
	FTimingEventSearchParameters SearchParameters(HoveredTimingEvent.GetStartTime(), HoveredTimingEvent.GetEndTime(), ETimingEventSearchFlags::StopAtFirstMatch);

	FindObjectEvent(SearchParameters, [this, &Tooltip, &HoveredTimingEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FObjectEventMessage& InMessage)
	{
		Tooltip.ResetContent();

		Tooltip.AddTitle(FText::FromString(FString(InMessage.Name)).ToString());
		Tooltip.AddNameValueTextLine(LOCTEXT("EventTime", "Time").ToString(), FText::AsNumber(HoveredTimingEvent.GetStartTime()).ToString());

		Tooltip.UpdateLayout();
	});
}

const TSharedPtr<const ITimingEvent> FObjectEventsTrack::SearchEvent(const FTimingEventSearchParameters& InSearchParameters) const
{
	TSharedPtr<const ITimingEvent> FoundEvent;

	FindObjectEvent(InSearchParameters, [this, &FoundEvent](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FObjectEventMessage& InFoundMessage)
	{
		FoundEvent = MakeShared<const FTimingEvent>(SharedThis(this), InFoundStartTime, InFoundEndTime, InFoundDepth);
	});

	return FoundEvent;
}

void FObjectEventsTrack::FindObjectEvent(const FTimingEventSearchParameters& InParameters, TFunctionRef<void(double, double, uint32, const FObjectEventMessage&)> InFoundPredicate) const
{
	TTimingEventSearch<FObjectEventMessage>::Search(
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

		// Found!
		[&InFoundPredicate](double InFoundStartTime, double InFoundEndTime, uint32 InFoundDepth, const FObjectEventMessage& InEvent)
		{
			InFoundPredicate(InFoundStartTime, InFoundEndTime, InFoundDepth, InEvent);
		});
}

#undef LOCTEXT_NAMESPACE