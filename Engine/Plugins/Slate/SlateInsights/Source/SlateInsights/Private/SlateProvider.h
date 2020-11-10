// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TraceServices/Model/AnalysisSession.h"

#include "FastUpdate/WidgetUpdateFlags.h"

#include "Common/PagedArray.h"
#include "Containers/ArrayView.h"
#include "Model/IntervalTimeline.h"
#include "Model/PointTimeline.h"
#include "Model/IntervalTimeline.h"
#include "Templates/EnableIf.h"
#include "Trace/Analyzer.h"
#include "Trace/SlateTrace.h"

namespace Trace { class IAnalysisSession; }

namespace UE
{
namespace SlateInsights
{

namespace Message
{

struct FWidgetId
{
private:
	uint64 Value;

public:
	constexpr FWidgetId() : Value(0) {}
	template<typename T, typename U = typename TEnableIf<TIsSame<T, uint64>::Value>::Type>
	constexpr FWidgetId(T InValue) : Value(InValue) {}
	explicit operator bool() const { return Value != 0; }
	friend uint32 GetTypeHash(const FWidgetId& Key) { return ::GetTypeHash(Key.Value); }
	friend bool operator==(const FWidgetId A, const FWidgetId B) { return A.Value == B.Value; }
	friend bool operator!=(const FWidgetId A, const FWidgetId B) { return A.Value != B.Value; }
};

struct FWidgetInfo
{
	FWidgetId WidgetId;
	FString Path;
	FString DebugInfo;
	uint64 EventIndex;

	FWidgetInfo() = default;
	FWidgetInfo(const Trace::IAnalyzer::FEventData& EventData);
	friend bool operator==(const FWidgetInfo& A, FWidgetId B) { return A.WidgetId == B; }
};

struct FWidgetUpdatedMessage
{
	FWidgetId WidgetId;
	/** Flag that was set by an invalidation or on the widget directly. */
	EWidgetUpdateFlags UpdateFlags;

	FWidgetUpdatedMessage(const Trace::IAnalyzer::FEventData& EventData);
};

struct FWidgetInvalidatedMessage
{
	FWidgetId WidgetId;
	FWidgetId InvestigatorId;
	EInvalidateWidgetReason InvalidationReason = EInvalidateWidgetReason::None;
	bool bRootInvalidated = false;
	bool bRootChildOrderInvalidated = false;
	FString ScriptTrace;
	FString Callstack;

	static FWidgetInvalidatedMessage FromWidget(const Trace::IAnalyzer::FEventData& EventData);
	static FWidgetInvalidatedMessage FromRoot(const Trace::IAnalyzer::FEventData& EventData);
	static FWidgetInvalidatedMessage FromChildOrder(const Trace::IAnalyzer::FEventData& EventData);
	static FString GetCallstack(const Trace::IAnalyzer::FEventData& EventData);
};

struct FApplicationTickedMessage
{
	float DeltaTime;
	uint32 WidgetCount;
	uint32 TickCount;
	uint32 TimerCount;
	uint32 RepaintCount;
	uint32 VolatilePaintCount;
	uint32 PaintCount;
	uint32 InvalidateCount;
	uint32 RootInvalidatedCount;
	ESlateTraceApplicationFlags Flags;

	FApplicationTickedMessage(const Trace::IAnalyzer::FEventData& EventData);
};

} //namespace Message

class FSlateProvider : public Trace::IProvider
{
public:
	static FName ProviderName;

	FSlateProvider(Trace::IAnalysisSession& InSession);

	/** */
	void AddWidget(double Seconds, uint64 WidgetId);
	void SetWidgetInfo(double Seconds, Message::FWidgetInfo Info);
	void RemoveWidget(double Seconds, uint64 WidgetId);

	/** */
	void AddApplicationTickedEvent(double Seconds, Message::FApplicationTickedMessage Message);
	void AddWidgetUpdatedEvent(double Seconds, Message::FWidgetUpdatedMessage UpdatedMessage);
	void AddWidgetInvalidatedEvent(double Seconds, Message::FWidgetInvalidatedMessage InvalidatedMessage);

	/** */
	using TApplicationTickedTimeline = Trace::TPointTimeline<Message::FApplicationTickedMessage>;
	const TApplicationTickedTimeline& GetApplicationTickedTimeline() const
	{
		Session.ReadAccessCheck();
		return ApplicationTickedTimeline;
	}

	/** */
	using TWidgetUpdatedTimeline = Trace::TPointTimeline<Message::FWidgetUpdatedMessage>;
	const TWidgetUpdatedTimeline& GetWidgetUpdatedTimeline() const
	{
		Session.ReadAccessCheck();
		return WidgetUpdatedTimeline;
	}
	
	/** */
	using TWidgetInvalidatedTimeline = Trace::TPointTimeline<Message::FWidgetInvalidatedMessage>;
	const TWidgetInvalidatedTimeline& GetWidgetInvalidatedTimeline() const
	{
		Session.ReadAccessCheck();
		return WidgetInvalidatedTimeline;
	}

	/** */
	using TWidgetTimeline = Trace::TIntervalTimeline<Message::FWidgetId>;
	const TWidgetTimeline& GetWidgetTimeline() const
	{
		Session.ReadAccessCheck();
		return WidgetTimelines;
	}

	/** */
	template<typename T>
	struct FScopedEnumerateOutsideRange
	{
		FScopedEnumerateOutsideRange(const T& InTimeline)
			: Timeline(InTimeline)
		{
			const_cast<T&>(Timeline).SetEnumerateOutsideRange(true);
		}
		~FScopedEnumerateOutsideRange()
		{
			const_cast<T&>(Timeline).SetEnumerateOutsideRange(false);
		}
		FScopedEnumerateOutsideRange(const FScopedEnumerateOutsideRange&) = delete;
		FScopedEnumerateOutsideRange& operator= (const FScopedEnumerateOutsideRange&) = delete;
	private:
		const T& Timeline;
	};

	/** */
	const Message::FWidgetInfo* FindWidget(Message::FWidgetId WidgetId) const
	{
		return WidgetInfos.Find(WidgetId);
	}

private:
	Trace::IAnalysisSession& Session;

	TMap<Message::FWidgetId, Message::FWidgetInfo> WidgetInfos;

	TWidgetTimeline WidgetTimelines;
	TApplicationTickedTimeline ApplicationTickedTimeline;
	TWidgetUpdatedTimeline WidgetUpdatedTimeline;
	TWidgetInvalidatedTimeline WidgetInvalidatedTimeline;
};

} //namespace SlateInsights
} //namespace UE
