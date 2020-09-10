// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ViewModels/TimingEventsTrack.h"
#include "TraceServices/Model/Frames.h"

namespace Trace { class IAnalysisSession; }
namespace Insights { class ITimingViewSession; }
namespace Insights { enum class ETimeChangedFlags : int32; }
namespace UE { namespace SlateInsights { class FSlateFrameGraphTrack; } }
class FMenuBuilder;
class SDockTab;

namespace UE
{
namespace SlateInsights
{ 

class FSlateTimingViewSession
{
public:
	FSlateTimingViewSession();

	void OnBeginSession(Insights::ITimingViewSession& InTimingViewSession);
	void OnEndSession(Insights::ITimingViewSession& InTimingViewSession);
	void Tick(Insights::ITimingViewSession& InTimingViewSession, const Trace::IAnalysisSession& InAnalysisSession);
	void ExtendFilterMenu(FMenuBuilder& InMenuBuilder);

	/** Get the last cached analysis session */
	const Trace::IAnalysisSession& GetAnalysisSession() const { return *AnalysisSession; }

	/** Check whether the analysis session is valid */
	bool IsAnalysisSessionValid() const { return AnalysisSession != nullptr; }

	/** Show/Hide the slate track */
	void ToggleSlateTrack();

	/** Open a resume of the frame */
	void OpenSlateFrameTab() const;

private:
	// Cached analysis session, set in Tick()
	const Trace::IAnalysisSession* AnalysisSession;

	// Cached timing view session, set in OnBeginSession/OnEndSession
	Insights::ITimingViewSession* TimingViewSession;

	// All the tracks we manage
	TSharedPtr<FSlateFrameGraphTrack> SlateFrameGraphTrack;

	// Flags controlling whether check type of our animation tracks are enabled
	bool bApplicationTracksEnabled;
};

} //namespace SlateInsights
} //namespace UE
