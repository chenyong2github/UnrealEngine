// Copyright Epic Games, Inc. All Rights Reserved.

#include "STrackVariantValueView.h"
#include "SVariantValueView.h"
#include "Insights/ViewModels/BaseTimingTrack.h"
#include "GameplayTrack.h"
#include "GameplayGraphTrack.h"
#include "Insights/ITimingViewSession.h"

void STrackVariantValueView::Construct(const FArguments& InArgs, const TSharedRef<FBaseTimingTrack>& InTimingTrack, Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	TimingTrack = InTimingTrack;
	AnalysisSession = &InAnalysisSession;

	InTimingViewSession.OnTimeMarkerChanged().AddSP(this, &STrackVariantValueView::HandleTimeMarkerChanged);

	ChildSlot
	[
		SAssignNew(VariantValueView, SVariantValueView, InAnalysisSession)
		.OnGetVariantValues_Lambda([this](const Trace::FFrame& InFrame, TArray<TSharedRef<FVariantTreeNode>>& OutValues)
		{
			TSharedPtr<FBaseTimingTrack> PinnedTrack = TimingTrack.Pin();
			if(PinnedTrack.IsValid())
			{
				if(PinnedTrack->Is<FGameplayTimingEventsTrack>())
				{
					StaticCastSharedPtr<FGameplayTimingEventsTrack>(PinnedTrack)->GetVariantsAtFrame(InFrame, OutValues);
				}
				else if(PinnedTrack->Is<FGameplayGraphTrack>())
				{
					StaticCastSharedPtr<FGameplayGraphTrack>(PinnedTrack)->GetVariantsAtFrame(InFrame, OutValues);
				}
			}
		})
	];

	Trace::FAnalysisSessionReadScope SessionReadScope(InAnalysisSession);

	const Trace::IFrameProvider& FramesProvider = Trace::ReadFrameProvider(InAnalysisSession);
	Trace::FFrame MarkerFrame;
	if(FramesProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, InTimingViewSession.GetTimeMarker(), MarkerFrame))
	{
		VariantValueView->RequestRefresh(MarkerFrame);
	}
}

void STrackVariantValueView::HandleTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker)
{
	Trace::FAnalysisSessionReadScope SessionReadScope(*AnalysisSession);

	const Trace::IFrameProvider& FramesProvider = Trace::ReadFrameProvider(*AnalysisSession);
	Trace::FFrame MarkerFrame;
	if(FramesProvider.GetFrameFromTime(ETraceFrameType::TraceFrameType_Game, InTimeMarker, MarkerFrame))
	{
		VariantValueView->RequestRefresh(MarkerFrame);
	}
}
