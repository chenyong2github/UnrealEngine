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

	InTimingViewSession.OnTimeMarkerChanged().AddSP(this, &STrackVariantValueView::HandleTimeMarkerChanged);

	ChildSlot
	[
		SAssignNew(VariantValueView, SVariantValueView, InAnalysisSession)
		.OnGetVariantValues_Lambda([this](double InTime, TArray<TSharedRef<FVariantTreeNode>>& OutValues)
		{
			TSharedPtr<FBaseTimingTrack> PinnedTrack = TimingTrack.Pin();
			if(PinnedTrack.IsValid())
			{
				if(PinnedTrack->Is<FGameplayTimingEventsTrack>())
				{
					StaticCastSharedPtr<FGameplayTimingEventsTrack>(PinnedTrack)->GetVariantsAtTime(InTime, OutValues);
				}
				else if(PinnedTrack->Is<FGameplayGraphTrack>())
				{
					StaticCastSharedPtr<FGameplayGraphTrack>(PinnedTrack)->GetVariantsAtTime(InTime, OutValues);
				}
			}
		})
	];

	VariantValueView->RequestRefresh(InTimingViewSession.GetTimeMarker());
}

void STrackVariantValueView::HandleTimeMarkerChanged(Insights::ETimeChangedFlags InFlags, double InTimeMarker)
{
	VariantValueView->RequestRefresh(InTimeMarker);
}
