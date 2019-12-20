// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Debugging/SlateDebugging.h"

#if WITH_SLATE_DEBUGGING

#include "SlateGlobals.h"
#include "FastUpdate/WidgetProxy.h"
#include "Animation/CurveSequence.h"
#include "Widgets/SNullWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SWidget.h"
#include "Application/SlateApplicationBase.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY_MODULE(SLATECORE_API, Slate, true);

#define LOCTEXT_NAMESPACE "SlateDebugger"

// Scalar goes from 0 to 1.  0 being all yellow, 1 being all red
FLinearColor YellowToRedFromScalar(float Scalar)
{
	return FLinearColor(1.0f, 1.0f * (1.0f - Scalar), 0.0f);
}

struct FInvalidatedWidgetDrawer
{
	FWidgetProxyHandle ProxyHandle;
	FCurveSequence FadeCurve;
	double StartTime;
	FLinearColor InvalidationColor;

	FInvalidatedWidgetDrawer(const FWidgetProxyHandle& InProxyHandle)
		: ProxyHandle(InProxyHandle)
		, FadeCurve(0, 1.0f, ECurveEaseFunction::Linear)

	{
	}

	/** Widget was invalidated */
	void Refresh(const FLinearColor* CustomInvalidationColor = nullptr)
	{
		if (ProxyHandle.IsValid())
		{
			if (CustomInvalidationColor)
			{
				InvalidationColor = *CustomInvalidationColor;
			}
			else if (FadeCurve.IsPlaying())
			{
				// Color more red based on how recently  this was already invalidated
				InvalidationColor = YellowToRedFromScalar(1.0f - FadeCurve.GetLerp());
			}
			else
			{
				InvalidationColor = FLinearColor::Yellow;
			}

			FadeCurve.Play(SNullWidget::NullWidget, false, 0.0f, false);
		}
	}

	bool Draw(const FPaintArgs& PaintArgs, FSlateWindowElementList& ElementList)
	{
		static const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("FocusRectangle"));

		static const FSlateFontInfo FontInfo = FCoreStyle::Get().GetFontStyle(TEXT("SmallFont"));

		if (ProxyHandle.IsValid() && !FadeCurve.IsAtEnd() && ProxyHandle.GetProxy().Widget)
		{
			SWidget* Widget = ProxyHandle.GetProxy().Widget;

			const FSlateWidgetPersistentState& MyState = Widget->GetPersistentState();
			if (MyState.InitialClipState.IsSet())
			{
				ElementList.GetClippingManager().PushClippingState(MyState.InitialClipState.GetValue());
			}
		
			//ElementList.PushAbsoluteBatchPriortyGroup(MyState.BatchPriorityGroup);
		/*	FSlateDrawElement::MakeText(
				ElementList,
				MyState.OutgoingLayerId + 1,
				MyState.AllottedGeometry.ToPaintGeometry(),
				FText::Format(FText::FromString(TEXT("{0} {1}")), FText::FromString(Widget->GetTypeAsString()), MyState.LayerId),
				FontInfo,
				ESlateDrawEffect::None,
				InvalidationColor.CopyWithNewOpacity(FMath::Lerp(1.0f, 0.0f, FadeCurve.GetLerp()))
			);
*/

			FSlateDrawElement::MakeBox(
				ElementList,
				MyState.OutgoingLayerId + 1,
				MyState.AllottedGeometry.ToPaintGeometry(),
				WhiteBrush,
				ESlateDrawEffect::None,
				InvalidationColor.CopyWithNewOpacity(FMath::Lerp(1.0f, 0.0f, FadeCurve.GetLerp()))
			);

			//ElementList.PopBatchPriortyGroup();

			if (MyState.InitialClipState.IsSet())
			{
				ElementList.PopClip();
			}
			return true;
		}
		return false;
	}
};

FSlateDebuggingInputEventArgs::FSlateDebuggingInputEventArgs(ESlateDebuggingInputEvent InInputEventType, const FReply& InReply, const TSharedPtr<SWidget>& InHandlerWidget, const FString& InAdditionalContent)
	: InputEventType(InInputEventType)
	, Reply(InReply)
	, HandlerWidget(InHandlerWidget)
	, AdditionalContent(InAdditionalContent)
{
}

FSlateDebuggingFocusEventArgs::FSlateDebuggingFocusEventArgs(
	ESlateDebuggingFocusEvent InFocusEventType,
	const FFocusEvent& InFocusEvent,
	const FWeakWidgetPath& InOldFocusedWidgetPath,
	const TSharedPtr<SWidget>& InOldFocusedWidget,
	const FWidgetPath& InNewFocusedWidgetPath,
	const TSharedPtr<SWidget>& InNewFocusedWidget)
	: FocusEventType(InFocusEventType)
	, FocusEvent(InFocusEvent)
	, OldFocusedWidgetPath(InOldFocusedWidgetPath)
	, OldFocusedWidget(InOldFocusedWidget)
	, NewFocusedWidgetPath(InNewFocusedWidgetPath)
	, NewFocusedWidget(InNewFocusedWidget)
{
}

FSlateDebuggingNavigationEventArgs::FSlateDebuggingNavigationEventArgs(
	const FNavigationEvent& InNavigationEvent,
	const FNavigationReply& InNavigationReply,
	const FWidgetPath& InNavigationSource,
	const TSharedPtr<SWidget>& InDestinationWidget,
	const ESlateDebuggingNavigationMethod InNavigationMethod)
	: NavigationEvent(InNavigationEvent)
	, NavigationReply(InNavigationReply)
	, NavigationSource(InNavigationSource)
	, DestinationWidget(InDestinationWidget)
	, NavigationMethod(InNavigationMethod)
{
}

FSlateDebuggingWarningEventArgs::FSlateDebuggingWarningEventArgs(
	const FText& InWarning,
	const TSharedPtr<SWidget>& InOptionalContextWidget)
	: Warning(InWarning)
	, OptionalContextWidget(InOptionalContextWidget)
{
}

FSlateDebuggingMouseCaptureEventArgs::FSlateDebuggingMouseCaptureEventArgs(
	bool InCaptured,
	uint32 InUserIndex,
	uint32 InPointerIndex,
	const TSharedPtr<const SWidget>& InCapturingWidget)
	: Captured(InCaptured)
	, UserIndex(InUserIndex)
	, PointerIndex(InPointerIndex)
	, CaptureWidget(InCapturingWidget)
{
}

FSlateDebugging::FBeginWindow FSlateDebugging::BeginWindow;

FSlateDebugging::FEndWindow FSlateDebugging::EndWindow;

FSlateDebugging::FBeginWidgetPaint FSlateDebugging::BeginWidgetPaint;

FSlateDebugging::FEndWidgetPaint FSlateDebugging::EndWidgetPaint;

FSlateDebugging::FDrawElement FSlateDebugging::ElementAdded;

FSlateDebugging::FWidgetWarningEvent FSlateDebugging::Warning;

FSlateDebugging::FWidgetInputEvent FSlateDebugging::InputEvent;

FSlateDebugging::FWidgetFocusEvent FSlateDebugging::FocusEvent;

FSlateDebugging::FWidgetAttemptNavigationEvent FSlateDebugging::AttemptNavigationEvent;

FSlateDebugging::FWidgetExecuteNavigationEvent FSlateDebugging::ExecuteNavigationEvent;

FSlateDebugging::FWidgetMouseCaptureEvent FSlateDebugging::MouseCaptureEvent;

FSlateDebugging::FUICommandRun FSlateDebugging::CommandRun;

DECLARE_MULTICAST_DELEGATE_OneParam(FWidgetMouseCaptureEvent, const FSlateDebuggingMouseCaptureEventArgs& /*EventArgs*/);

TArray<struct FInvalidatedWidgetDrawer> FSlateDebugging::InvalidatedWidgetDrawers;

void FSlateDebugging::BroadcastWarning(const FText& WarningText, const TSharedPtr<SWidget>& OptionalContextWidget)
{
	Warning.Broadcast(FSlateDebuggingWarningEventArgs(WarningText, OptionalContextWidget));
}

void FSlateDebugging::BroadcastInputEvent(ESlateDebuggingInputEvent InputEventType, const FReply& InReply)
{
	if (InReply.IsEventHandled())
	{
		InputEvent.Broadcast(FSlateDebuggingInputEventArgs(InputEventType, InReply, TSharedPtr<SWidget>(), TEXT("")));
	}
}

void FSlateDebugging::BroadcastInputEvent(ESlateDebuggingInputEvent InputEventType, const TSharedPtr<SWidget>& HandlerWidget)
{
	InputEvent.Broadcast(FSlateDebuggingInputEventArgs(InputEventType, FReply::Handled(), TSharedPtr<SWidget>(), TEXT("")));
}

void FSlateDebugging::BroadcastInputEvent(ESlateDebuggingInputEvent InputEventType, const FReply& InReply, const TSharedPtr<SWidget>& HandlerWidget)
{
	if (InReply.IsEventHandled())
	{
		InputEvent.Broadcast(FSlateDebuggingInputEventArgs(InputEventType, InReply, HandlerWidget, TEXT("")));
	}
}

void FSlateDebugging::BroadcastInputEvent(ESlateDebuggingInputEvent InputEventType, const FReply& InReply, const TSharedPtr<SWidget>& HandlerWidget, const FString& AdditionalContent)
{
	if (InReply.IsEventHandled())
	{
		InputEvent.Broadcast(FSlateDebuggingInputEventArgs(InputEventType, InReply, HandlerWidget, AdditionalContent));
	}
}

void FSlateDebugging::BroadcastFocusChanging(const FFocusEvent& InFocusEvent, const FWeakWidgetPath& InOldFocusedWidgetPath, const TSharedPtr<SWidget>& InOldFocusedWidget, const FWidgetPath& InNewFocusedWidgetPath, const TSharedPtr<SWidget>& InNewFocusedWidget)
{
	FocusEvent.Broadcast(FSlateDebuggingFocusEventArgs(ESlateDebuggingFocusEvent::FocusChanging, InFocusEvent, InOldFocusedWidgetPath, InOldFocusedWidget, InNewFocusedWidgetPath, InNewFocusedWidget));
}

void FSlateDebugging::BroadcastFocusLost(const FFocusEvent& InFocusEvent, const FWeakWidgetPath& InOldFocusedWidgetPath, const TSharedPtr<SWidget>& InOldFocusedWidget, const FWidgetPath& InNewFocusedWidgetPath, const TSharedPtr<SWidget>& InNewFocusedWidget)
{
	FocusEvent.Broadcast(FSlateDebuggingFocusEventArgs(ESlateDebuggingFocusEvent::FocusLost, InFocusEvent, InOldFocusedWidgetPath, InOldFocusedWidget, InNewFocusedWidgetPath, InNewFocusedWidget));
}

void FSlateDebugging::BroadcastFocusReceived(const FFocusEvent& InFocusEvent, const FWeakWidgetPath& InOldFocusedWidgetPath, const TSharedPtr<SWidget>& InOldFocusedWidget, const FWidgetPath& InNewFocusedWidgetPath, const TSharedPtr<SWidget>& InNewFocusedWidget)
{
	FocusEvent.Broadcast(FSlateDebuggingFocusEventArgs(ESlateDebuggingFocusEvent::FocusReceived, InFocusEvent, InOldFocusedWidgetPath, InOldFocusedWidget, InNewFocusedWidgetPath, InNewFocusedWidget));
}

void FSlateDebugging::BroadcastAttemptNavigation(const FNavigationEvent& InNavigationEvent, const FNavigationReply& InNavigationReply, const FWidgetPath& InNavigationSource, const TSharedPtr<SWidget>& InDestinationWidget, ESlateDebuggingNavigationMethod InNavigationMethod)
{
	AttemptNavigationEvent.Broadcast(FSlateDebuggingNavigationEventArgs(InNavigationEvent, InNavigationReply, InNavigationSource, InDestinationWidget, InNavigationMethod));
}

void FSlateDebugging::BroadcastExecuteNavigation()
{
	ExecuteNavigationEvent.Broadcast(FSlateDebuggingExecuteNavigationEventArgs());
}

void FSlateDebugging::BroadcastMouseCapture(uint32 UserIndex, uint32 PointerIndex, TSharedPtr<const SWidget> InCapturingWidget)
{
	MouseCaptureEvent.Broadcast(FSlateDebuggingMouseCaptureEventArgs(true, UserIndex, PointerIndex, InCapturingWidget));
}

void FSlateDebugging::BroadcastMouseCaptureLost(uint32 UserIndex, uint32 PointerIndex, TSharedPtr<const SWidget> InWidgetLostCapture)
{
	MouseCaptureEvent.Broadcast(FSlateDebuggingMouseCaptureEventArgs(false, UserIndex, PointerIndex, InWidgetLostCapture));
}

void FSlateDebugging::WidgetInvalidated(FSlateInvalidationRoot& InvalidationRoot, const class FWidgetProxy& WidgetProxy, const FLinearColor* CustomInvalidationColor)
{
	if(FSlateApplicationBase::IsInitialized())
	{
		int32 Index = WidgetProxy.Index;
	
		FInvalidatedWidgetDrawer* Drawer = InvalidatedWidgetDrawers.FindByPredicate([&InvalidationRoot, Index](FInvalidatedWidgetDrawer& InDrawer) { 
			return InDrawer.ProxyHandle.GetInvalidationRoot() == &InvalidationRoot && InDrawer.ProxyHandle.GetIndex() == Index;
		});

		if (!Drawer) 
		{
			Drawer = &InvalidatedWidgetDrawers.Emplace_GetRef(FWidgetProxyHandle(InvalidationRoot, WidgetProxy.Index));
		}

		Drawer->Refresh(CustomInvalidationColor);
	}
}

void FSlateDebugging::DrawInvalidationRoot(const SWidget& RootWidget, int32 LayerId, FSlateWindowElementList& OutDrawElements)
{
	if(GSlateInvalidationDebugging)
	{
		static const FSlateBrush* WhiteBrush = FCoreStyle::Get().GetBrush(TEXT("FocusRectangle"));

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			LayerId,
			RootWidget.GetPaintSpaceGeometry().ToPaintGeometry(),
			WhiteBrush,
			ESlateDrawEffect::None,
			FLinearColor(128, 0, 128)
		);
	}
}

void FSlateDebugging::DrawInvalidatedWidgets(const FSlateInvalidationRoot& Root, const FPaintArgs& PaintArgs, FSlateWindowElementList& OutDrawElements)
{
	for (int DrawerIdx = 0; DrawerIdx < InvalidatedWidgetDrawers.Num(); ++DrawerIdx)
	{		
		FInvalidatedWidgetDrawer& Drawer = InvalidatedWidgetDrawers[DrawerIdx];
		
		if(Drawer.ProxyHandle.GetInvalidationRoot() == &Root)
		{
			if (!Drawer.Draw(PaintArgs, OutDrawElements))
			{
				InvalidatedWidgetDrawers.RemoveAtSwap(DrawerIdx, 1, false);
				--DrawerIdx;
			}
		}
	}
}


void FSlateDebugging::ClearInvalidatedWidgets(const FSlateInvalidationRoot& Root)
{
	for (int DrawerIdx = 0; DrawerIdx < InvalidatedWidgetDrawers.Num(); ++DrawerIdx)
	{
		FInvalidatedWidgetDrawer& Drawer = InvalidatedWidgetDrawers[DrawerIdx];

		if (Drawer.ProxyHandle.GetInvalidationRoot() == &Root)
		{
			InvalidatedWidgetDrawers.RemoveAtSwap(DrawerIdx, 1, false);
		}
	}
}

#undef LOCTEXT_NAMESPACE

#endif