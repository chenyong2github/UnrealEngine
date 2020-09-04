// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// Insights
#include "Insights/ITimingViewExtender.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryGraphTrack.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTag.h"

class FTimingEventSearchParameters;
class FTimingGraphSeries;
class FTimingGraphTrack;
class STimingView;

namespace Insights
{
	struct FReportConfig;
	struct FReportTypeConfig;
	struct FReportTypeGraphConfig;
	class FMemoryTracker;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

class FMemorySharedState : public Insights::ITimingViewExtender, public TSharedFromThis<FMemorySharedState>
{
public:
	FMemorySharedState();
	virtual ~FMemorySharedState();

	TSharedPtr<STimingView> GetTimingView() const { return TimingView; }
	void SetTimingView(TSharedPtr<STimingView> InTimingView) { TimingView = InTimingView; }

	const Insights::FMemoryTagList& GetTagList() { return TagList; }

	const TArray<TSharedPtr<Insights::FMemoryTracker>>& GetTrackers()  const { return Trackers; }
	TSharedPtr<Insights::FMemoryTracker> GetCurrentTracker() const { return CurrentTracker; }
	void SetCurrentTracker(TSharedPtr<Insights::FMemoryTracker> Tracker) { CurrentTracker = Tracker; OnTrackerChanged(); }
	FString TrackersToString(uint64 Flags, const TCHAR* Conjunction) const;

	TSharedPtr<FMemoryGraphTrack> GetMainGraphTrack() const { return MainGraphTrack; }

	EMemoryTrackHeightMode GetTrackHeightMode() const { return TrackHeightMode; }
	void SetTrackHeightMode(EMemoryTrackHeightMode InTrackHeightMode);

	// ITimingViewExtender
	virtual void OnBeginSession(Insights::ITimingViewSession& InSession) override;
	virtual void OnEndSession(Insights::ITimingViewSession& InSession) override;
	virtual void Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession) override;
	virtual void ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InMenuBuilder) override;

	bool IsAllMemoryTracksToggleOn() const { return bShowHideAllMemoryTracks; }
	void SetAllMemoryTracksToggle(bool bOnOff);
	void ShowAllMemoryTracks() { SetAllMemoryTracksToggle(true); }
	void HideAllMemoryTracks() { SetAllMemoryTracksToggle(false); }
	void ShowHideAllMemoryTracks() { SetAllMemoryTracksToggle(!IsAllMemoryTracksToggleOn()); }

	void CreateDefaultTracks();

	TSharedPtr<FMemoryGraphTrack> CreateMemoryGraphTrack();
	int32 RemoveMemoryGraphTrack(TSharedPtr<FMemoryGraphTrack> GraphTrack);
	int32 RemoveAllMemoryGraphTracks();

	TSharedPtr<FMemoryGraphTrack> GetMemTagGraphTrack(Insights::FMemoryTagId MemTagId);
	TSharedPtr<FMemoryGraphTrack> CreateMemTagGraphTrack(Insights::FMemoryTagId MemTagId);
	void RemoveTrackFromMemTags(TSharedPtr<FMemoryGraphTrack>& GraphTrack);
	int32 RemoveMemTagGraphTrack(Insights::FMemoryTagId MemTagId);
	int32 RemoveUnusedMemTagGraphTracks();

	TSharedPtr<FMemoryGraphSeries> ToggleMemTagGraphSeries(TSharedPtr<FMemoryGraphTrack> GraphTrack, Insights::FMemoryTagId MemTagId);

	void CreateTracksFromReport(const FString& Filename);
	void CreateTracksFromReport(const Insights::FReportConfig& ReportConfig);
	void CreateTracksFromReport(const Insights::FReportTypeConfig& ReportTypeConfig);

private:
	void SyncTrackers();
	void OnTrackerChanged();
	void SetTrackerIdToAllSeries(TSharedPtr<FMemoryGraphTrack>& GraphTrack, Insights::FMemoryTrackerId TrackerId);
	int32 GetNextMemoryGraphTrackOrder();
	TSharedPtr<FMemoryGraphTrack> CreateGraphTrack(const Insights::FReportTypeGraphConfig& ReportTypeGraphConfig);

private:
	TSharedPtr<STimingView> TimingView;

	Insights::FMemoryTagList TagList;

	TArray<TSharedPtr<Insights::FMemoryTracker>> Trackers;
	TSharedPtr<Insights::FMemoryTracker> DefaultTracker;
	TSharedPtr<Insights::FMemoryTracker> CurrentTracker;

	TSharedPtr<FMemoryGraphTrack> MainGraphTrack; // the Main Memory Graph track
	TSet<TSharedPtr<FMemoryGraphTrack>> AllTracks;

	EMemoryTrackHeightMode TrackHeightMode;

	bool bShowHideAllMemoryTracks;

	TBitArray<> CreatedDefaultTracks;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
