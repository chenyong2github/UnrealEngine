// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"

namespace Trace { class IAnalysisSession; }
class FMenuBuilder;

namespace Insights
{

class ITimingViewSession;
extern TRACEINSIGHTS_API const FName TimingViewExtenderFeatureName;

class TRACEINSIGHTS_API ITimingViewExtender : public IModularFeature
{
public:
	virtual ~ITimingViewExtender() = default;

	/** Called to set up any data at the end of the timing view session */
	virtual void OnBeginSession(ITimingViewSession& InSession) = 0;

	/** Called to clear out any data at the end of the timing view session */
	virtual void OnEndSession(ITimingViewSession& InSession) = 0;

	/** Called each frame. If any new tracks are created they can be added via ITimingViewSession::Add*Track() */
	virtual void Tick(ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) = 0;

	/** Extension hook for the 'quick filter' menu */
	virtual void ExtendFilterMenu(ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) = 0;
};

} // namespace Insights
