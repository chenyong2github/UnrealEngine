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

void FLoadingTimingTrack::DrawTooltip(FTimingViewTooltip& Tooltip, const FVector2D& MousePosition, const FTimingEvent& HoveredTimingEvent, const FTimingTrackViewport& Viewport, const FDrawContext& DrawContext, const FSlateBrush* WhiteBrush, const FSlateFontInfo& Font) const
{
	FString EventName(State->GetEventNameEx(HoveredTimingEvent.Depth, HoveredTimingEvent.LoadingInfo));

	const Trace::FPackageInfo* Package = HoveredTimingEvent.LoadingInfo.Package;
	const Trace::FPackageExportInfo* Export = HoveredTimingEvent.LoadingInfo.Export;

	FString PackageName = Package ? Package->Name : TEXT("N/A");

	constexpr float ValueOffsetX = 100.0f;
	constexpr float MinValueTextWidth = 220.0f;

	// Compute desired tooltip width.
	const TSharedRef<FSlateFontMeasure> FontMeasureService = FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
	const float NameWidth = FontMeasureService->Measure(EventName, Font).X;
	const float PackageNameWidth = FontMeasureService->Measure(PackageName, Font).X;
	const float MinWidth1 = NameWidth + 2 * FTimingViewTooltip::BorderX;
	const float MinWidth2 = ValueOffsetX + FMath::Max(PackageNameWidth, MinValueTextWidth) + 2 * FTimingViewTooltip::BorderX;
	const float DesiredTooltipWidth = FMath::Max3(MinWidth1, MinWidth2, FTimingViewTooltip::MinWidth);

	constexpr float LineH = 14.0f;

	// Compute desired tooltip height.
	int32 LineCount = 5;
	if (Package)
	{
		LineCount += 3;
	}
	if (Export)
	{
		LineCount += 2;
	}
	const float MinHeight = LineCount * LineH + 2 * FTimingViewTooltip::BorderY;
	const float DesiredTooltipHeight = FMath::Max(MinHeight, FTimingViewTooltip::MinHeight);

	Tooltip.Update(MousePosition, DesiredTooltipWidth, DesiredTooltipHeight, Viewport.Width, Viewport.Height);

	const FLinearColor BackgroundColor(0.05f, 0.05f, 0.05f, Tooltip.Opacity);
	const FLinearColor NameColor(0.9f, 0.9f, 0.5f, Tooltip.Opacity);
	const FLinearColor TextColor(0.6f, 0.6f, 0.6f, Tooltip.Opacity);
	const FLinearColor ValueColor(1.0f, 1.0f, 1.0f, Tooltip.Opacity);

	DrawContext.DrawBox(Tooltip.PosX, Tooltip.PosY, Tooltip.Width, Tooltip.Height, WhiteBrush, BackgroundColor);
	DrawContext.LayerId++;

	const float X = Tooltip.PosX + FTimingViewTooltip::BorderX;
	const float ValueX = X + ValueOffsetX;
	float Y = Tooltip.PosY + FTimingViewTooltip::BorderY;

	DrawContext.DrawText(X, Y, EventName, Font, NameColor);
	Y += LineH;

	DrawContext.DrawText(X, Y, TEXT("Duration:"), Font, TextColor);
	FString InclStr = TimeUtils::FormatTimeAuto(HoveredTimingEvent.Duration());
	DrawContext.DrawText(ValueX, Y, InclStr, Font, ValueColor);
	Y += LineH;

	DrawContext.DrawText(X, Y, TEXT("Depth:"), Font, TextColor);
	FString DepthStr = FString::Printf(TEXT("%d"), HoveredTimingEvent.Depth);
	DrawContext.DrawText(ValueX, Y, DepthStr, Font, ValueColor);
	Y += LineH;

	DrawContext.DrawText(X, Y, TEXT("Package Event:"), Font, TextColor);
	DrawContext.DrawText(ValueX, Y, ::GetName(HoveredTimingEvent.LoadingInfo.PackageEventType), Font, ValueColor);
	Y += LineH;

	if (Package)
	{
		DrawContext.DrawText(X, Y, TEXT("Package Name:"), Font, TextColor);
		DrawContext.DrawText(ValueX, Y, PackageName, Font, ValueColor);
		Y += LineH;

		DrawContext.DrawText(X, Y, TEXT("Header Size:"), Font, TextColor);
		FString HeaderSizeStr = FString::Printf(TEXT("%d bytes"), Package->Summary.TotalHeaderSize);
		DrawContext.DrawText(ValueX, Y, HeaderSizeStr, Font, ValueColor);
		Y += LineH;

		DrawContext.DrawText(X, Y, TEXT("Package Summary:"), Font, TextColor);
		FString SummaryStr = FString::Printf(TEXT("%d names, %d imports, %d exports"), Package->Summary.NameCount, Package->Summary.ImportCount, Package->Summary.ExportCount);
		DrawContext.DrawText(ValueX, Y, SummaryStr, Font, ValueColor);
		Y += LineH;
	}

	{
		DrawContext.DrawText(X, Y, TEXT("Export Event:"), Font, TextColor);
		FString ExportTypeStr = FString::Printf(TEXT("%s%s"), ::GetName(HoveredTimingEvent.LoadingInfo.ExportEventType), Export && Export->IsAsset ? TEXT(" [asset]") : TEXT(""));
		DrawContext.DrawText(ValueX, Y, ExportTypeStr, Font, ValueColor);
		Y += LineH;
	}

	if (Export)
	{
		DrawContext.DrawText(X, Y, TEXT("Export Class:"), Font, TextColor);
		FString ClassStr = FString::Printf(TEXT("%s"), Export->Class ? Export->Class->Name : TEXT("N/A"));
		DrawContext.DrawText(ValueX, Y, ClassStr, Font, ValueColor);
		Y += LineH;

		DrawContext.DrawText(X, Y, TEXT("Serial:"), Font, TextColor);
		FString SerialStr = FString::Printf(TEXT("Offset: %llu, Size: %llu%s"), Export->SerialOffset, Export->SerialSize);
		DrawContext.DrawText(ValueX, Y, SerialStr, Font, ValueColor);
		Y += LineH;
	}

	DrawContext.LayerId++;
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
					const double SecondsPerPixel = 1.0 / Helper.GetViewport().ScaleX;
					Timeline.EnumerateEventsDownSampled(Helper.GetViewport().StartTime, Helper.GetViewport().EndTime, SecondsPerPixel, [this, &Helper](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
					{
						const TCHAR* Name = State->GetEventNameEx(Depth, Event);
						Helper.AddEvent(StartTime, EndTime, Depth, Name);
					});
				}
				else
				{
					Timeline.EnumerateEvents(Helper.GetViewport().StartTime, Helper.GetViewport().EndTime, [this, &Helper](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
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
					const double SecondsPerPixel = 1.0 / Helper.GetViewport().ScaleX;
					Timeline.EnumerateEventsDownSampled(Helper.GetViewport().StartTime, Helper.GetViewport().EndTime, SecondsPerPixel, [this, &Helper](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
					{
						const TCHAR* Name = State->GetEventNameEx(Depth, Event);
						Helper.AddEvent(StartTime, EndTime, Depth, Name);
					});
				}
				else
				{
					Timeline.EnumerateEvents(Helper.GetViewport().StartTime, Helper.GetViewport().EndTime, [this, &Helper](double StartTime, double EndTime, uint32 Depth, const Trace::FLoadTimeProfilerCpuEvent& Event)
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
