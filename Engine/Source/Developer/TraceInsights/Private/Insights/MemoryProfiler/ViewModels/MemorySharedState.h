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

// Known memory rules.
// The enum uses the following naming convention:
//     A, B, C, D = time markers
//     a = time when "alloc" event occurs
//     f = time when "free" event occurs (can be infinite)
// Ex.: "AaBf" means "all memory allocations allocated between time A and time B and freed after time B".
enum class EMemoryRule
{
	aAf,     // active allocs at A
	afA,     // before
	Aaf,     // after
	aAfB,    // decline
	AaBf,    // growth
	AafB,    // short living allocs
	aABf,    // long living allocs
	AaBCf,   // memory leaks
	AaBfC,   // limited lifetime
	aABfC,   // decline of long living allocs
	AaBCfD,  // specific lifetime
	A_vs_B,  // compare A vs. B; {aAf} vs. {aBf}
	A_or_B,  // live at A or at B; {aAf} U {aBf}
	A_xor_B, // live either at A or at B; ({aAf} U {aBf}) \ {aABf}
};

class FMemoryRuleSpec
{
public:
	FMemoryRuleSpec(EMemoryRule InValue, uint32 InNumTimeMarkers, const FText& InShortName, const FText& InVerboseName, const FText& InDescription)
		: Value(InValue)
		, NumTimeMarkers(InNumTimeMarkers)
		, ShortName(InShortName)
		, VerboseName(InVerboseName)
		, Description(InDescription)
	{}

	EMemoryRule GetValue() const { return Value; }
	uint32 GetNumTimeMarkers() const { return NumTimeMarkers; }
	FText GetShortName() const { return ShortName; }
	FText GetVerboseName() const { return VerboseName; }
	FText GetDescription() const { return Description; }

private:
	EMemoryRule Value;     // ex.: EMemoryRule::AafB
	uint32 NumTimeMarkers; // ex.: 2
	FText ShortName;     // ex.: "A**B"
	FText VerboseName;   // ex.: "Short Living Allocations"
	FText Description;   // ex.: "Allocations allocated and freed between time A and time B (A <= a <= f <= B)."
};

} // namespace Insights

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
	virtual void Tick(Insights::ITimingViewSession& InSession, const TraceServices::IAnalysisSession& InAnalysisSession) override;
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

	const TArray<TSharedPtr<Insights::FMemoryRuleSpec>>& GetMemoryRules() const { return MemoryRules; }
	
	TSharedPtr<Insights::FMemoryRuleSpec> GetCurrentMemoryRule() const { return CurrentMemoryRule; }
	void SetCurrentMemoryRule(TSharedPtr<Insights::FMemoryRuleSpec> InRule) { CurrentMemoryRule = InRule; OnMemoryRuleChanged(); }

private:
	void SyncTrackers();
	void OnTrackerChanged();
	void SetTrackerIdToAllSeries(TSharedPtr<FMemoryGraphTrack>& GraphTrack, Insights::FMemoryTrackerId TrackerId);
	int32 GetNextMemoryGraphTrackOrder();
	TSharedPtr<FMemoryGraphTrack> CreateGraphTrack(const Insights::FReportTypeGraphConfig& ReportTypeGraphConfig);
	void InitMemoryRules();
	void OnMemoryRuleChanged();

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

	TArray<TSharedPtr<Insights::FMemoryRuleSpec>> MemoryRules;
	TSharedPtr<Insights::FMemoryRuleSpec> CurrentMemoryRule;
};

////////////////////////////////////////////////////////////////////////////////////////////////////
