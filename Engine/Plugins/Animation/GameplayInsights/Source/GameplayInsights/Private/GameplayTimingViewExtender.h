// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Insights/ITimingViewExtender.h"

namespace Insights { class ITimingViewSession; }
class UWorld;
class FGameplaySharedData;
class FAnimationSharedData;

class FGameplayTimingViewExtender : public Insights::ITimingViewExtender
{
public:
	// Insights::ITimingViewExtender interface
	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	// Tick the visualizers
	void TickVisualizers(float DeltaTime);

private:
	struct FPerSessionData
	{
		// Shared data
		FGameplaySharedData* GameplaySharedData;
		FAnimationSharedData* AnimationSharedData;
	};

	// The data we host per-session
	TMap<Insights::ITimingViewSession*, FPerSessionData> PerSessionDataMap;
};
