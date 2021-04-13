// Copyright Epic Games, Inc. All Rights Reserved.

#include "UITests.h"

#include "Insights/Common/Stopwatch.h"

#include "TraceServices/Model/TimingProfiler.h"
#include "Insights/InsightsManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemAllocFilterValueConverter.h"
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

#endif // !WITH_EDITOR

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FMemoryFilterValueConverterTest::RunTest(const FString& Parameters)
{
	Insights::FMemoryFilterValueConverter Coverter;

	FText Error;
	int64 Value;

	Coverter.Convert(TEXT("152485"), Value, Error);
	TestEqual(TEXT("BasicValue"), 152485LL, Value);

	Coverter.Convert(TEXT("125.56"), Value, Error);
	TestEqual(TEXT("DoubleValue"), 125LL, Value);

	Coverter.Convert(TEXT("3 KiB"), Value, Error);
	TestEqual(TEXT("Kib"), 3072LL, Value);

	Coverter.Convert(TEXT("7.14 KiB"), Value, Error);
	TestEqual(TEXT("KibDouble"), 7311LL, Value);

	Coverter.Convert(TEXT("5 MiB"), Value, Error);
	TestEqual(TEXT("Mib"), 5242880LL, Value);

	Coverter.Convert(TEXT("1 EiB"), Value, Error);
	TestEqual(TEXT("Eib"), 1152921504606846976LL, Value);

	Coverter.Convert(TEXT("2 kib"), Value, Error);
	TestEqual(TEXT("CaseInsesitive"), 2048LL, Value);

	TestFalse(TEXT("Fail1"), Coverter.Convert(TEXT("23test"), Value, Error));
	TestFalse(TEXT("Fail2"), Coverter.Convert(TEXT("43 kOb"), Value, Error));
	TestFalse(TEXT("FailInvalidChar"), Coverter.Convert(TEXT("45,"), Value, Error));

	return !HasAnyErrors();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
