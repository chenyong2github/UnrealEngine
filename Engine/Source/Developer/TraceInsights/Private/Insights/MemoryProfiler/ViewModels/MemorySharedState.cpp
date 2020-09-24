// Copyright Epic Games, Inc. All Rights Reserved.

#include "MemorySharedState.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/Model/Memory.h"

// Insights
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/Stopwatch.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/InsightsManager.h"
#include "Insights/MemoryProfiler/MemoryProfilerManager.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTag.h"
#include "Insights/MemoryProfiler/ViewModels/MemoryTracker.h"
#include "Insights/MemoryProfiler/ViewModels/Report.h"
#include "Insights/MemoryProfiler/ViewModels/ReportXmlParser.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingGraphTrack.h"
#include "Insights/ViewModels/TooltipDrawState.h"
#include "Insights/ViewModels/TimingEventSearch.h"
#include "Insights/ViewModels/InsightsMessageLogViewModel.h"
#include "Insights/Widgets/STimingView.h"

#include <limits>

#define LOCTEXT_NAMESPACE "MemorySharedState"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FMemorySharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

FMemorySharedState::FMemorySharedState()
	: TimingView(nullptr)
	, DefaultTracker(nullptr)
	, CurrentTracker(nullptr)
	, MainGraphTrack(nullptr)
	, TrackHeightMode(EMemoryTrackHeightMode::Medium)
	, bShowHideAllMemoryTracks(false)
	, CreatedDefaultTracks()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FMemorySharedState::~FMemorySharedState()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::OnBeginSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView.Get())
	{
		return;
	}

	TagList.Reset();

	Trackers.Reset();
	DefaultTracker = nullptr;
	CurrentTracker = nullptr;

	MainGraphTrack.Reset();
	AllTracks.Reset();

	bShowHideAllMemoryTracks = true;

	CreatedDefaultTracks.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::OnEndSession(Insights::ITimingViewSession& InSession)
{
	if (&InSession != TimingView.Get())
	{
		return;
	}

	TagList.Reset();

	Trackers.Reset();
	DefaultTracker = nullptr;
	CurrentTracker = nullptr;

	MainGraphTrack.Reset();
	AllTracks.Reset();

	bShowHideAllMemoryTracks = false;

	CreatedDefaultTracks.Reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::Tick(Insights::ITimingViewSession& InSession, const Trace::IAnalysisSession& InAnalysisSession)
{
	if (&InSession != TimingView.Get())
	{
		return;
	}

	if (!MainGraphTrack.IsValid())
	{
		MainGraphTrack = CreateMemoryGraphTrack();

		MainGraphTrack->SetOrder(FTimingTrackOrder::First);
		MainGraphTrack->SetName(TEXT("Main Memory Graph"));
		MainGraphTrack->SetVisibilityFlag(bShowHideAllMemoryTracks);

		MainGraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Small, 100.0f);
		MainGraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Medium, 200.0f);
		MainGraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Large, 400.0f);
		MainGraphTrack->SetCurrentTrackHeight(TrackHeightMode);

		TimingView->InvalidateScrollableTracksOrder();
	}

	const int32 PrevTagCount = TagList.GetTags().Num();

	TagList.Update();

	if (!CurrentTracker)
	{
		SyncTrackers();
	}

	if (CurrentTracker)
	{
		CurrentTracker->Update();
	}

	// Scan for mem tags to show as default, but only when new mem tags are added.
	const int32 NewTagCount = TagList.GetTags().Num();
	if (NewTagCount > PrevTagCount)
	{
		CreateDefaultTracks();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::CreateDefaultTracks()
{
	if (!DefaultTracker)
	{
		return;
	}

	const uint64 TrackerFilterMask = 1ULL << static_cast<int32>(DefaultTracker->GetId());

	static const TCHAR* DefaultTags[] =
	{
		TEXT("Total"),
		TEXT("TrackedTotal"),
		TEXT("Untracked"),
		TEXT("Meshes"),
		TEXT("Textures"),
		TEXT("Physics"),
		TEXT("Audio"),
	};
	constexpr int32 DefaultTagCount = UE_ARRAY_COUNT(DefaultTags);

	if (CreatedDefaultTracks.Num() != DefaultTagCount)
	{
		CreatedDefaultTracks.Init(false, DefaultTagCount);
	}

	const auto Tags = TagList.GetTags();
	for (int32 DefaultTagIndex = 0; DefaultTagIndex < DefaultTagCount; ++DefaultTagIndex)
	{
		if (!CreatedDefaultTracks[DefaultTagIndex])
		{
			for (const Insights::FMemoryTag* Tag : Tags)
			{
				if ((Tag->GetTrackers() & TrackerFilterMask) != 0 && // is it used by current tracker?
					Tag->GetGraphTracks().Num() == 0 && // a graph isn't already added for this llm tag?
					FCString::Stricmp(*Tag->GetStatName(), DefaultTags[DefaultTagIndex]) == 0) // is it a llm tag to show as default?
				{
					CreateMemTagGraphTrack(Tag->GetId());
					CreatedDefaultTracks[DefaultTagIndex] = true;
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FString FMemorySharedState::TrackersToString(uint64 Flags, const TCHAR* Conjunction) const
{
	FString Str;
	if (Flags != 0)
	{
		for (const TSharedPtr<Insights::FMemoryTracker>& Tracker : Trackers)
		{
			const uint64 TrackerFlag = 1ULL << Tracker->GetId();
			if ((Flags & TrackerFlag) != 0)
			{
				if (!Str.IsEmpty())
				{
					Str.Append(Conjunction);
				}
				Str.Append(Tracker->GetName());
				Flags &= ~TrackerFlag;
				if (Flags == 0)
				{
					break;
				}
			}
		}
	}
	return Str;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::SyncTrackers()
{
	DefaultTracker = nullptr;
	CurrentTracker = nullptr;
	Trackers.Reset();

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());
		const Trace::IMemoryProvider& MemoryProvider = Trace::ReadMemoryProvider(*Session.Get());

		MemoryProvider.EnumerateTrackers([this](const Trace::FMemoryTracker& Tracker)
		{
			Trackers.Add(MakeShared<Insights::FMemoryTracker>(Tracker.Id, Tracker.Name));
		});

		Trackers.Sort([](const TSharedPtr<Insights::FMemoryTracker>& A, const TSharedPtr<Insights::FMemoryTracker>& B) { return A->GetId() < B->GetId(); });
	}

	if (Trackers.Num() > 0)
	{
		for (const TSharedPtr<Insights::FMemoryTracker>& Tracker : Trackers)
		{
			if (FCString::Stricmp(*Tracker->GetName(), TEXT("Default")) == 0)
			{
				DefaultTracker = Tracker;
				break;
			}
		}

		CurrentTracker = DefaultTracker ? DefaultTracker : Trackers.Last();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::OnTrackerChanged()
{
	if (CurrentTracker != nullptr)
	{
		for (TSharedPtr<FMemoryGraphTrack>& GraphTrack : AllTracks)
		{
			SetTrackerIdToAllSeries(GraphTrack, CurrentTracker->GetId());
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::SetTrackerIdToAllSeries(TSharedPtr<FMemoryGraphTrack>& GraphTrack, Insights::FMemoryTrackerId TrackerId)
{
	for (TSharedPtr<FGraphSeries>& Series : GraphTrack->GetSeries())
	{
		//TODO: if (Series->Is<FMemoryGraphSeries>())
		TSharedPtr<FMemoryGraphSeries> MemorySeries = StaticCastSharedPtr<FMemoryGraphSeries>(Series);
		MemorySeries->SetTrackerId(TrackerId);
		MemorySeries->SetValueRange(0.0, 0.0);
		MemorySeries->SetDirtyFlag();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::SetTrackHeightMode(EMemoryTrackHeightMode InTrackHeightMode)
{
	TrackHeightMode = InTrackHeightMode;

	for (TSharedPtr<FMemoryGraphTrack>& GraphTrack : AllTracks)
	{
		GraphTrack->SetCurrentTrackHeight(InTrackHeightMode);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::ExtendFilterMenu(Insights::ITimingViewSession& InSession, FMenuBuilder& InOutMenuBuilder)
{
	if (&InSession != TimingView.Get())
	{
		return;
	}

	InOutMenuBuilder.BeginSection("Memory", LOCTEXT("MemoryHeading", "Memory"));
	{
		InOutMenuBuilder.AddMenuEntry(
			LOCTEXT("AllMemoryTracks", "Memory Tracks - M"),
			LOCTEXT("AllMemoryTracks_Tooltip", "Show/hide the Memory tracks"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &FMemorySharedState::ShowHideAllMemoryTracks),
					  FCanExecuteAction(),
					  FIsActionChecked::CreateSP(this, &FMemorySharedState::IsAllMemoryTracksToggleOn)),
			NAME_None, //"QuickFilterSeparator",
			EUserInterfaceActionType::ToggleButton
		);
	}
	InOutMenuBuilder.EndSection();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::SetAllMemoryTracksToggle(bool bOnOff)
{
	bShowHideAllMemoryTracks = bOnOff;

	for (TSharedPtr<FMemoryGraphTrack>& GraphTrack : AllTracks)
	{
		GraphTrack->SetVisibilityFlag(bShowHideAllMemoryTracks);
	}

	if (TimingView)
	{
		TimingView->OnTrackVisibilityChanged();
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FMemorySharedState::GetNextMemoryGraphTrackOrder()
{
	int32 Order = FTimingTrackOrder::Memory;
	for (const TSharedPtr<FMemoryGraphTrack>& GraphTrack : AllTracks)
	{
		Order = FMath::Max(Order, GraphTrack->GetOrder() + 1);
	}
	return Order;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FMemoryGraphTrack> FMemorySharedState::CreateMemoryGraphTrack()
{
	if (!TimingView.IsValid())
	{
		return nullptr;
	}

	TSharedPtr<FMemoryGraphTrack> GraphTrack = MakeShared<FMemoryGraphTrack>(*this);

	const int32 Order = GetNextMemoryGraphTrackOrder();
	GraphTrack->SetOrder(Order);
	GraphTrack->SetName(TEXT("Memory Graph"));
	GraphTrack->SetVisibilityFlag(bShowHideAllMemoryTracks);

	GraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Small, 100.0f);
	GraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Medium, 300.0f);
	GraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Large, 600.0f);
	GraphTrack->SetCurrentTrackHeight(TrackHeightMode);

	GraphTrack->SetLabelUnit(EGraphTrackLabelUnit::MiB, 1);
	GraphTrack->EnableAutoZoom();

	TimingView->AddScrollableTrack(GraphTrack);
	AllTracks.Add(GraphTrack);

	return GraphTrack;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FMemorySharedState::RemoveMemoryGraphTrack(TSharedPtr<FMemoryGraphTrack> GraphTrack)
{
	if (!GraphTrack)
	{
		return 0;
	}

	if (GraphTrack == MainGraphTrack)
	{
		RemoveTrackFromMemTags(GraphTrack);
		GraphTrack->RemoveAllMemTagSeries();
		GraphTrack->Hide();
		TimingView->OnTrackVisibilityChanged();
		return -1;
	}

	if (AllTracks.Remove(GraphTrack))
	{
		RemoveTrackFromMemTags(GraphTrack);
		GraphTrack->RemoveAllMemTagSeries();
		TimingView->RemoveTrack(GraphTrack);
		return 1;
	}

	return 0;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::RemoveTrackFromMemTags(TSharedPtr<FMemoryGraphTrack>& GraphTrack)
{
	for (TSharedPtr<FGraphSeries>& Series : GraphTrack->GetSeries())
	{
		//TODO: if (Series->Is<FMemoryGraphSeries>())
		TSharedPtr<FMemoryGraphSeries> MemorySeries = StaticCastSharedPtr<FMemoryGraphSeries>(Series);
		Insights::FMemoryTag* TagPtr = TagList.GetTagById(MemorySeries->GetTagId());
		if (TagPtr)
		{
			TagPtr->RemoveTrack(GraphTrack);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FMemorySharedState::RemoveAllMemoryGraphTracks()
{
	if (!TimingView.IsValid() || !CurrentTracker)
	{
		return -1;
	}

	int32 TrackCount = 0;

	for (TSharedPtr<FMemoryGraphTrack>& GraphTrack : AllTracks)
	{
		GraphTrack->RemoveAllMemTagSeries();
		if (GraphTrack != MainGraphTrack)
		{
			++TrackCount;
			TimingView->RemoveTrack(GraphTrack);
		}
	}

	AllTracks.Reset();

	// Hide the MainGraphTrack instead of removing it.
	if (MainGraphTrack.IsValid())
	{
		AllTracks.Add(MainGraphTrack);
		MainGraphTrack->Hide();
		TimingView->OnTrackVisibilityChanged();
	}

	for (Insights::FMemoryTag* TagPtr : TagList.GetTags())
	{
		TagPtr->RemoveAllTracks();
	}

	return TrackCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FMemoryGraphTrack> FMemorySharedState::GetMemTagGraphTrack(Insights::FMemoryTagId MemTagId)
{
	if (!TimingView.IsValid())
	{
		return nullptr;
	}

	Insights::FMemoryTag* TagPtr = TagList.GetTagById(MemTagId);
	if (TagPtr)
	{
		for (TSharedPtr<FMemoryGraphTrack> MemoryGraph : TagPtr->GetGraphTracks())
		{
			if (MemoryGraph != MainGraphTrack && MemoryGraph->GetSeries().Num() == 1)
			{
				return MemoryGraph;
			}
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FMemoryGraphTrack> FMemorySharedState::CreateMemTagGraphTrack(Insights::FMemoryTagId MemTagId)
{
	if (!TimingView.IsValid() || !CurrentTracker)
	{
		return nullptr;
	}

	Insights::FMemoryTrackerId MemTrackerId = CurrentTracker->GetId();

	Insights::FMemoryTag* TagPtr = TagList.GetTagById(MemTagId);

	const FString SeriesName = TagPtr ? TagPtr->GetStatName() : FString::Printf(TEXT("Unknown LLM Tag (%d)"), MemTagId);

	const FLinearColor Color = TagPtr ? TagPtr->GetColor() : FLinearColor(0.5f, 0.5f, 0.5f, 1.0f);
	const FLinearColor BorderColor(FMath::Min(Color.R + 0.4f, 1.0f), FMath::Min(Color.G + 0.4f, 1.0f), FMath::Min(Color.B + 0.4f, 1.0f), 1.0f);

	// Also create a series in the MainGraphTrack.
	if (MainGraphTrack.IsValid())
	{
		TSharedPtr<FMemoryGraphSeries> Series = MainGraphTrack->AddMemTagSeries(MemTrackerId, MemTagId);
		Series->SetName(SeriesName);
		Series->SetColor(Color, BorderColor, Color.CopyWithNewOpacity(0.1f));
		Series->DisableAutoZoom();
		Series->SetScaleY(0.0000002);

		if (TagPtr)
		{
			TagPtr->AddTrack(MainGraphTrack);
		}

		MainGraphTrack->Show();
		TimingView->OnTrackVisibilityChanged();
	}

	TSharedPtr<FMemoryGraphTrack> GraphTrack = GetMemTagGraphTrack(MemTagId);

	if (!GraphTrack.IsValid())
	{
		// Create new Graph track.
		GraphTrack = MakeShared<FMemoryGraphTrack>(*this);

		const int32 Order = GetNextMemoryGraphTrackOrder();
		GraphTrack->SetOrder(Order);
		GraphTrack->SetName(SeriesName);
		//GraphTrack->SetVisibilityFlag(bShowHideAllMemoryTracks);
		GraphTrack->Show();

		GraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Small, 32.0f);
		GraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Medium, 100.0f);
		GraphTrack->SetAvailableTrackHeight(EMemoryTrackHeightMode::Large, 200.0f);
		GraphTrack->SetCurrentTrackHeight(TrackHeightMode);

		GraphTrack->EnableAutoZoom();

		// Create series.
		TSharedPtr<FMemoryGraphSeries> Series = GraphTrack->AddMemTagSeries(MemTrackerId, MemTagId);
		Series->SetName(SeriesName);
		Series->SetColor(Color, BorderColor);
		Series->SetBaselineY(GraphTrack->GetHeight() - 1.0f);
		Series->EnableAutoZoom();

		if (TagPtr)
		{
			TagPtr->AddTrack(GraphTrack);
		}

		// Add the new Graph in scrollable tracks.
		TimingView->AddScrollableTrack(GraphTrack);
		AllTracks.Add(GraphTrack);
	}
	else
	{
		GraphTrack->Show();
		TimingView->OnTrackVisibilityChanged();
	}

	return GraphTrack;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FMemorySharedState::RemoveMemTagGraphTrack(Insights::FMemoryTagId MemTagId)
{
	if (!TimingView.IsValid() || !CurrentTracker)
	{
		return -1;
	}

	int32 TrackCount = 0;

	Insights::FMemoryTag* TagPtr = TagList.GetTagById(MemTagId);
	if (TagPtr)
	{
		for (TSharedPtr<FMemoryGraphTrack> GraphTrack : TagPtr->GetGraphTracks())
		{
			GraphTrack->RemoveMemTagSeries(MemTagId);
			if (GraphTrack->GetSeries().Num() == 0)
			{
				if (GraphTrack == MainGraphTrack)
				{
					GraphTrack->Hide();
					TimingView->OnTrackVisibilityChanged();
				}
				else
				{
					++TrackCount;
					AllTracks.Remove(GraphTrack);
					TimingView->RemoveTrack(GraphTrack);
				}
			}
		}
		TagPtr->RemoveAllTracks();
	}

	return TrackCount;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

int32 FMemorySharedState::RemoveUnusedMemTagGraphTracks()
{
	if (!TimingView.IsValid() || !CurrentTracker)
	{
		return -1;
	}

	TArray<TSharedPtr<FMemoryGraphTrack>> TracksToRemove;

	for (TSharedPtr<FMemoryGraphTrack>& GraphTrack : AllTracks)
	{
		TArray<Insights::FMemoryTagId> IdsToRemove;
		for (TSharedPtr<FGraphSeries>& Series : GraphTrack->GetSeries())
		{
			//TODO: if (Series->Is<FMemoryGraphSeries>())
			TSharedPtr<FMemoryGraphSeries> MemorySeries = StaticCastSharedPtr<FMemoryGraphSeries>(Series);
			Insights::FMemoryTag* TagPtr = TagList.GetTagById(MemorySeries->GetTagId());
			if (TagPtr)
			{
				const uint64 TrackerFlag = 1ULL << MemorySeries->GetTrackerId();
				if ((TagPtr->GetTrackers() & TrackerFlag) != TrackerFlag)
				{
					IdsToRemove.Add(MemorySeries->GetTagId());
					TagPtr->RemoveTrack(GraphTrack);
				}
			}
		}
		for (Insights::FMemoryTagId MemTagId : IdsToRemove)
		{
			GraphTrack->RemoveMemTagSeries(MemTagId);
		}
		if (GraphTrack->GetSeries().Num() == 0)
		{
			if (GraphTrack == MainGraphTrack)
			{
				GraphTrack->Hide();
				TimingView->OnTrackVisibilityChanged();
			}
			else
			{
				TracksToRemove.Add(GraphTrack);
			}
		}
	}

	for (TSharedPtr<FMemoryGraphTrack>& GraphTrack : TracksToRemove)
	{
		AllTracks.Remove(GraphTrack);
		TimingView->RemoveTrack(GraphTrack);
	}

	return TracksToRemove.Num();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FMemoryGraphSeries> FMemorySharedState::ToggleMemTagGraphSeries(TSharedPtr<FMemoryGraphTrack> GraphTrack, Insights::FMemoryTagId MemTagId)
{
	if (!GraphTrack.IsValid() || !CurrentTracker)
	{
		return nullptr;
	}

	Insights::FMemoryTrackerId MemTrackerId = CurrentTracker->GetId();

	Insights::FMemoryTag* TagPtr = TagList.GetTagById(MemTagId);

	TSharedPtr<FMemoryGraphSeries> Series = GraphTrack->GetMemTagSeries(MemTagId);
	if (Series.IsValid())
	{
		// Remove existing series.
		GraphTrack->RemoveMemTagSeries(MemTagId);
		GraphTrack->SetDirtyFlag();
		TimingView->OnTrackVisibilityChanged();

		if (TagPtr)
		{
			TagPtr->RemoveTrack(GraphTrack);
		}

		return nullptr;
	}
	else
	{
		// Add new series.
		Series = GraphTrack->AddMemTagSeries(MemTrackerId, MemTagId);
		Series->DisableAutoZoom();

		if (TagPtr)
		{
			TagPtr->AddTrack(GraphTrack);
		}

		GraphTrack->SetDirtyFlag();
		GraphTrack->Show();
		TimingView->OnTrackVisibilityChanged();

		return Series;
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::CreateTracksFromReport(const FString& Filename)
{
	if (!CurrentTracker)
	{
		return;
	}

	Insights::FReportConfig ReportConfig;

	Insights::FReportXmlParser ReportXmlParser;

	auto MessageLog = FInsightsManager::Get()->GetMessageLog();
	MessageLog->ClearMessageLog();

	ReportXmlParser.LoadReportTypesXML(ReportConfig, Filename);
	if (ReportXmlParser.GetStatus() != Insights::FReportXmlParser::EStatus::Completed)
	{
		MessageLog->UpdateMessageLog(ReportXmlParser.GetErrorMessages());
	}

	CreateTracksFromReport(ReportConfig);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::CreateTracksFromReport(const Insights::FReportConfig& ReportConfig)
{
	if (!CurrentTracker)
	{
		return;
	}

	//for (const Insights::FReportTypeConfig& ReportTypeConfig : ReportConfig.ReportTypes)
	//{
	//	CreateTracksFromReport(ReportTypeConfig);
	//}

	if (ReportConfig.ReportTypes.Num() > 0)
	{
		CreateTracksFromReport(ReportConfig.ReportTypes[0]);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FMemorySharedState::CreateTracksFromReport(const Insights::FReportTypeConfig& ReportTypeConfig)
{
	if (!CurrentTracker)
	{
		return;
	}

	int32 Order = GetNextMemoryGraphTrackOrder();
	int32 NumAddedTracks = 0;

	for (const Insights::FReportTypeGraphConfig& ReportTypeGraphConfig : ReportTypeConfig.Graphs)
	{
		TSharedPtr<FMemoryGraphTrack> GraphTrack = CreateGraphTrack(ReportTypeGraphConfig);
		if (GraphTrack)
		{
			GraphTrack->SetOrder(Order++);
			++NumAddedTracks;
		}
	}

	if (NumAddedTracks > 0)
	{
		if (TimingView)
		{
			TimingView->InvalidateScrollableTracksOrder();
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<FMemoryGraphTrack> FMemorySharedState::CreateGraphTrack(const Insights::FReportTypeGraphConfig& ReportTypeGraphConfig)
{
	if (ReportTypeGraphConfig.GraphConfig == nullptr)
	{
		// Invalid graph config.
		return nullptr;
	}

	if (!TimingView.IsValid() || !CurrentTracker)
	{
		return nullptr;
	}

	Insights::FMemoryTrackerId MemTrackerId = CurrentTracker->GetId();

	const Insights::FGraphConfig& GraphConfig = *ReportTypeGraphConfig.GraphConfig;

	TArray<FString> IncludeStats;
	GraphConfig.StatString.ParseIntoArray(IncludeStats, TEXT(" ")); // TODO: names enclosed in quotes

	if (IncludeStats.Num() == 0)
	{
		// No stats specified!?
		return nullptr;
	}

	TArray<FString> IgnoreStats;
	GraphConfig.IgnoreStats.ParseIntoArray(IgnoreStats, TEXT(";"));

	TArray<Insights::FMemoryTag*> Tags;
	TagList.FilterTags(IncludeStats, IgnoreStats, Tags);

	TSharedPtr<FMemoryGraphTrack> GraphTrack = CreateMemoryGraphTrack();
	if (GraphTrack)
	{
		if (GraphConfig.Height > 0.0f)
		{
			constexpr float MinGraphTrackHeight = 32.0f;
			constexpr float MaxGraphTrackHeight = 600.0f;
			GraphTrack->SetHeight(FMath::Clamp(GraphConfig.Height, MinGraphTrackHeight, MaxGraphTrackHeight));
		}

		GraphTrack->SetName(ReportTypeGraphConfig.Title);

		const double MinValue = GraphConfig.MinY * 1024.0 * 1024.0;
		const double MaxValue = GraphConfig.MaxY * 1024.0 * 1024.0;
		GraphTrack->SetDefaultValueRange(MinValue, MaxValue);

		UE_LOG(MemoryProfiler, Log, TEXT("[LLM Tags] Created graph \"%s\" (H=%.1f%s, MainStat=%s, Stats=%s)"),
			*ReportTypeGraphConfig.Title,
			GraphTrack->GetHeight(),
			GraphConfig.bStacked ? TEXT(", stacked") : TEXT(""),
			*GraphConfig.MainStat,
			*GraphConfig.StatString);

		TSharedPtr<FMemoryGraphSeries> MainSeries;

		for (Insights::FMemoryTag* TagPtr : Tags)
		{
			Insights::FMemoryTag& Tag = *TagPtr;

			TSharedPtr<FMemoryGraphSeries> Series = GraphTrack->AddMemTagSeries(MemTrackerId, Tag.GetId());
			Series->SetName(Tag.GetStatName());
			const FLinearColor Color = Tag.GetColor();
			const FLinearColor BorderColor(FMath::Min(Color.R + 0.4f, 1.0f), FMath::Min(Color.G + 0.4f, 1.0f), FMath::Min(Color.B + 0.4f, 1.0f), 1.0f);
			Series->SetColor(Color, BorderColor);

			Tag.AddTrack(MainGraphTrack);

			if (GraphConfig.MainStat == Tag.GetStatName())
			{
				MainSeries = Series;
			}
		}

		if (GraphConfig.bStacked)
		{
			GraphTrack->SetStacked(true);
			GraphTrack->SetMainSeries(MainSeries);
		}
	}

	return GraphTrack;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
