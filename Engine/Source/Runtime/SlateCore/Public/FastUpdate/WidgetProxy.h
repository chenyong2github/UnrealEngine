// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/PaintArgs.h"
#include "Styling/WidgetStyle.h"
#include "Misc/MemStack.h"
#include "FastUpdate/SlateInvalidationRootHandle.h"
#include "FastUpdate/SlateInvalidationWidgetIndex.h"
#include "FastUpdate/SlateInvalidationWidgetSortOrder.h"
#include "FastUpdate/WidgetUpdateFlags.h"
#include "Layout/Clipping.h"
#include "Layout/FlowDirection.h"
#include "Rendering/DrawElements.h"

class SWidget;
class FPaintArgs;
struct FFastPathPerFrameData;
class FSlateInvalidationWidgetHeap;
class FSlateInvalidationWidgetList;
struct FSlateWidgetPersistentState;
class FSlateInvalidationRoot;

enum class EInvalidateWidgetReason : uint8;

#define UE_SLATE_WITH_WIDGETPROXY_WEAKPTR 0
#define UE_SLATE_VERIFY_WIDGETPROXY_WEAKPTR_STALE 0
#define UE_SLATE_WITH_WIDGETPROXY_WIDGETTYPE 0

class FWidgetProxy
{
public:
	FWidgetProxy(TSharedRef<SWidget>& InWidget);

	int32 Update(const FPaintArgs& PaintArgs, FSlateWindowElementList& OutDrawElements);

	bool ProcessInvalidation(FSlateInvalidationWidgetHeap& UpdateList, FSlateInvalidationWidgetList& FastPathWidgetList, FSlateInvalidationRoot& Root);

	void MarkProxyUpdatedThisFrame(FSlateInvalidationWidgetHeap& UpdateList);

#if UE_SLATE_WITH_WIDGETPROXY_WEAKPTR
	SWidget* GetWidget() const
	{
#if UE_SLATE_VERIFY_WIDGETPROXY_WEAKPTR_STALE
		ensureAlways(!Widget.IsStale()); // Widget.Object != nullptr && !Widget.WeakRerenceCount.IsValid()
#endif
		return Widget.Pin().Get();
	}
	TSharedPtr<SWidget> GetWidgetAsShared() const;
	void ResetWidget() { Widget.Reset(); }
	bool IsSameWidget(const SWidget* InWidget) const
	{
#if UE_SLATE_VERIFY_WIDGETPROXY_WEAKPTR_STALE
		return (InWidget == Widget.Pin().Get()) || (Widget.IsStale() && GetTypeHash(Widget) == GetTypeHash(InWidget));
#else
		return InWidget == Widget.Pin().Get();
#endif
	}
#else
	SWidget* GetWidget() const { return Widget; }
	TSharedPtr<SWidget> GetWidgetAsShared() const;
	void ResetWidget() { Widget = nullptr; }
	bool IsSameWidget(const SWidget* InWidget) const { return InWidget == Widget; }
#endif

private:
	int32 Repaint(const FPaintArgs& PaintArgs, FSlateWindowElementList& OutDrawElements) const;

private:
#if UE_SLATE_WITH_WIDGETPROXY_WEAKPTR
	TWeakPtr<SWidget> Widget;
#else
	SWidget* Widget;
#endif
#if UE_SLATE_WITH_WIDGETPROXY_WIDGETTYPE
	FName WidgetType;
#endif

public:
	FSlateInvalidationWidgetIndex Index;
	FSlateInvalidationWidgetIndex ParentIndex;
	FSlateInvalidationWidgetIndex LeafMostChildIndex;
	EWidgetUpdateFlags UpdateFlags;
	EInvalidateWidgetReason CurrentInvalidateReason;
	/** The widgets own visibility */
	EVisibility Visibility;
	/** Used to make sure we don't double process a widget that is invalidated.  (a widget can invalidate itself but an ancestor can end up painting that widget first thus rendering the child's own invalidate unnecessary */
	uint8 bUpdatedSinceLastInvalidate : 1;
	/** Is the widget already in a pending update list.  If it already is in an update list we don't bother adding it again */
	uint8 bContainedByWidgetHeap : 1;
	/** Use with "Slate.InvalidationRoot.VerifyWidgetVisibility". Cached the last FastPathVisible value to find widgets that do not call Invalidate properly. */
	uint8 bDebug_LastFrameVisible : 1;
	uint8 bDebug_LastFrameVisibleSet : 1;
};

#if !UE_SLATE_WITH_WIDGETPROXY_WIDGETTYPE
static_assert(sizeof(FWidgetProxy) <= 32, "FWidgetProxy should be 32 bytes");
#endif

#if !UE_SLATE_WITH_WIDGETPROXY_WEAKPTR
static_assert(TIsTriviallyDestructible<FWidgetProxy>::Value == true, "FWidgetProxy must be trivially destructible");
template <> struct TIsPODType<FWidgetProxy> { enum { Value = true }; };
#endif

/**
 * Represents the state of a widget from when it last had SWidget::Paint called on it. 
 * This should contain everything needed to directly call Paint on a widget
 */
struct FSlateWidgetPersistentState
{
	FSlateWidgetPersistentState()
		: CachedElementHandle()
		, LayerId(0)
		, OutgoingLayerId(0)
		, IncomingUserIndex(INDEX_NONE)
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
	FSlateCachedElementsHandle CachedElementHandle;
	/** Starting layer id for drawing children **/
	int32 LayerId;
	int32 OutgoingLayerId;
	int8 IncomingUserIndex;
	EFlowDirection IncomingFlowDirection;
	uint8 bParentEnabled : 1;
	uint8 bInheritedHittestability : 1;

	static const FSlateWidgetPersistentState NoState;
};

class FWidgetProxyHandle
{
	friend class SWidget;
	friend class FSlateInvalidationRoot;
	friend class FSlateInvalidationWidgetList;

public:
	FWidgetProxyHandle()
		: WidgetIndex(FSlateInvalidationWidgetIndex::Invalid)
		, GenerationNumber(INDEX_NONE)
	{}

	/** @returns true if it has a valid InvalidationRoot and Index. */
	SLATECORE_API bool IsValid(const SWidget* Widget) const;
	/**
	 * @returns true if it has a valid InvalidationRoot owner
	 * but it could be consider invalid because the InvalidationRoot needs to be rebuilt. */
	SLATECORE_API bool HasValidInvalidationRootOwnership(const SWidget* Widget) const;

	FSlateInvalidationRootHandle GetInvalidationRootHandle() const { return InvalidationRootHandle; }

	FSlateInvalidationWidgetIndex GetWidgetIndex() const { return WidgetIndex; }
	FSlateInvalidationWidgetSortOrder GetWidgetSortOrder() const { return WidgetSortOrder; }

	FWidgetProxy& GetProxy();
	const FWidgetProxy& GetProxy() const;

private:
	bool HasValidIndexAndInvalidationRootHandle() const;
	FSlateInvalidationRoot* GetInvalidationRoot() const { return InvalidationRootHandle.Advanced_GetInvalidationRootNoCheck(); }

	/**
	 * Marks the widget as updated this frame
	 * Note: If the widget still has update flags (e.g it ticks or is volatile or something during update added new flags)
	 * it will remain in the update list
	 */
	void MarkWidgetUpdatedThisFrame();
	
	void MarkWidgetDirty(EInvalidateWidgetReason InvalidateReason);
	SLATECORE_API void UpdateWidgetFlags(const SWidget* Widget, EWidgetUpdateFlags NewFlags);

private:
	FWidgetProxyHandle(const FSlateInvalidationRootHandle& InInvalidationRoot, FSlateInvalidationWidgetIndex InIndex, FSlateInvalidationWidgetSortOrder InSortIndex, int32 InGenerationNumber);
	FWidgetProxyHandle(FSlateInvalidationWidgetIndex InIndex);

private:
	/** The root of invalidation tree this proxy belongs to. */
	FSlateInvalidationRootHandle InvalidationRootHandle;
	/** Index to myself in the fast path list. */
	FSlateInvalidationWidgetIndex WidgetIndex;
	/** Order of the widget in the fast path list. */
	FSlateInvalidationWidgetSortOrder WidgetSortOrder;
	/** This serves as an efficient way to test for validity which does not require invalidating all handles directly. */
	int32 GenerationNumber;
};
