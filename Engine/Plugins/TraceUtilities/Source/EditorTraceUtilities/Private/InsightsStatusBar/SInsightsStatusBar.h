// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"

class FMenuBuilder;

TSharedRef<SWidget> CreateInsightsStatusBarWidget();

enum class ETraceDestination : uint32
{
	TraceStore = 0,
	File = 1
};

/**
 *  Status bar widget for Unreal Insights.
 *  Shows buttons to start tracing either to a file or to the trace store and allows saving a snapshot to file.
 */
class SInsightsStatusBarWidget : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SInsightsStatusBarWidget) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	FText	GetTitleToolTipText() const;
	
	FSlateColor GetRecordingButtonColor() const;
	FSlateColor GetRecordingButtonOutlineColor() const;
	FText GetRecordingButtonTooltipText() const;

	void LaunchUnrealInsights_OnClicked();
	
	void OpenLiveSession_OnClicked();

	void OpenProfilingDirectory_OnClicked();
	void OpenTraceStoreDirectory_OnClicked();

	void SetTraceDestination_Execute(ETraceDestination InDestination);
	bool SetTraceDestination_CanExecute();
	bool SetTraceDestination_IsChecked(ETraceDestination InDestination);

	void SaveSnapshot();
	bool SaveSnapshot_CanExecute();

	FText GetTraceMenuItemText() const;
	FText GetTraceMenuItemTooltipText() const;

	void ToggleTracing_OnClicked();

	EVisibility GetStartTraceIconVisibility() const;
	EVisibility GetStopTraceIconVisibility() const;

	void StartTracing();

	TSharedRef<SWidget> MakeTraceMenu();
	void Channels_BuildMenu(FMenuBuilder& MenuBuilder);

	static void SendSnapshotNotification();
	static void SendTraceStartedNotification();

	void SetTraceChannels(const TCHAR* InChannels);
	bool IsPresetSet(const TCHAR* InChannels) const;

private:
	static const TCHAR* DefaultPreset;
	static const TCHAR* MemoryPreset;
	static const TCHAR* TaskGraphPreset;
	static const TCHAR* ContextSwitchesPreset;

	ETraceDestination TraceDestination = ETraceDestination::TraceStore;
	bool bIsTraceRecordButtonHovered = false;
	const TCHAR* Channels;
	mutable double ConnectionStartTime = 0.0f;
};
