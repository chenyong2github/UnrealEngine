// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/SlateTrace.h"

#if UE_SLATE_TRACE_ENABLED

#include "Application/SlateApplicationBase.h"
#include "HAL/PlatformTime.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformProcess.h"
#include "Trace/Trace.inl"
#include "Types/ReflectionMetadata.h"
#include "Widgets/SWidget.h"
#include "FastUpdate/WidgetProxy.h"

#if !(UE_BUILD_SHIPPING)

static int32 bCaptureRootInvalidationCallstacks = 0;
static FAutoConsoleVariableRef CVarCaptureRootInvalidationCallstacks(
	TEXT("SlateDebugger.bCaptureRootInvalidationCallstacks"),
	bCaptureRootInvalidationCallstacks,
	TEXT("Whenever a widget is the root cause of an invalidation, capture the callstack for slate insights."));

#endif // !(UE_BUILD_SHIPPING)

//-----------------------------------------------------------------------------------//

UE_TRACE_CHANNEL_DEFINE(SlateChannel)

UE_TRACE_EVENT_BEGIN(SlateTrace, ApplicationTickAndDrawWidgets)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, DeltaTime)
	UE_TRACE_EVENT_FIELD(uint32, WidgetCount)			// Total amount of widget.
	UE_TRACE_EVENT_FIELD(uint32, TickCount)				// Amount of widget that needed to be tick.
	UE_TRACE_EVENT_FIELD(uint32, TimerCount)			// Amount of widget that needed a timer update.
	UE_TRACE_EVENT_FIELD(uint32, RepaintCount)			// Amount of widget that requested a paint.
	UE_TRACE_EVENT_FIELD(uint32, VolatilePaintCount)	// Amount of widget that will always get painted.
	UE_TRACE_EVENT_FIELD(uint32, PaintCount)			// Total amount of widget that got painted
														//This can be higher than RepaintCount+VolatilePaintCount because some widget can get be painted as a side effect of another widget being painted.
	UE_TRACE_EVENT_FIELD(uint32, InvalidateCount)		// Amount of widget that got invalidated.
	UE_TRACE_EVENT_FIELD(uint32, RootInvalidatedCount)	// Amount of InvalidationRoot that got invalidated.
	UE_TRACE_EVENT_FIELD(uint8, SlateFlags)				// Various flags that was enabled for that frame.
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(SlateTrace, AddWidget)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WidgetId)				// Added widget unique ID.
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(SlateTrace, WidgetInfo)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WidgetId)				// Created/Updated widget unique ID.
	UE_TRACE_EVENT_FIELD(Trace::WideString, Path)		// FReflectionMetaData::GetWidgetPath
	UE_TRACE_EVENT_FIELD(Trace::WideString, DebugInfo)	// FReflectionMetaData::GetWidgetDebugInfo
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(SlateTrace, RemoveWidget)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WidgetId)				// Removed widget unique ID.
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(SlateTrace, WidgetUpdated)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WidgetId)				// Updated widget unique ID.
	UE_TRACE_EVENT_FIELD(uint8, UpdateFlags)			// The reason of the update. (EWidgetUpdateFlags)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(SlateTrace, WidgetInvalidated)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WidgetId)					// Invalidated widget unique ID.
	UE_TRACE_EVENT_FIELD(uint64, InvestigatorId)			// Widget unique ID that investigated the invalidation.
	UE_TRACE_EVENT_FIELD(uint8, InvalidateWidgetReason)		// The reason of the invalidation. (EInvalidateWidgetReason)
	UE_TRACE_EVENT_FIELD(Trace::WideString, ScriptTrace)	// Optional script trace for root widget invalidations
	UE_TRACE_EVENT_FIELD(uint64[], Callstack)				// Optional callstack for root widget invalidations
	UE_TRACE_EVENT_FIELD(uint32, ProcessId)					// Optional proccess ID where the invalidation occureed.
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(SlateTrace, RootInvalidated)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WidgetId)				// Invalidated InvalidationRoot widget unique ID.
	UE_TRACE_EVENT_FIELD(uint64, InvestigatorId)		// Widget unique ID that investigated the invalidation.
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(SlateTrace, RootChildOrderInvalidated)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, WidgetId)				// Invalidated InvalidationRoot widget unique ID.
	UE_TRACE_EVENT_FIELD(uint64, InvestigatorId)		// Widget unique ID that investigated the invalidation.
UE_TRACE_EVENT_END()

//-----------------------------------------------------------------------------------//

namespace SlateTraceDetail
{
	uint32 GWidgetCount = 0;
	uint32 GScopedPaintCount = 0;

	uint32 GFramePaintCount = 0;
	uint32 GFrameTickCount = 0;
	uint32 GFrameTimerCount = 0;
	uint32 GFrameRepaintCount = 0;
	uint32 GFrameVolatileCount = 0;
	uint32 GFrameInvalidateCount = 0;
	uint32 GFrameRootInvalidateCount = 0;

	uint64 GetWidgetId(const SWidget* InWidget)
	{
#if UE_SLATE_WITH_WIDGET_UNIQUE_IDENTIFIER
		check(InWidget);
		return InWidget->GetId();
#endif
		return 0;
	}

	uint64 GetWidgetIdIfValid(const SWidget* InWidget)
	{
#if UE_SLATE_WITH_WIDGET_UNIQUE_IDENTIFIER
		return InWidget ? InWidget->GetId() : 0;
#endif
		return 0;
	}
}

 //-----------------------------------------------------------------------------------//

FSlateTrace::FScopedWidgetPaintTrace::FScopedWidgetPaintTrace(const SWidget* InWidget)
	: StartCycle(FPlatformTime::Cycles64())
	, Widget(InWidget)
	, StartPaintCount(SlateTraceDetail::GScopedPaintCount)
{
	++SlateTraceDetail::GScopedPaintCount;
	++SlateTraceDetail::GFramePaintCount;
}

FSlateTrace::FScopedWidgetPaintTrace::~FScopedWidgetPaintTrace()
{
	FSlateTrace::OutputWidgetPaint(Widget, StartCycle, FPlatformTime::Cycles64(), SlateTraceDetail::GScopedPaintCount - StartPaintCount);
	--SlateTraceDetail::GScopedPaintCount;
}

//-----------------------------------------------------------------------------------//

void FSlateTrace::ApplicationTickAndDrawWidgets(float DeltaTime)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(SlateChannel))
	{
		static_assert(sizeof(ESlateTraceApplicationFlags) == sizeof(uint8), "FSlateTrace::ESlateFlags is not a uint8");

		ESlateTraceApplicationFlags LocalFlags = ESlateTraceApplicationFlags::None;
		if (GSlateEnableGlobalInvalidation) { LocalFlags |= ESlateTraceApplicationFlags::GlobalInvalidation; }
		if (GSlateFastWidgetPath) { LocalFlags |= ESlateTraceApplicationFlags::FastWidgetPath; }

		UE_TRACE_LOG(SlateTrace, ApplicationTickAndDrawWidgets, SlateChannel)
			<< ApplicationTickAndDrawWidgets.Cycle(FPlatformTime::Cycles64())
			<< ApplicationTickAndDrawWidgets.DeltaTime(DeltaTime)
			<< ApplicationTickAndDrawWidgets.WidgetCount(SlateTraceDetail::GWidgetCount)
			<< ApplicationTickAndDrawWidgets.TickCount(SlateTraceDetail::GFrameTickCount)
			<< ApplicationTickAndDrawWidgets.TimerCount(SlateTraceDetail::GFrameTimerCount)
			<< ApplicationTickAndDrawWidgets.RepaintCount(SlateTraceDetail::GFrameRepaintCount)
			<< ApplicationTickAndDrawWidgets.VolatilePaintCount(SlateTraceDetail::GFrameVolatileCount)
			<< ApplicationTickAndDrawWidgets.PaintCount(SlateTraceDetail::GFramePaintCount)
			<< ApplicationTickAndDrawWidgets.InvalidateCount(SlateTraceDetail::GFrameInvalidateCount)
			<< ApplicationTickAndDrawWidgets.RootInvalidatedCount(SlateTraceDetail::GFrameRootInvalidateCount)
			<< ApplicationTickAndDrawWidgets.SlateFlags(static_cast<uint8>(LocalFlags));

		SlateTraceDetail::GFrameTickCount = 0;
		SlateTraceDetail::GFrameTimerCount = 0;
		SlateTraceDetail::GFrameRepaintCount = 0;
		SlateTraceDetail::GFrameVolatileCount = 0;
		SlateTraceDetail::GFramePaintCount = 0;
		SlateTraceDetail::GFrameInvalidateCount = 0;
		SlateTraceDetail::GFrameRootInvalidateCount = 0;
	}
}

void FSlateTrace::WidgetUpdated(const SWidget* Widget, EWidgetUpdateFlags UpdateFlags)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(SlateChannel) && UpdateFlags != EWidgetUpdateFlags::None)
	{
		if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsTick)) { ++SlateTraceDetail::GFrameTickCount; }
		if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsActiveTimerUpdate)) { ++SlateTraceDetail::GFrameTimerCount; }

		if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsVolatilePaint)) { ++SlateTraceDetail::GFrameVolatileCount; }
		else if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsRepaint)) { ++SlateTraceDetail::GFrameRepaintCount; }

		static_assert(sizeof(EWidgetUpdateFlags) == sizeof(uint8), "EWidgetUpdateFlags is not a uint8");

		const uint64 WidgetId = SlateTraceDetail::GetWidgetId(Widget);

		UE_TRACE_LOG(SlateTrace, WidgetUpdated, SlateChannel)
			<< WidgetUpdated.Cycle(FPlatformTime::Cycles64())
			<< WidgetUpdated.WidgetId(WidgetId)
			<< WidgetUpdated.UpdateFlags(static_cast<uint8>(UpdateFlags));
	}
}

void FSlateTrace::WidgetInvalidated(const SWidget* Widget, const SWidget* Investigator, EInvalidateWidgetReason Reason)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(SlateChannel) && Reason != EInvalidateWidgetReason::None)
	{
		++SlateTraceDetail::GFrameInvalidateCount;

		static_assert(sizeof(EInvalidateWidgetReason) == sizeof(uint8), "EInvalidateWidgetReason is not a uint8");

		FString ScriptTrace;
		constexpr int MAX_DEPTH = 64;
		uint64 StackTrace[MAX_DEPTH] = { 0 };
		uint32 StackTraceDepth = 0;
		uint32 ProcessId = 0;

#if !(UE_BUILD_SHIPPING)
		//@TODO: Could add a CVar to only capture certain callstacks for performance (Widget name, type, etc).
		if (!Investigator && bCaptureRootInvalidationCallstacks)
		{
			FSlowHeartBeatScope SuspendHeartBeat;
			FDisableHitchDetectorScope SuspendGameThreadHitch;

			ScriptTrace = FFrame::GetScriptCallstack(true /* bReturnEmpty */);
			if (!ScriptTrace.IsEmpty())
			{
				ScriptTrace = "ScriptTrace: \n" + ScriptTrace;
			}

			// Walk the stack and dump it to the allocated memory.
			StackTraceDepth = FPlatformStackWalk::CaptureStackBackTrace(StackTrace, MAX_DEPTH);
			ProcessId = FPlatformProcess::GetCurrentProcessId();
		}
#endif // !(UE_BUILD_SHIPPING)

		const uint64 WidgetId = SlateTraceDetail::GetWidgetId(Widget);
		const uint64 InvestigatorId = SlateTraceDetail::GetWidgetIdIfValid(Investigator);

		UE_TRACE_LOG(SlateTrace, WidgetInvalidated, SlateChannel)
			<< WidgetInvalidated.Cycle(FPlatformTime::Cycles64())
			<< WidgetInvalidated.WidgetId(WidgetId)
			<< WidgetInvalidated.InvestigatorId(InvestigatorId)
			<< WidgetInvalidated.ScriptTrace(*ScriptTrace)
			<< WidgetInvalidated.Callstack(StackTrace, StackTraceDepth)
			<< WidgetInvalidated.ProcessId(ProcessId)
			<< WidgetInvalidated.InvalidateWidgetReason(static_cast<uint8>(Reason));
	}
}

void FSlateTrace::RootInvalidated(const SWidget* Widget, const SWidget* Investigator)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(SlateChannel))
	{
		++SlateTraceDetail::GFrameInvalidateCount;

		const uint64 WidgetId = SlateTraceDetail::GetWidgetId(Widget);
		const uint64 InvestigatorId = SlateTraceDetail::GetWidgetIdIfValid(Investigator);

		UE_TRACE_LOG(SlateTrace, RootInvalidated, SlateChannel)
			<< RootInvalidated.Cycle(FPlatformTime::Cycles64())
			<< RootInvalidated.WidgetId(WidgetId)
			<< RootInvalidated.InvestigatorId(InvestigatorId);
	}
}

void FSlateTrace::RootChildOrderInvalidated(const SWidget* Widget, const SWidget* Investigator)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(SlateChannel))
	{
		++SlateTraceDetail::GFrameInvalidateCount;

		const uint64 WidgetId = SlateTraceDetail::GetWidgetId(Widget);
		const uint64 InvestigatorId = SlateTraceDetail::GetWidgetIdIfValid(Investigator);

		UE_TRACE_LOG(SlateTrace, RootChildOrderInvalidated, SlateChannel)
			<< RootChildOrderInvalidated.Cycle(FPlatformTime::Cycles64())
			<< RootChildOrderInvalidated.WidgetId(WidgetId)
			<< RootChildOrderInvalidated.InvestigatorId(InvestigatorId);
	}
}

void FSlateTrace::AddWidget(const SWidget* Widget)
{
	++SlateTraceDetail::GWidgetCount;

	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(SlateChannel))
	{
		const uint64 WidgetId = SlateTraceDetail::GetWidgetId(Widget);

		UE_TRACE_LOG(SlateTrace, AddWidget, SlateChannel)
			<< AddWidget.Cycle(FPlatformTime::Cycles64())
			<< AddWidget.WidgetId(WidgetId);
	}
}

void FSlateTrace::UpdateWidgetInfo(const SWidget* Widget)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(SlateChannel))
	{
		const uint64 WidgetId = SlateTraceDetail::GetWidgetId(Widget);

		UE_TRACE_LOG(SlateTrace, WidgetInfo, SlateChannel)
			<< WidgetInfo.WidgetId(WidgetId)
			<< WidgetInfo.Path(*FReflectionMetaData::GetWidgetPath(Widget))
			<< WidgetInfo.DebugInfo(*FReflectionMetaData::GetWidgetDebugInfo(Widget));
	}
}

void FSlateTrace::RemoveWidget(const SWidget* Widget)
{
	if (UE_TRACE_CHANNELEXPR_IS_ENABLED(SlateChannel))
	{
		const uint64 WidgetId = SlateTraceDetail::GetWidgetId(Widget);

		UE_TRACE_LOG(SlateTrace, RemoveWidget, SlateChannel)
			<< RemoveWidget.Cycle(FPlatformTime::Cycles64())
			<< RemoveWidget.WidgetId(WidgetId);
		ensure(SlateTraceDetail::GWidgetCount > 0);
	}
	--SlateTraceDetail::GWidgetCount;
}

void FSlateTrace::OutputWidgetPaint(const SWidget* Widget, uint64 StartCycle, uint64 EndCycle, uint32 PaintCount)
{
}

 #endif // UE_SLATE_TRACE_ENABLED
