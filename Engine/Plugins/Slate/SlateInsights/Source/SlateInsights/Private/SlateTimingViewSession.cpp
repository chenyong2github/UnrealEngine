// Copyright Epic Games, Inc. All Rights Reserved.

#include "SlateTimingViewSession.h"

#include "Insights/ITimingViewSession.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Modules/ModuleManager.h"
#include "TraceServices/Model/AnalysisSession.h"

#include "SlateFrameGraphTrack.h"
#include "SlateInsightsModule.h"
#include "SlateProvider.h"
#include "SlateTimingViewExtender.h"
#include "SSlateFrameSchematicView.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "SlateTimingViewSession"

namespace UE
{
namespace SlateInsights
{

FSlateTimingViewSession::FSlateTimingViewSession()
	: AnalysisSession(nullptr)
	, bApplicationTracksEnabled(true)
{
}

void FSlateTimingViewSession::OnBeginSession(Insights::ITimingViewSession& InTimingViewSession)
{
	TimingViewSession = &InTimingViewSession;

	SlateFrameGraphTrack.Reset();
}

void FSlateTimingViewSession::OnEndSession(Insights::ITimingViewSession& InTimingViewSession)
{
	const bool bInvoke = false;
	if (TSharedPtr<SSlateFrameSchematicView> SchematicView = FSlateInsightsModule::Get().GetSlateFrameSchematicViewTab(bInvoke))
	{
		SchematicView->SetSession(nullptr, nullptr);
	}
	SlateFrameGraphTrack.Reset();
	TimingViewSession = nullptr;
}

void FSlateTimingViewSession::Tick(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	AnalysisSession = &InAnalysisSession;

	const FSlateProvider* SlateProvider = InAnalysisSession.ReadProvider<FSlateProvider>(FSlateProvider::ProviderName);

	if (SlateProvider)
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(GetAnalysisSession());

		// Add "stat" track
		const FSlateProvider::TApplicationTickedTimeline& TickedTimeline = SlateProvider->GetApplicationTickedTimeline();
		if (TickedTimeline.GetEventCount() > 0 && !SlateFrameGraphTrack.IsValid())
		{
			SlateFrameGraphTrack = MakeShared<FSlateFrameGraphTrack>(*this);
			SlateFrameGraphTrack->SetVisibilityFlag(bApplicationTracksEnabled);

			InTimingViewSession.AddScrollableTrack(SlateFrameGraphTrack);

			const bool bInvoke = false;
			if (TSharedPtr<SSlateFrameSchematicView> SchematicView = FSlateInsightsModule::Get().GetSlateFrameSchematicViewTab(bInvoke))
			{
				SchematicView->SetSession(&InTimingViewSession, &InAnalysisSession);
			}
		}
	}
}

void FSlateTimingViewSession::ExtendFilterMenu(FMenuBuilder& InMenuBuilder)
{
	InMenuBuilder.BeginSection("SlateTracks", LOCTEXT("SlateHeader", "Slate"));
	{
		InMenuBuilder.AddMenuEntry(
			LOCTEXT("SlateTimingTracks", "Slate Tracks"),
			LOCTEXT("SlateTimingTracks_Tooltip", "Show/hide the Slate track"),
			FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateRaw(this, &FSlateTimingViewSession::ToggleSlateTrack),
				FCanExecuteAction(),
				FIsActionChecked::CreateLambda([this]() { return bApplicationTracksEnabled; })
				),
			NAME_None,
			EUserInterfaceActionType::ToggleButton
		);
	}
	InMenuBuilder.EndSection();
}

void FSlateTimingViewSession::ToggleSlateTrack()
{
	bApplicationTracksEnabled = !bApplicationTracksEnabled;

	if (SlateFrameGraphTrack)
	{
		SlateFrameGraphTrack->SetVisibilityFlag(bApplicationTracksEnabled);
	}
}

void FSlateTimingViewSession::OpenSlateFrameTab() const
{
	if (TimingViewSession && AnalysisSession)
	{
		const bool bInvoke = true;
		if (TSharedPtr<SSlateFrameSchematicView> SchematicView = FSlateInsightsModule::Get().GetSlateFrameSchematicViewTab(bInvoke))
		{
			SchematicView->SetSession(TimingViewSession, AnalysisSession);
		}
	}
}

} //namespace SlateInsights
} //namespace UE

#undef LOCTEXT_NAMESPACE
