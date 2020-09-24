// Copyright Epic Games, Inc. All Rights Reserved.

#include "UITests.h"

#include "Insights/Common/Stopwatch.h"

#include "TraceServices/Model/TimingProfiler.h"
#include "Insights/InsightsManager.h"
#include "Insights/TimingProfilerManager.h"
#include "Insights/Widgets/STimingProfilerWindow.h"

////////////////////////////////////////////////////////////////////////////////////////////////////

#if !WITH_EDITOR

////////////////////////////////////////////////////////////////////////////////////////////////////

DEFINE_LOG_CATEGORY(UITests);

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FHideAndShowAllTimingViewTabs::RunTest(const FString& Parameters)
{
	TSharedPtr<FTimingProfilerManager> TimingProfilerManager = FTimingProfilerManager::Get();

	TimingProfilerManager->ShowHideTimingView(false);
	TimingProfilerManager->ShowHideCalleesTreeView(false);
	TimingProfilerManager->ShowHideCallersTreeView(false);
	TimingProfilerManager->ShowHideFramesTrack(false);
	TimingProfilerManager->ShowHideLogView(false);
	TimingProfilerManager->ShowHideTimersView(false);

	TimingProfilerManager->ShowHideTimingView(true);
	TimingProfilerManager->ShowHideCalleesTreeView(true);
	TimingProfilerManager->ShowHideCallersTreeView(true);
	TimingProfilerManager->ShowHideFramesTrack(true);
	TimingProfilerManager->ShowHideLogView(true);
	TimingProfilerManager->ShowHideTimersView(true);

	return true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#endif // !WITH_EDITOR
