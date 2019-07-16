// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/PaintArgs.h"
#include "Styling/WidgetStyle.h"
#include "Misc/MemStack.h"
#include "Layout/Clipping.h"
#include "Layout/FlowDirection.h"
#include "Rendering/DrawElements.h"

class SWidget;
class FPaintArgs;
struct FFastPathPerFrameData;
struct FSlateWidgetPersistentState;
struct FWidgetUpdateList;
class FSlateInvalidationRoot;


/** Widget update flags for fast path.  Work in progress, do not modify */
enum class EWidgetUpdateFlags : uint8
{
	None = 0,

	/** Widget has a tick function */
	NeedsTick = 1 << 2,

	/** Widget has an active timer that needs to update */
	NeedsActiveTimerUpdate = 1 << 3,

	/** Needs repaint because the widget is dirty */
	NeedsRepaint = 1 << 4,

	/** Needs repaint because the widget is volatile */
	NeedsVolatilePaint = 1 << 6,
	
	AnyUpdate = 0xff,
};

ENUM_CLASS_FLAGS(EWidgetUpdateFlags)

enum class EInvalidateWidgetReason : uint8;

class FWidgetProxy
{
public:
	FWidgetProxy(SWidget* InWidget);

	int32 Update(
		const FPaintArgs& PaintArgs,
		int32 MyIndex,
		FSlateWindowElementList& OutDrawElements);

	bool ProcessInvalidation(FWidgetUpdateList& UpdateList, TArray<FWidgetProxy>& FastPathWidgetList, FSlateInvalidationRoot& Root);

	static void MarkProxyUpdatedThisFrame(FWidgetProxy& Proxy, FWidgetUpdateList& UpdateList);

private:
	int32 Repaint(const FPaintArgs& PaintArgs, int32 MyIndex, FSlateWindowElementList& OutDrawElements) const;
public:
	SWidget* Widget;
	int32 Index;
	int32 ParentIndex;
	int32 NumChildren;
	int32 LeafMostChildIndex;
	EWidgetUpdateFlags UpdateFlags;
	EInvalidateWidgetReason CurrentInvalidateReason;
	/** The widgets own visibility */
	EVisibility Visibility;
	uint8 bUpdatedSinceLastInvalidate : 1;
	uint8 bInUpdateList : 1;
	uint8 bInvisibleDueToParentOrSelfVisibility : 1;
	uint8 bChildOrderInvalid : 1;
};

static_assert(sizeof(FWidgetProxy) <= 32, "FWidgetProxy should be 32 bytes");

static_assert(TIsTriviallyDestructible<FWidgetProxy>::Value == true, "FWidgetProxy must be trivially destructible");

template <> struct TIsPODType<FWidgetProxy> { enum { Value = true }; };

struct FWidgetUpdateList
{
public:
	inline void Push(FWidgetProxy& Proxy)
	{
		if (!Heap.Contains(Proxy.Index))
		{
			Proxy.bInUpdateList = true;
			Heap.HeapPush(Proxy.Index, TGreater<>());
		}
	}

	inline int32 Pop()
	{
		int32 Index;
		Heap.HeapPop(Index, TGreater<>(), false);
		return Index;
	}

	inline void Empty()
	{
		Heap.Empty();
	}

	inline void Reset()
	{
		Heap.Reset();
	}

	inline int32 Num() const
	{
		return Heap.Num();
	}

	bool Contains(FWidgetProxy& Proxy) const
	{
		return Heap.Contains(Proxy.Index);
	}

	const TArray<int32, TInlineAllocator<100>>& GetRawData() const
	{
		return Heap;
	}
private:
	TArray<int32, TInlineAllocator<100>> Heap;
};


/**
 * Represents the state of a widget from when it last had SWidget::Paint called on it. 
 * This should contain everything needed to directly call Paint on a widget
 */
struct FSlateWidgetPersistentState
{
	FSlateWidgetPersistentState()
		: CachedElementListNode(nullptr)
		, LayerId(0)
		, OutgoingLayerId(0)
		, IncomingFlowDirection(EFlowDirection::LeftToRight)
		, bParentEnabled(true)
		, bInheritedHittestability(false)
	{}

	TWeakPtr<SWidget> PaintParent;
	TOptional<FSlateClippingState> InitialClipState;
	FGeometry AllottedGeometry;
	FGeometry DesktopGeometry;
	FSlateRect CullingBounds;
	FWidgetStyle WidgetStyle;
	FSlateCachedElementListNode* CachedElementListNode;
	/** Starting layer id for drawing children **/
	int32 LayerId;
	int32 OutgoingLayerId;
	EFlowDirection IncomingFlowDirection;
	uint8 bParentEnabled : 1;
	uint8 bInheritedHittestability : 1;

	static const FSlateWidgetPersistentState NoState;
};
template <> struct TIsPODType<FSlateWidgetPersistentState> { enum { Value = true }; };

struct FWidgetStackData
{
	FWidgetStackData()
		: ClipStackIndex(INDEX_NONE)
		, IncomingLayerId(0)
		, CurrentMaxLayerId(0)
	{}

	/** Index into the current stack where the widgets clip state is stored */
	int32 ClipStackIndex;

	/** The incoming layer id for children and draw elements*/
	int32 IncomingLayerId;

	/**
	* Current Max layer id being computed.
	* This is the layer id that would be returned from the widget during a traditional recurive
	* OnPaint call
	*/
	int32 CurrentMaxLayerId;
};

template <> struct TIsPODType<FWidgetStackData> { enum { Value = true }; };

class FWidgetProxyHandle
{
	friend class SWidget;
	friend class FSlateDebugging;
	friend class FSlateInvalidationRoot;
public:
	FWidgetProxyHandle()
		: InvalidationRoot(nullptr)
		, MyIndex(INDEX_NONE)
		, GenerationNumber(INDEX_NONE)

	{}

	SLATECORE_API bool IsValid() const;

	FSlateInvalidationRoot* GetInvalidationRoot() const { return InvalidationRoot; }
	FWidgetProxy& GetProxy();
	const FWidgetProxy& GetProxy() const;

	int32 GetIndex(bool bEvenIfInvalid = false) const { return bEvenIfInvalid || IsValid() ? MyIndex : INDEX_NONE; }

	/**
	 * Marks the widget as updated this frame
	 * Note: If the widget still has update flags (e.g it ticks or is volatile or something during update added new flags)
	 * it will remain in the update list
	 */
	void MarkWidgetUpdatedThisFrame();
	
	void MarkWidgetDirty(EInvalidateWidgetReason InvalidateReason);
	SLATECORE_API void UpdateWidgetFlags(EWidgetUpdateFlags NewFlags);
private:
	FWidgetProxyHandle(FSlateInvalidationRoot& InInvalidationRoot, int32 InIndex);
private:
	/** The root of invalidation tree this proxy belongs to */
	FSlateInvalidationRoot* InvalidationRoot;
	/** Index to myself in the fast path list */
	int32 MyIndex;
	/** This serves as an efficient way to test for validity which does not require invalidating all handles directly*/
	int32 GenerationNumber;
};