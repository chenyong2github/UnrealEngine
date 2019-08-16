// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "FastUpdate/WidgetProxy.h"
#include "Widgets/SWidget.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SWindow.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Types/ReflectionMetadata.h"

const FSlateWidgetPersistentState FSlateWidgetPersistentState::NoState;

FWidgetProxy::FWidgetProxy(SWidget* InWidget)
	: Widget(InWidget)
	, Index(INDEX_NONE)
	, ParentIndex(INDEX_NONE)
	, NumChildren(0)
	, LeafMostChildIndex(INDEX_NONE)
	, UpdateFlags(EWidgetUpdateFlags::None)
	, CurrentInvalidateReason(EInvalidateWidget::None)
	// Potentially unsafe to update visibility from the widget due to attribute bindings.  This is updated later when the widgets are sorted in ProcessInvalidation
	, Visibility(EVisibility::Collapsed) 
	, bUpdatedSinceLastInvalidate(false)
	, bInUpdateList(false)
	, bInvisibleDueToParentOrSelfVisibility(false)
	, bChildOrderInvalid(false)
{
	check(Widget != nullptr);
}

int32 FWidgetProxy::Update(const FPaintArgs& PaintArgs, int32 MyIndex, FSlateWindowElementList& OutDrawElements)
{
	// If Outgoing layer id remains index none, there was no change
	int32 OutgoingLayerId = INDEX_NONE;
	if (EnumHasAnyFlags(UpdateFlags,  EWidgetUpdateFlags::NeedsRepaint|EWidgetUpdateFlags::NeedsVolatilePaint))
	{
		ensure(!bInvisibleDueToParentOrSelfVisibility);
		OutgoingLayerId = Repaint(PaintArgs, MyIndex, OutDrawElements);
	}
	else if(!bInvisibleDueToParentOrSelfVisibility)
	{
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
	}

	return OutgoingLayerId;
}

bool FWidgetProxy::ProcessInvalidation(FWidgetUpdateList& UpdateList, TArray<FWidgetProxy>& FastWidgetPathList, FSlateInvalidationRoot& Root)
{
	bool bWidgetNeedsRepaint = false;
	if (!bInvisibleDueToParentOrSelfVisibility && ParentIndex != INDEX_NONE && !Widget->PrepassLayoutScaleMultiplier.IsSet())
	{
		// If this widget has never been prepassed make sure the parent prepasses it to set the correct multiplier
		FWidgetProxy& ParentProxy = FastWidgetPathList[ParentIndex];
		if (ParentProxy.Widget)
		{
			ParentProxy.Widget->InvalidatePrepass();
			ParentProxy.CurrentInvalidateReason |= EInvalidateWidget::Layout;
			//UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;
			UpdateList.Push(ParentProxy);
		}
		bWidgetNeedsRepaint = true;
	}
	else if (EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidget::RenderTransform | EInvalidateWidget::Layout | EInvalidateWidget::Visibility | EInvalidateWidget::ChildOrder))
	{
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
			UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;
		}

		// If the desired size changed, invalidate the parent if it is visible
		if (NewDesiredSize != CurrentDesiredSize || EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidget::Visibility|EInvalidateWidget::RenderTransform))
		{
			if (ParentIndex != INDEX_NONE)
			{
				FWidgetProxy& ParentProxy = FastWidgetPathList[ParentIndex];
				if (ParentIndex == 0)
				{
					// root of the invalidation panel just invalidate the whole thing
					Root.InvalidateRoot();
				}
				else if (ParentProxy.Visibility.IsVisible())
				{
					ParentProxy.CurrentInvalidateReason |= EInvalidateWidget::Layout;
					UpdateList.Push(ParentProxy);
				}
			}
			else if (!GSlateEnableGlobalInvalidation && Widget->IsParentValid())
			{
				TSharedPtr<SWidget> ParentWidget = Widget->GetParentWidget();
				if (ParentWidget->Advanced_IsInvalidationRoot())
				{
					Root.InvalidateRoot();
				}
			}
		}

		bWidgetNeedsRepaint = true;
	}
	else if (EnumHasAnyFlags(CurrentInvalidateReason, EInvalidateWidget::Paint) && !Widget->IsVolatileIndirectly())
	{
		UpdateFlags |= EWidgetUpdateFlags::NeedsRepaint;

		bWidgetNeedsRepaint = true;
	}

	CurrentInvalidateReason = EInvalidateWidget::None;

	return bWidgetNeedsRepaint;
}

void FWidgetProxy::MarkProxyUpdatedThisFrame(FWidgetProxy& Proxy, FWidgetUpdateList& UpdateList)
{
	Proxy.bUpdatedSinceLastInvalidate = true;

	if(EnumHasAnyFlags(Proxy.UpdateFlags, EWidgetUpdateFlags::AnyUpdate))
	{
		if (!Proxy.bInUpdateList && !Proxy.bInvisibleDueToParentOrSelfVisibility)
		{
			// If there are any updates still needed add them to the next update list
			UpdateList.Push(Proxy);
		}
	}
	else
	{
		Proxy.bInUpdateList = false;
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

	if (bNeedsNewClipState)
	{
		OutDrawElements.PopClip();
		// clip index should be what it was before.  if this assert fails something internal inside the above paint call did not pop clip properly
		check(StartingClipIndex == OutDrawElements.GetClippingIndex());
	}

	return NewLayerId;
}

FWidgetProxyHandle::FWidgetProxyHandle(FSlateInvalidationRoot& InInvalidationRoot, int32 InIndex)
	: InvalidationRoot(&InInvalidationRoot)
	, MyIndex(InIndex)
	, GenerationNumber(InInvalidationRoot.GetFastPathGenerationNumber())
{

}

bool FWidgetProxyHandle::IsValid() const
{
	return InvalidationRoot && InvalidationRoot->GetFastPathGenerationNumber() == GenerationNumber && MyIndex != INDEX_NONE;
}

FWidgetProxy& FWidgetProxyHandle::GetProxy()
{
	return InvalidationRoot->FastWidgetPathList[MyIndex];
}

const FWidgetProxy& FWidgetProxyHandle::GetProxy() const
{
	return InvalidationRoot->FastWidgetPathList[MyIndex];
}

void FWidgetProxyHandle::MarkWidgetUpdatedThisFrame()
{
	FWidgetProxy::MarkProxyUpdatedThisFrame(GetProxy(), InvalidationRoot->WidgetsNeedingUpdate);
}

void FWidgetProxyHandle::MarkWidgetDirty(EInvalidateWidget InvalidateReason)
{
	FWidgetProxy& Proxy = GetProxy();

	if (EnumHasAnyFlags(InvalidateReason, EInvalidateWidget::ChildOrder))
	{
		/*
				CSV_EVENT_GLOBAL(TEXT("Slow Path Needed"));
		#if WITH_SLATE_DEBUGGING
				UE_LOG(LogSlate, Log, TEXT("Slow Widget Path Needed: %s %s"), *Proxy.Widget->ToString(), *Proxy.Widget->GetTag().ToString());
		#endif*/
		Proxy.bChildOrderInvalid = true;
		InvalidationRoot->InvalidateChildOrder();
	}

	if (Proxy.CurrentInvalidateReason == EInvalidateWidget::None)
	{
		InvalidationRoot->WidgetsNeedingUpdate.Push(Proxy);
	}
#if 0
	else
	{
		ensure(InvalidationRoot->WidgetsNeedingUpdate.Contains(Proxy));
	}
#endif
	Proxy.CurrentInvalidateReason |= InvalidateReason;
}

void FWidgetProxyHandle::UpdateWidgetFlags(EWidgetUpdateFlags NewFlags)
{
	FWidgetProxy& Proxy = GetProxy();

	if (!Proxy.bInvisibleDueToParentOrSelfVisibility)
	{
		Proxy.UpdateFlags = NewFlags;

		// Add to update list if the widget is now tickable or has an active timer.  Disregard if dirty, it was already in the list
		if (!Proxy.bInUpdateList && EnumHasAnyFlags(NewFlags, EWidgetUpdateFlags::AnyUpdate))
		{
			InvalidationRoot->WidgetsNeedingUpdate.Push(Proxy);
		}
	}
}
