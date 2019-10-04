// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "LoadingTimingTrack.h"

#include "Fonts/FontMeasure.h"
#include "Styling/SlateBrush.h"
#include "TraceServices/AnalysisService.h"
#include "TraceServices/SessionService.h"

// Insights
#include "Insights/InsightsManager.h"
#include "Insights/Common/PaintUtils.h"
#include "Insights/Common/TimeUtils.h"
#include "Insights/ViewModels/TimingEvent.h"
#include "Insights/ViewModels/TimingTrackViewport.h"
#include "Insights/ViewModels/TimingViewDrawHelper.h"
#include "Insights/ViewModels/TooltipDrawState.h"

#define LOCTEXT_NAMESPACE "LoadingTimingTrack"

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLoadingSharedState
////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingSharedState::Reset()
{
	LoadingGetEventNameFn = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventName);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* GetName(ELoadTimeProfilerPackageEventType Type)
{
	switch (Type)
	{
	case LoadTimeProfilerPackageEventType_CreateLinker:				return TEXT("CreateLinker");
	case LoadTimeProfilerPackageEventType_FinishLinker:				return TEXT("FinishLinker");
	case LoadTimeProfilerPackageEventType_StartImportPackages:		return TEXT("StartImportPackages");
	case LoadTimeProfilerPackageEventType_SetupImports:				return TEXT("SetupImports");
	case LoadTimeProfilerPackageEventType_SetupExports:				return TEXT("SetupExports");
	case LoadTimeProfilerPackageEventType_ProcessImportsAndExports:	return TEXT("ProcessImportsAndExports");
	case LoadTimeProfilerPackageEventType_ExportsDone:				return TEXT("ExportsDone");
	case LoadTimeProfilerPackageEventType_PostLoadWait:				return TEXT("PostLoadWait");
	case LoadTimeProfilerPackageEventType_StartPostLoad:			return TEXT("StartPostLoad");
	case LoadTimeProfilerPackageEventType_Tick:						return TEXT("Tick");
	case LoadTimeProfilerPackageEventType_Finish:					return TEXT("Finish");
	case LoadTimeProfilerPackageEventType_DeferredPostLoad:			return TEXT("DeferredPostLoad");
	case LoadTimeProfilerPackageEventType_None:						return TEXT("None");
	default:														return TEXT("");
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* GetName(ELoadTimeProfilerObjectEventType Type)
{
	switch (Type)
	{
	case LoadTimeProfilerObjectEventType_Create:	return TEXT("Create");
	case LoadTimeProfilerObjectEventType_Serialize:	return TEXT("Serialize");
	case LoadTimeProfilerObjectEventType_PostLoad:	return TEXT("PostLoad");
	case LoadTimeProfilerObjectEventType_None:		return TEXT("None");
	default:										return TEXT("");
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameByPackageEventType(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return GetName(Event.PackageEventType);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameByExportEventType(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return GetName(Event.ExportEventType);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameByPackageName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return Event.Package ? Event.Package->Name : TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameByExportClassName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return Event.Export && Event.Export->Class ? Event.Export->Class->Name : TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventName(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	if (Depth == 0)
	{
		if (Event.Package)
		{
			return Event.Package->Name;
		}
	}

	if (Event.Export && Event.Export->Class)
	{
		return Event.Export->Class->Name;
	}

	return TEXT("");
}

////////////////////////////////////////////////////////////////////////////////////////////////////

const TCHAR* FLoadingSharedState::GetEventNameEx(uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event) const
{
	return LoadingGetEventNameFn.Execute(Depth, Event);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingSharedState::SetColorSchema(int32 Schema)
{
	switch (Schema)
	{
		case 0: LoadingGetEventNameFn = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventNameByPackageEventType); break;
		case 1: LoadingGetEventNameFn = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventNameByExportEventType); break;
		case 2: LoadingGetEventNameFn = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventNameByPackageName); break;
		case 3: LoadingGetEventNameFn = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventNameByExportClassName); break;
		case 4: LoadingGetEventNameFn = FLoadingTrackGetEventNameDelegate::CreateRaw(this, &FLoadingSharedState::GetEventName); break;
	};
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLoadingTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingTimingTrack::InitTooltip(FTooltipDrawState& Tooltip, const FTimingEvent& HoveredTimingEvent) const
{
	Tooltip.ResetContent();

	Tooltip.AddTitle(State->GetEventNameEx(HoveredTimingEvent.Depth, HoveredTimingEvent.LoadingInfo));

	const Trace::FPackageInfo* Package = HoveredTimingEvent.LoadingInfo.Package;
	const Trace::FPackageExportInfo* Export = HoveredTimingEvent.LoadingInfo.Export;

	Tooltip.AddNameValueTextLine(TEXT("Duration:"), TimeUtils::FormatTimeAuto(HoveredTimingEvent.Duration()));
	Tooltip.AddNameValueTextLine(TEXT("Depth:"), FString::Printf(TEXT("%d"), HoveredTimingEvent.Depth));
	Tooltip.AddNameValueTextLine(TEXT("Package Event:"), ::GetName(HoveredTimingEvent.LoadingInfo.PackageEventType));

	if (Package)
	{
		Tooltip.AddNameValueTextLine(TEXT("Package Name:"), Package->Name);
		Tooltip.AddNameValueTextLine(TEXT("Header Size:"), FString::Printf(TEXT("%s bytes"), *FText::AsNumber(Package->Summary.TotalHeaderSize).ToString()));
		Tooltip.AddNameValueTextLine(TEXT("Package Summary:"), FString::Printf(TEXT("%d names, %d imports, %d exports"), Package->Summary.NameCount, Package->Summary.ImportCount, Package->Summary.ExportCount));
	}

	Tooltip.AddNameValueTextLine(TEXT("Export Event:"), FString::Printf(TEXT("%s%s"), ::GetName(HoveredTimingEvent.LoadingInfo.ExportEventType), Export && Export->IsAsset ? TEXT(" [asset]") : TEXT("")));

	if (Export)
	{
		Tooltip.AddNameValueTextLine(TEXT("Export Class:"), Export->Class ? Export->Class->Name : TEXT("N/A"));
		Tooltip.AddNameValueTextLine(TEXT("Serial Offset:"), FString::Printf(TEXT("%s bytes"), *FText::AsNumber(Export->SerialOffset).ToString()));
		Tooltip.AddNameValueTextLine(TEXT("Serial Size:"), FString::Printf(TEXT("%s bytes"), *FText::AsNumber(Export->SerialSize).ToString()));
	}

	Tooltip.UpdateLayout();
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FSearchLoadTimeProfilerCpuEvent
////////////////////////////////////////////////////////////////////////////////////////////////////

struct FSearchLoadTimeProfilerCpuEvent
{
	const double StartTime;
	const double EndTime;
	TFunctionRef<bool(double, double, uint32)> Predicate;
	FTimingEvent& TimingEvent;
	const bool bStopAtFirstMatch;
	const bool bSearchForLargestEvent;
	mutable bool bFound;
	mutable bool bContinueSearching;
	mutable double LargestDuration;

	FSearchLoadTimeProfilerCpuEvent(const double InStartTime, const double InEndTime, TFunctionRef<bool(double, double, uint32)> InPredicate, FTimingEvent& InOutTimingEvent, bool bInStopAtFirstMatch, bool bInSearchForLargestEvent)
		: StartTime(InStartTime)
		, EndTime(InEndTime)
		, Predicate(InPredicate)
		, TimingEvent(InOutTimingEvent)
		, bStopAtFirstMatch(bInStopAtFirstMatch)
		, bSearchForLargestEvent(bInSearchForLargestEvent)
		, bFound(false)
		, bContinueSearching(true)
		, LargestDuration(-1.0)
	{
	}

	void CheckEvent(double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FLoadTimeProfilerCpuEvent& Event)
	{
		if (bContinueSearching && Predicate(EventStartTime, EventEndTime, EventDepth))
		{
			if (!bSearchForLargestEvent || EventEndTime - EventStartTime > LargestDuration)
			{
				LargestDuration = EventEndTime - EventStartTime;

				TimingEvent.TypeId = 0;
				TimingEvent.Depth = EventDepth;
				TimingEvent.StartTime = EventStartTime;
				TimingEvent.EndTime = EventEndTime;

				TimingEvent.LoadingInfo = Event;

				bFound = true;
				bContinueSearching = !bStopAtFirstMatch || bSearchForLargestEvent;
			}
		}
	}
};

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLoadingMainThreadTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingMainThreadTimingTrack::Draw(FTimingViewDrawHelper& Helper) const
{
	FTimingEventsTrack& Track = *const_cast<FLoadingMainThreadTimingTrack*>(this);

	if (Helper.BeginTimeline(Track))
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && Trace::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *Trace::ReadLoadTimeProfilerProvider(*Session.Get());

			LoadTimeProfilerProvider.ReadMainThreadCpuTimeline([this, &Helper](const Trace::ILoadTimeProfilerProvider::CpuTimeline& Timeline)
			{
				if (FTimingEventsTrack::bUseDownSampling)
				{
					const double SecondsPerPixel = 1.0 / Helper.GetViewport().GetScaleX();
					Timeline.EnumerateEventsDownSampled(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), SecondsPerPixel, [this, &Helper](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
					{
						const TCHAR* Name = State->GetEventNameEx(Depth, Event);
						Helper.AddEvent(StartTime, EndTime, Depth, Name);
					});
				}
				else
				{
					Timeline.EnumerateEvents(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), [this, &Helper](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
					{
						const TCHAR* Name = State->GetEventNameEx(Depth, Event);
						Helper.AddEvent(StartTime, EndTime, Depth, Name);
					});
				}
			});
		}

		Helper.EndTimeline(Track);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FLoadingMainThreadTimingTrack::SearchTimingEvent(const double InStartTime,
	const double InEndTime,
	TFunctionRef<bool(double, double, uint32)> InPredicate,
	FTimingEvent& InOutTimingEvent,
	bool bInStopAtFirstMatch,
	bool bInSearchForLargestEvent) const
{
	FSearchLoadTimeProfilerCpuEvent Ctx(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent);

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const FTimingEventsTrack* Track = Ctx.TimingEvent.Track;

		if (Trace::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			const Trace::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *Trace::ReadLoadTimeProfilerProvider(*Session.Get());

			LoadTimeProfilerProvider.ReadMainThreadCpuTimeline([&Ctx](const Trace::ILoadTimeProfilerProvider::CpuTimeline& Timeline)
			{
				Timeline.EnumerateEvents(Ctx.StartTime, Ctx.EndTime, [&Ctx](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FLoadTimeProfilerCpuEvent& Event)
				{
					Ctx.CheckEvent(EventStartTime, EventEndTime, EventDepth, Event);
				});
			});
		}
	}

	return Ctx.bFound;
}

////////////////////////////////////////////////////////////////////////////////////////////////////
// FLoadingAsyncThreadTimingTrack
////////////////////////////////////////////////////////////////////////////////////////////////////

void FLoadingAsyncThreadTimingTrack::Draw(FTimingViewDrawHelper& Helper) const
{
	FTimingEventsTrack& Track = *const_cast<FLoadingAsyncThreadTimingTrack*>(this);

	if (Helper.BeginTimeline(Track))
	{
		TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
		if (Session.IsValid() && Trace::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

			const Trace::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *Trace::ReadLoadTimeProfilerProvider(*Session.Get());

			LoadTimeProfilerProvider.ReadAsyncLoadingThreadCpuTimeline([this, &Helper](const Trace::ILoadTimeProfilerProvider::CpuTimeline& Timeline)
			{
				if (FTimingEventsTrack::bUseDownSampling)
				{
					const double SecondsPerPixel = 1.0 / Helper.GetViewport().GetScaleX();
					Timeline.EnumerateEventsDownSampled(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), SecondsPerPixel, [this, &Helper](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
					{
						const TCHAR* Name = State->GetEventNameEx(Depth, Event);
						Helper.AddEvent(StartTime, EndTime, Depth, Name);
					});
				}
				else
				{
					Timeline.EnumerateEvents(Helper.GetViewport().GetStartTime(), Helper.GetViewport().GetEndTime(), [this, &Helper](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
					{
						const TCHAR* Name = State->GetEventNameEx(Depth, Event);
						Helper.AddEvent(StartTime, EndTime, Depth, Name);
					});
				}
			});
		}

		Helper.EndTimeline(Track);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

bool FLoadingAsyncThreadTimingTrack::SearchTimingEvent(const double InStartTime,
													   const double InEndTime,
													   TFunctionRef<bool(double, double, uint32)> InPredicate,
													   FTimingEvent& InOutTimingEvent,
													   bool bInStopAtFirstMatch,
													   bool bInSearchForLargestEvent) const
{
	FSearchLoadTimeProfilerCpuEvent Ctx(InStartTime, InEndTime, InPredicate, InOutTimingEvent, bInStopAtFirstMatch, bInSearchForLargestEvent);

	TSharedPtr<const Trace::IAnalysisSession> Session = FInsightsManager::Get()->GetSession();
	if (Session.IsValid())
	{
		Trace::FAnalysisSessionReadScope SessionReadScope(*Session.Get());

		const FTimingEventsTrack* Track = Ctx.TimingEvent.Track;

		if (Trace::ReadLoadTimeProfilerProvider(*Session.Get()))
		{
			const Trace::ILoadTimeProfilerProvider& LoadTimeProfilerProvider = *Trace::ReadLoadTimeProfilerProvider(*Session.Get());

			LoadTimeProfilerProvider.ReadAsyncLoadingThreadCpuTimeline([&Ctx](const Trace::ILoadTimeProfilerProvider::CpuTimeline& Timeline)
			{
				Timeline.EnumerateEvents(Ctx.StartTime, Ctx.EndTime, [&Ctx](double EventStartTime, double EventEndTime, uint32 EventDepth, const Trace::FLoadTimeProfilerCpuEvent& Event)
				{
					Ctx.CheckEvent(EventStartTime, EventEndTime, EventDepth, Event);
				});
			});
		}
	}

	return Ctx.bFound;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
