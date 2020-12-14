// Copyright Epic Games, Inc. All Rights Reserved.

#include "FastUpdate/WidgetProxy.h"
#include "Widgets/SWidget.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SWindow.h"
#include "FastUpdate/SlateInvalidationWidgetHeap.h"
#include "FastUpdate/SlateInvalidationWidgetList.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Types/ReflectionMetadata.h"
#include "Input/HittestGrid.h"
#include "Trace/SlateTrace.h"
#include "Widgets/SWidgetUtils.h"

const FSlateWidgetPersistentState FSlateWidgetPersistentState::NoState;

FWidgetProxy::FWidgetProxy(TSharedRef<SWidget>& InWidget)
#if UE_SLATE_WITH_WIDGETPROXY_WEAKPTR
	: Widget(InWidget)
#else
	: Widget(&InWidget.Get())
#endif
	, Index(FSlateInvalidationWidgetIndex::Invalid)
	, ParentIndex(FSlateInvalidationWidgetIndex::Invalid)
	, LeafMostChildIndex(FSlateInvalidationWidgetIndex::Invalid)
	, UpdateFlags(EWidgetUpdateFlags::None)
	, CurrentInvalidateReason(EInvalidateWidgetReason::None)
	// Potentially unsafe to update visibility from the widget due to attribute bindings.  This is updated later when the widgets are sorted in ProcessInvalidation
	, Visibility(EVisibility::Collapsed) 
	, bUpdatedSinceLastInvalidate(false)
	, bContainedByWidgetHeap(false)
	, bDebug_LastFrameVisible(true)
	, bDebug_LastFrameVisibleSet(false)
{
#if UE_SLATE_WITH_WIDGETPROXY_WIDGETTYPE
	WidgetName = GetWidget()->GetType();
#endif
}

TSharedPtr<SWidget> FWidgetProxy::GetWidgetAsShared() const
{
#if UE_SLATE_WITH_WIDGETPROXY_WEAKPTR
	return Widget.Pin();
#else
	return Widget ? Widget->AsShared() : TSharedPtr<SWidget>();
#endif
}

int32 FWidgetProxy::Update(const FPaintArgs& PaintArgs, FSlateWindowElementList& OutDrawElements)
{
// Commenting this since it could be triggered in specific cases where Widgte->UpdateFlags in reset but the Widget Proxy is still in the  update list
//#if WITH_SLATE_DEBUGGING
//	ensure(UpdateFlags == Widget->UpdateFlags);
//#endif

	TSharedPtr<SWidget> CurrentWidget = GetWidgetAsShared();

	// If Outgoing layer id remains index none, there was no change
	int32 OutgoingLayerId = INDEX_NONE;
	if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsRepaint|EWidgetUpdateFlags::NeedsVolatilePaint))
	{
		check(CurrentWidget->IsFastPathVisible());
		OutgoingLayerId = Repaint(PaintArgs, OutDrawElements);
	}
	else if(CurrentWidget->IsFastPathVisible())
	{
		EWidgetUpdateFlags PreviousUpdateFlag = UpdateFlags;
		if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsActiveTimerUpdate))
		{
			SCOPE_CYCLE_COUNTER(STAT_SlateExecuteActiveTimers);
			CurrentWidget->ExecuteActiveTimers(PaintArgs.GetCurrentTime(), PaintArgs.GetDeltaTime());
		}

		if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsTick))
		{
			const FSlateWidgetPersistentState& MyState = CurrentWidget->GetPersistentState();

			INC_DWORD_STAT(STAT_SlateNumTickedWidgets);
			SCOPE_CYCLE_COUNTER(STAT_SlateTickWidgets);

			CurrentWidget->Tick(MyState.DesktopGeometry, PaintArgs.GetCurrentTime(), PaintArgs.GetDeltaTime());
		}

#if WITH_SLATE_DEBUGGING
		FSlateDebugging::BroadcastWidgetUpdated(CurrentWidget.Get(), PreviousUpdateFlag);
#endif
		UE_TRACE_SLATE_WIDGET_UPDATED(CurrentWidget.Get(), PreviousUpdateFlag);
	}

	return OutgoingLayerId;
}

bool FWidgetProxy::ProcessInvalidation(FSlateInvalidationWidgetHeap& UpdateList, FSlateInvalidationWidgetList& FastWidgetPathList, FSlateInvalidationRoot& Root)
{
	bool bWidgetNeedsRepaint = false;
	SWidget* WidgetPtr = GetWidget();

	if (WidgetPtr->IsFastPathVisible() && ParentIndex != FSlateInvalidationWidgetIndex::Invalid && !WidgetPtr->PrepassLayoutScaleMultiplier.IsSet())
	{
		SCOPE_CYCLE_SWIDGET(WidgetPtr);
		// If this widget has never been prepassed make sure the parent prepasses it to set the correct multiplier
		FWidgetProxy& ParentProxy = FastWidgetPathList[ParentIndex];
		if (SWidget* ParentWidgetPtr = ParentProxy.GetWidget())
		{
			ParentWidgetPtr->InvalidatePrepass();
			ParentProxy.CurrentInvalidateReason |= EInvalidateWidgetReason::Layout;
#if WITH_SLATE_DEBUGGING
			FSlateDebugging::BroadcastWidgetInvalidate(ParentWidgetPtr, WidgetPtr, EInvalidateWidgetReason::Layout);
#endif
			UE_TRACE_SLATE_WIDGET_INVALIDATED(ParentWidgetPtr, WidgetPtr, EInvalidateWidgetReason::Layout);
			UpdateList.PushUnique(ParentProxy);
		}
		bWidgetNeedsRepaint = true;
	}
	else if (EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidgetReason::RenderTransform | EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Visibility | EInvalidateWidgetReason::ChildOrder))
	{
		SCOPE_CYCLE_SWIDGET(WidgetPtr);
		// When layout changes compute a new desired size for this widget
		FVector2D CurrentDesiredSize = WidgetPtr->GetDesiredSize();
		FVector2D NewDesiredSize = FVector2D::ZeroVector;
		if (Visibility != EVisibility::Collapsed)
		{
			if (WidgetPtr->NeedsPrepass())
			{
				WidgetPtr->SlatePrepass(WidgetPtr->PrepassLayoutScaleMultiplier.Get(1.0f));
			}
			else
			{
				WidgetPtr->CacheDesiredSize(WidgetPtr->PrepassLayoutScaleMultiplier.Get(1.0f));
			}

			NewDesiredSize = WidgetPtr->GetDesiredSize();
		}

		// Note even if volatile we need to recompute desired size. We do not need to invalidate parents though if they are volatile since they will naturally redraw this widget
		if (!WidgetPtr->IsVolatileIndirectly() && Visibility.IsVisible())
		{
			// Set the value directly instead of calling AddUpdateFlags as an optimization
			WidgetPtr->UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;
			UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;
		}

		// If the desired size changed, invalidate the parent if it is visible
		if (NewDesiredSize != CurrentDesiredSize || EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidgetReason::Visibility|EInvalidateWidgetReason::RenderTransform))
		{
			if (ParentIndex != FSlateInvalidationWidgetIndex::Invalid)
			{
				FWidgetProxy& ParentProxy = FastWidgetPathList[ParentIndex];
				if (ParentIndex == FastWidgetPathList.FirstIndex())
				{
					// root of the invalidation panel just invalidate the whole thing
					Root.InvalidateRootLayout(WidgetPtr);
				}
				else if (ParentProxy.Visibility.IsVisible())
				{
					ParentProxy.CurrentInvalidateReason |= EInvalidateWidgetReason::Layout;
#if WITH_SLATE_DEBUGGING
					FSlateDebugging::BroadcastWidgetInvalidate(ParentProxy.GetWidget(), WidgetPtr, EInvalidateWidgetReason::Layout);
#endif
					UE_TRACE_SLATE_WIDGET_INVALIDATED(ParentProxy.GetWidget(), WidgetPtr, EInvalidateWidgetReason::Layout);
					UpdateList.PushUnique(ParentProxy);
				}
			}
			else if (TSharedPtr<SWidget> ParentWidget = WidgetPtr->GetParentWidget())
			{
				ParentWidget->Invalidate(EInvalidateWidgetReason::Layout);
			}
		}

		bWidgetNeedsRepaint = true;
	}
	else if (EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidgetReason::Paint) && !WidgetPtr->IsVolatileIndirectly())
	{
		SCOPE_CYCLE_SWIDGET(WidgetPtr);
		// Set the value directly instead of calling AddUpdateFlags as an optimization
		WidgetPtr->UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;
		UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;

		bWidgetNeedsRepaint = true;
	}

	CurrentInvalidateReason = EInvalidateWidgetReason::None;

	return bWidgetNeedsRepaint;
}

void FWidgetProxy::MarkProxyUpdatedThisFrame(FSlateInvalidationWidgetHeap& UpdateList)
{
	bUpdatedSinceLastInvalidate = true;

	if(EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::AnyUpdate))
	{
		SWidget* WidgetPtr = GetWidget();
		if (WidgetPtr && WidgetPtr->IsFastPathVisible())
		{
			// If there are any updates still needed add them to the next update list
			UpdateList.PushUnique(*this);
		}
	}
}

int32 FWidgetProxy::Repaint(const FPaintArgs& PaintArgs, FSlateWindowElementList& OutDrawElements) const
{
	SWidget* WidgetPtr = GetWidget();
	check(WidgetPtr);

#if WITH_SLATE_DEBUGGING
	SCOPED_NAMED_EVENT_FSTRING(FReflectionMetaData::GetWidgetDebugInfo(WidgetPtr), EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsRepaint) ? FColor::Orange : FColor::Red);
#endif

	const FSlateWidgetPersistentState& MyState = WidgetPtr->GetPersistentState();

	const int32 StartingClipIndex = OutDrawElements.GetClippingIndex();

	// Get the clipping manager into the correct state
	const bool bNeedsNewClipState = MyState.InitialClipState.IsSet();
	if (bNeedsNewClipState)
	{
		OutDrawElements.GetClippingManager().PushClippingState(MyState.InitialClipState.GetValue());
	}
	
	const int32 PrevUserIndex = PaintArgs.GetHittestGrid().GetUserIndex();

	PaintArgs.GetHittestGrid().SetUserIndex(MyState.IncomingUserIndex);
	GSlateFlowDirection = MyState.IncomingFlowDirection;
	
	FPaintArgs UpdatedArgs = PaintArgs.WithNewParent(MyState.PaintParent.Pin().Get());
	UpdatedArgs.SetInheritedHittestability(MyState.bInheritedHittestability);

	int32 PrevLayerId = MyState.OutgoingLayerId;

	if (GSlateEnableGlobalInvalidation)
	{
		if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsVolatilePaint))
		{
			if (WidgetPtr->ShouldInvalidatePrepassDueToVolatility())
			{
				WidgetPtr->InvalidatePrepass();
			}
			WidgetPtr->SlatePrepass(WidgetPtr->GetPrepassLayoutScaleMultiplier());
		}
	}
	const int32 NewLayerId = WidgetPtr->Paint(UpdatedArgs, MyState.AllottedGeometry, MyState.CullingBounds, OutDrawElements, MyState.LayerId, MyState.WidgetStyle, MyState.bParentEnabled);

	PaintArgs.GetHittestGrid().SetUserIndex(PrevUserIndex);

	if (bNeedsNewClipState)
	{
		OutDrawElements.PopClip();
		// clip index should be what it was before.  if this assert fails something internal inside the above paint call did not pop clip properly
		check(StartingClipIndex == OutDrawElements.GetClippingIndex());
	}

	return NewLayerId;
}

FWidgetProxyHandle::FWidgetProxyHandle(const FSlateInvalidationRootHandle& InInvalidationRoot, FSlateInvalidationWidgetIndex InIndex, FSlateInvalidationWidgetSortOrder InSortIndex, int32 InGenerationNumber)
	: InvalidationRootHandle(InInvalidationRoot)
	, WidgetIndex(InIndex)
	, WidgetSortOrder(InSortIndex)
	, GenerationNumber(InGenerationNumber)
{
}

FWidgetProxyHandle::FWidgetProxyHandle(FSlateInvalidationWidgetIndex InIndex)
	: InvalidationRootHandle()
	, WidgetIndex(InIndex)
	, WidgetSortOrder()
	, GenerationNumber(INDEX_NONE)
{

}

bool FWidgetProxyHandle::IsValid(const SWidget* Widget) const
{
	FSlateInvalidationRoot* InvalidationRoot = InvalidationRootHandle.GetInvalidationRoot();
	return InvalidationRoot
		&& InvalidationRoot->GetFastPathGenerationNumber() == GenerationNumber
		&& InvalidationRoot->GetFastPathWidgetList().IsValidIndex(WidgetIndex)
		&& InvalidationRoot->GetFastPathWidgetList()[WidgetIndex].GetWidget() == Widget;
}

bool FWidgetProxyHandle::HasValidInvalidationRootOwnership(const SWidget* Widget) const
{
	FSlateInvalidationRoot* InvalidationRoot = InvalidationRootHandle.GetInvalidationRoot();
	return InvalidationRoot
		&& InvalidationRoot->GetFastPathWidgetList().GetGenerationNumber() == GenerationNumber
		&& InvalidationRoot->GetFastPathWidgetList().IsValidIndex(WidgetIndex)
		&& InvalidationRoot->GetFastPathWidgetList()[WidgetIndex].GetWidget() == Widget;
}

FWidgetProxy& FWidgetProxyHandle::GetProxy()
{
	return GetInvalidationRoot()->GetFastPathWidgetList()[WidgetIndex];
}

const FWidgetProxy& FWidgetProxyHandle::GetProxy() const
{
	return GetInvalidationRoot()->GetFastPathWidgetList()[WidgetIndex];
}

void FWidgetProxyHandle::MarkWidgetUpdatedThisFrame()
{
	GetInvalidationRoot()->GetFastPathWidgetList()[WidgetIndex].MarkProxyUpdatedThisFrame(*GetInvalidationRoot()->WidgetsNeedingUpdate);
}

void FWidgetProxyHandle::MarkWidgetDirty(EInvalidateWidgetReason InvalidateReason)
{
	FWidgetProxy& Proxy = GetInvalidationRoot()->GetFastPathWidgetList()[WidgetIndex];
	SWidget* WidgetPtr = Proxy.GetWidget();

	if (EnumHasAnyFlags(InvalidateReason, EInvalidateWidgetReason::ChildOrder))
	{
		GetInvalidationRoot()->InvalidateWidgetChildOrder(WidgetPtr->AsShared());
	}

	if (Proxy.CurrentInvalidateReason == EInvalidateWidgetReason::None)
	{
		GetInvalidationRoot()->WidgetsNeedingUpdate->PushUnique(Proxy);
	}
#if 0
	else
	{
		ensure(InvalidationRoot->WidgetsNeedingUpdate.Contains(Proxy));
	}
#endif
	Proxy.CurrentInvalidateReason |= InvalidateReason;
#if WITH_SLATE_DEBUGGING
	FSlateDebugging::BroadcastWidgetInvalidate(WidgetPtr, nullptr, InvalidateReason);
#endif
	UE_TRACE_SLATE_WIDGET_INVALIDATED(WidgetPtr, nullptr, InvalidateReason);
}

void FWidgetProxyHandle::UpdateWidgetFlags(const SWidget* Widget, EWidgetUpdateFlags NewFlags)
{
	if (IsValid(Widget))
	{
		FWidgetProxy& Proxy = GetInvalidationRoot()->GetFastPathWidgetList()[WidgetIndex];

		if (Widget->IsFastPathVisible())
		{
			Proxy.UpdateFlags = NewFlags;

			// Add to update list if the widget is now tickable or has an active timer.
			if (EnumHasAnyFlags(NewFlags, EWidgetUpdateFlags::AnyUpdate))
			{
				GetInvalidationRoot()->WidgetsNeedingUpdate->PushUnique(Proxy);
			}
		}
	}
}
