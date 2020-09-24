// Copyright Epic Games, Inc. All Rights Reserved.

#include "FastUpdate/WidgetProxy.h"
#include "Widgets/SWidget.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SWindow.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Types/ReflectionMetadata.h"
#include "Input/HittestGrid.h"
#include "Trace/SlateTrace.h"
#include "Widgets/SWidgetUtils.h"

const FSlateWidgetPersistentState FSlateWidgetPersistentState::NoState;

FWidgetProxy::FWidgetProxy(SWidget& InWidget)
	: Widget(&InWidget)
	, Index(INDEX_NONE)
	, ParentIndex(INDEX_NONE)
	, NumChildren(0)
	, LeafMostChildIndex(INDEX_NONE)
	, UpdateFlags(EWidgetUpdateFlags::None)
	, CurrentInvalidateReason(EInvalidateWidgetReason::None)
	// Potentially unsafe to update visibility from the widget due to attribute bindings.  This is updated later when the widgets are sorted in ProcessInvalidation
	, Visibility(EVisibility::Collapsed) 
	, bUpdatedSinceLastInvalidate(false)
	, bInUpdateList(false)
	, bInvisibleDueToParentOrSelfVisibility(false)
	, bChildOrderInvalid(false)
{
}

int32 FWidgetProxy::Update(const FPaintArgs& PaintArgs, int32 MyIndex, FSlateWindowElementList& OutDrawElements)
{
// Commenting this since it could be triggered in specific cases where Widgte->UpdateFlags in reset but the Widget Proxy is still in the  update list
//#if WITH_SLATE_DEBUGGING
//	ensure(UpdateFlags == Widget->UpdateFlags);
//#endif

	// If Outgoing layer id remains index none, there was no change
	int32 OutgoingLayerId = INDEX_NONE;
	if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsRepaint|EWidgetUpdateFlags::NeedsVolatilePaint))
	{
		ensure(!bInvisibleDueToParentOrSelfVisibility);
		OutgoingLayerId = Repaint(PaintArgs, MyIndex, OutDrawElements);
	}
	else if(!bInvisibleDueToParentOrSelfVisibility)
	{
		EWidgetUpdateFlags PreviousUpdateFlag = UpdateFlags;

		if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsActiveTimerUpdate))
		{
			SCOPE_CYCLE_COUNTER(STAT_SlateExecuteActiveTimers);
			Widget->ExecuteActiveTimers(PaintArgs.GetCurrentTime(), PaintArgs.GetDeltaTime());
		}

		if (EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsTick))
		{
			const FSlateWidgetPersistentState& MyState = Widget->GetPersistentState();

			INC_DWORD_STAT(STAT_SlateNumTickedWidgets);
			SCOPE_CYCLE_COUNTER(STAT_SlateTickWidgets);

			Widget->Tick(MyState.DesktopGeometry, PaintArgs.GetCurrentTime(), PaintArgs.GetDeltaTime());
		}

#if WITH_SLATE_DEBUGGING
		FSlateDebugging::BroadcastWidgetUpdated(Widget, PreviousUpdateFlag);
#endif
		UE_TRACE_SLATE_WIDGET_UPDATED(Widget, PreviousUpdateFlag);
	}

	return OutgoingLayerId;
}

bool FWidgetProxy::ProcessInvalidation(FWidgetUpdateList& UpdateList, TArray<FWidgetProxy>& FastWidgetPathList, FSlateInvalidationRoot& Root)
{
	bool bWidgetNeedsRepaint = false;
	if (!bInvisibleDueToParentOrSelfVisibility && ParentIndex != INDEX_NONE && !Widget->PrepassLayoutScaleMultiplier.IsSet())
	{
		SCOPE_CYCLE_SWIDGET(Widget);
		// If this widget has never been prepassed make sure the parent prepasses it to set the correct multiplier
		FWidgetProxy& ParentProxy = FastWidgetPathList[ParentIndex];
		if (ParentProxy.Widget)
		{
			ParentProxy.Widget->InvalidatePrepass();
			ParentProxy.CurrentInvalidateReason |= EInvalidateWidgetReason::Layout;
#if WITH_SLATE_DEBUGGING
			FSlateDebugging::BroadcastWidgetInvalidate(ParentProxy.Widget, Widget, EInvalidateWidgetReason::Layout);
#endif
			UE_TRACE_SLATE_WIDGET_INVALIDATED(ParentProxy.Widget, Widget, EInvalidateWidgetReason::Layout);
			UpdateList.Push(ParentProxy);
		}
		bWidgetNeedsRepaint = true;
	}
	else if (EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidgetReason::RenderTransform | EInvalidateWidgetReason::Layout | EInvalidateWidgetReason::Visibility | EInvalidateWidgetReason::ChildOrder))
	{
		SCOPE_CYCLE_SWIDGET(Widget);
		// When layout changes compute a new desired size for this widget
		FVector2D CurrentDesiredSize = Widget->GetDesiredSize();
		FVector2D NewDesiredSize = FVector2D::ZeroVector;
		if (Visibility != EVisibility::Collapsed)
		{
			if (Widget->NeedsPrepass() || (!GSlateEnableGlobalInvalidation && Widget->Advanced_IsInvalidationRoot()))
			{
				Widget->SlatePrepass(Widget->PrepassLayoutScaleMultiplier.Get(1.0f));
			}
			else
			{
				Widget->CacheDesiredSize(Widget->PrepassLayoutScaleMultiplier.Get(1.0f));
			}

			NewDesiredSize = Widget->GetDesiredSize();
		}

		// Note even if volatile we need to recompute desired size. We do not need to invalidate parents though if they are volatile since they will naturally redraw this widget
		if (!Widget->IsVolatileIndirectly() && Visibility.IsVisible())
		{
			// Set the value directly instead of calling AddUpdateFlags as an optimization
			Widget->UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;
			UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;
		}

		// If the desired size changed, invalidate the parent if it is visible
		if (NewDesiredSize != CurrentDesiredSize || EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidgetReason::Visibility|EInvalidateWidgetReason::RenderTransform))
		{
			if (ParentIndex != INDEX_NONE)
			{
				FWidgetProxy& ParentProxy = FastWidgetPathList[ParentIndex];
				if (ParentIndex == 0)
				{
					// root of the invalidation panel just invalidate the whole thing
					Root.InvalidateRoot(Widget);
				}
				else if (ParentProxy.Visibility.IsVisible())
				{
					ParentProxy.CurrentInvalidateReason |= EInvalidateWidgetReason::Layout;
#if WITH_SLATE_DEBUGGING
					FSlateDebugging::BroadcastWidgetInvalidate(ParentProxy.Widget, Widget, EInvalidateWidgetReason::Layout);
#endif
					UE_TRACE_SLATE_WIDGET_INVALIDATED(ParentProxy.Widget, Widget, EInvalidateWidgetReason::Layout);
					UpdateList.Push(ParentProxy);
				}
			}
			else if (!GSlateEnableGlobalInvalidation && Widget->IsParentValid())
			{
				TSharedPtr<SWidget> ParentWidget = Widget->GetParentWidget();
				if (ParentWidget->Advanced_IsInvalidationRoot())
				{
					Root.InvalidateRoot(Widget);
				}
			}
		}

		bWidgetNeedsRepaint = true;
	}
	else if (EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidgetReason::Paint) && !Widget->IsVolatileIndirectly())
	{
		SCOPE_CYCLE_SWIDGET(Widget);
		// Set the value directly instead of calling AddUpdateFlags as an optimization
		Widget->UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;
		UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;

		bWidgetNeedsRepaint = true;
	}

	CurrentInvalidateReason = EInvalidateWidgetReason::None;

	return bWidgetNeedsRepaint;
}

void FWidgetProxy::MarkProxyUpdatedThisFrame(FWidgetUpdateList& UpdateList)
{
	bUpdatedSinceLastInvalidate = true;

	if(EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::AnyUpdate))
	{
		if (!bInUpdateList && !bInvisibleDueToParentOrSelfVisibility)
		{
			// If there are any updates still needed add them to the next update list
			UpdateList.Push(*this);
		}
	}
	else
	{
		bInUpdateList = false;
	}
}

int32 FWidgetProxy::Repaint(const FPaintArgs& PaintArgs, int32 MyIndex, FSlateWindowElementList& OutDrawElements) const
{
#if WITH_SLATE_DEBUGGING
	SCOPED_NAMED_EVENT_FSTRING(FReflectionMetaData::GetWidgetDebugInfo(Widget), EnumHasAnyFlags(UpdateFlags, EWidgetUpdateFlags::NeedsRepaint) ? FColor::Orange : FColor::Red);
#endif

	const FSlateWidgetPersistentState& MyState = Widget->GetPersistentState();

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
			if (Widget->ShouldInvalidatePrepassDueToVolatility())
			{
				Widget->InvalidatePrepass();
			}
			Widget->SlatePrepass(Widget->GetPrepassLayoutScaleMultiplier());
		}
	}
	const int32 NewLayerId = Widget->Paint(UpdatedArgs, MyState.AllottedGeometry, MyState.CullingBounds, OutDrawElements, MyState.LayerId, MyState.WidgetStyle, MyState.bParentEnabled);

	PaintArgs.GetHittestGrid().SetUserIndex(PrevUserIndex);

	if (bNeedsNewClipState)
	{
		OutDrawElements.PopClip();
		// clip index should be what it was before.  if this assert fails something internal inside the above paint call did not pop clip properly
		check(StartingClipIndex == OutDrawElements.GetClippingIndex());
	}

	return NewLayerId;
}

FWidgetProxyHandle::FWidgetProxyHandle(FSlateInvalidationRoot& InInvalidationRoot, int32 InIndex)
	: InvalidationRootHandle(InInvalidationRoot.GetInvalidationRootHandle())
	, MyIndex(InIndex)
	, GenerationNumber(InInvalidationRoot.GetFastPathGenerationNumber())
{

}

bool FWidgetProxyHandle::IsValid() const
{
	FSlateInvalidationRoot* InvalidationRoot = InvalidationRootHandle.GetInvalidationRoot();
	return InvalidationRoot && InvalidationRoot->GetFastPathGenerationNumber() == GenerationNumber && MyIndex != INDEX_NONE;
}

FWidgetProxy& FWidgetProxyHandle::GetProxy()
{
	check(IsValid());
	return GetInvalidationRoot()->FastWidgetPathList[MyIndex];
}

const FWidgetProxy& FWidgetProxyHandle::GetProxy() const
{
	check(IsValid());
	return GetInvalidationRoot()->FastWidgetPathList[MyIndex];
}

void FWidgetProxyHandle::MarkWidgetUpdatedThisFrame()
{
	check(IsValid());
	GetInvalidationRoot()->FastWidgetPathList[MyIndex].MarkProxyUpdatedThisFrame(GetInvalidationRoot()->WidgetsNeedingUpdate);
}

void FWidgetProxyHandle::MarkWidgetDirty(EInvalidateWidgetReason InvalidateReason)
{
	check(IsValid());
	FWidgetProxy& Proxy = GetInvalidationRoot()->FastWidgetPathList[MyIndex];

	if (EnumHasAnyFlags(InvalidateReason, EInvalidateWidgetReason::ChildOrder))
	{
		/*
				CSV_EVENT_GLOBAL(TEXT("Slow Path Needed"));
		#if WITH_SLATE_DEBUGGING
				UE_LOG(LogSlate, Log, TEXT("Slow Widget Path Needed: %s %s"), *Proxy.Widget->ToString(), *Proxy.Widget->GetTag().ToString());
		#endif*/
		Proxy.bChildOrderInvalid = true;
		GetInvalidationRoot()->InvalidateChildOrder(Proxy.Widget);
	}

	if (Proxy.CurrentInvalidateReason == EInvalidateWidgetReason::None)
	{
		GetInvalidationRoot()->WidgetsNeedingUpdate.Push(Proxy);
	}
#if 0
	else
	{
		ensure(InvalidationRoot->WidgetsNeedingUpdate.Contains(Proxy));
	}
#endif
	Proxy.CurrentInvalidateReason |= InvalidateReason;
#if WITH_SLATE_DEBUGGING
	FSlateDebugging::BroadcastWidgetInvalidate(Proxy.Widget, nullptr, InvalidateReason);
#endif
	UE_TRACE_SLATE_WIDGET_INVALIDATED(Proxy.Widget, nullptr, InvalidateReason);
}

void FWidgetProxyHandle::UpdateWidgetFlags(EWidgetUpdateFlags NewFlags)
{
	check(IsValid());
	FWidgetProxy& Proxy = GetInvalidationRoot()->FastWidgetPathList[MyIndex];

	if (!Proxy.bInvisibleDueToParentOrSelfVisibility)
	{
		Proxy.UpdateFlags = NewFlags;

		// Add to update list if the widget is now tickable or has an active timer.  Disregard if dirty, it was already in the list
		if (!Proxy.bInUpdateList && EnumHasAnyFlags(NewFlags, EWidgetUpdateFlags::AnyUpdate))
		{
			GetInvalidationRoot()->WidgetsNeedingUpdate.Push(Proxy);
		}
	}
}
