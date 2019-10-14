// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

namespace Insights
{

class ITimingViewSession;
extern TRACEINSIGHTS_API const FName TimingViewExtenderFeatureName;

class ITimingViewExtender : public IModularFeature
{
public:
	virtual ~ITimingViewExtender() = default;

	/** Called to set up any data at the end of the timing view session */
	virtual void OnBeginSession(ITimingViewSession& InSession) = 0;

	/** Called to clear out any data at the end of the timing view session */
	virtual void OnEndSession(ITimingViewSession& InSession) = 0;

	/** Called each frame. If any new tracks are created they can be added via ITimingViewSession::AddTimingEventsTrack() */
	virtual void Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) = 0;

	/** Extension hook for the 'quick filter' menu */
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) = 0;

	/** Called when tracks have changed and need to be re-ordered */
	virtual void OnTracksChanged(Insights::ITimingViewSession& InSession, int32& InOutOrder) = 0;
};

}