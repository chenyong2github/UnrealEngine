// Copyright Epic Games, Inc. All Rights Reserved.

#if NV_GEFORCENOW

#include "GeForceNOWActionZoneProcessor.h"
#include "Widgets/Accessibility/SlateWidgetTracker.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/Input/EditableTextMetaData.h"
#include "Containers/Ticker.h"
#include "GeForceNOWWrapper.h"
#include "Widgets/SWindow.h"
#include "Widgets/SWidget.h"

static bool bForceProcessGFNWidgetActionZones = false;
FAutoConsoleVariableRef CVarForceProcessGFNWidgetActionZones(
	TEXT("GFN.ForceProcessGFNWidgetActionZones"),
	bForceProcessGFNWidgetActionZones,
	TEXT("Force the processing of GFN Actions Zones even if we aren't running in GFN"));

static float GFNWidgetActionZonesProcessDelay = 0.1f;
FAutoConsoleVariableRef CVarGFNWidgetActionZonesProcessDelay(
	TEXT("GFN.WidgetActionZonesProcessDelay"),
	GFNWidgetActionZonesProcessDelay,
	TEXT("Intervals in seconds between each processing of the GFN Action Zones"));

//---------------------------GFNWidgetActionZone---------------------------

FWidgetGFNActionZone::FWidgetGFNActionZone(const SWidget* InWidget) : Widget(InWidget) {}

void FWidgetGFNActionZone::UpdateActionZone(TArray<TSharedRef<SWindow>>& SlateWindows)
{
	FSlateRect LayoutBoundingRect = Widget->GetPaintSpaceGeometry().GetLayoutBoundingRect();
	const bool bRectChanged = ActionZoneRect != LayoutBoundingRect;
	ActionZoneRect = LayoutBoundingRect;

	FWidgetPath WidgetPath = FSlateApplication::Get().LocateWindowUnderMouse(LayoutBoundingRect.GetCenter(), SlateWindows);
	const bool bIsInteractable = WidgetPath.IsValid() && &WidgetPath.GetLastWidget().Get() == Widget;

	if (bIsInteractable)
	{
		if (bRectChanged || !bWasInteractable)
		{
			if (ActionZoneRect.IsValid() && !ActionZoneRect.IsEmpty())
			{
				//Our Widget is interactable; let GFN know.
				UE_LOG(LogTemp, Warning, TEXT("[GFNWidgetActionZone::UpdateActionZone] Updating Widget %p GFN Action Zone | ActionZoneRect : L: %f , T: %f , R: %f , B: %f"), Widget, ActionZoneRect.Left, ActionZoneRect.Top, ActionZoneRect.Right, ActionZoneRect.Bottom);

				bWasInteractable = true;
				GfnRect ActionZoneGFNRect;
				ActionZoneGFNRect.value1 = ActionZoneRect.Left;
				ActionZoneGFNRect.value2 = ActionZoneRect.Top;
				ActionZoneGFNRect.value3 = ActionZoneRect.Right;
				ActionZoneGFNRect.value4 = ActionZoneRect.Bottom;
				ActionZoneGFNRect.format = gfnRectLTRB;
				ActionZoneGFNRect.normalized = false;
			
				GfnRuntimeError GfnResult = GeForceNOWWrapper::Get().SetActionZone(gfnEditBox, GetID(), &ActionZoneGFNRect);
				if (GfnResult != GfnError::gfnSuccess)
				{
					UE_LOG(LogTemp, Warning, TEXT("[GFNWidgetActionZone::UpdateActionZone] Failed to set Action Zone.  | Error Code : %i"), GfnResult);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[GFNWidgetActionZone::UpdateActionZone] Updating Widget %p GFN Action Zone | No longer interactable"), Widget);
				//Our Widget has an invalid Rect and is no longer interactable; let GFN know.
				bWasInteractable = false;
				ClearActionZone();
			}
		}
	}
	else if (bWasInteractable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GFNWidgetActionZone::UpdateActionZone] Updating Widget %p GFN Action Zone | No longer interactable"), Widget);
		//Our Widget was interactable but no longer is; let GFN know.
		bWasInteractable = false;
		ClearActionZone();
	}
}

void FWidgetGFNActionZone::ClearActionZone()
{
	GfnRuntimeError GfnResult = GeForceNOWWrapper::Get().SetActionZone(gfnEditBox, GetID(), nullptr);
	if (GfnResult != GfnError::gfnSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("[GFNWidgetActionZone::ClearActionZone] Failed to Remove Action Zone. | Error Code : %i"), GfnResult);
	}
}

unsigned int FWidgetGFNActionZone::GetID() const
{
	//Transform the address of our widget into an Id.
	return static_cast<unsigned int>(reinterpret_cast<uintptr_t>(Widget));
}

//--------------------------GeForceNOWActionZoneProcessor--------------------------

void GeForceNOWActionZoneProcessor::Initialize()
{
#if WITH_SLATE_WIDGET_TRACKING

	if (GeForceNOWWrapper::Get().IsRunningInGFN() || bForceProcessGFNWidgetActionZones)
	{
		FSlateWidgetTracker::Get().AddTrackedWidgetListener<FEditableTextMetaData>().AddSP(this, &GeForceNOWActionZoneProcessor::HandleTrackedWidgetChanges);
		if (const TArray<const SWidget*>* ArrayOfWidgets = FSlateWidgetTracker::Get().GetTrackedWidgetsWithMetaData<FEditableTextMetaData>())
		{
			for (const SWidget* Widget : *ArrayOfWidgets)
			{
				HandleEditableTextWidgetRegistered(Widget);
			}
		}
	}

#endif //WITH_SLATE_WIDGET_TRACKING
}

void GeForceNOWActionZoneProcessor::Terminate()
{
#if WITH_SLATE_WIDGET_TRACKING

	if (GeForceNOWWrapper::Get().IsRunningInGFN() || bForceProcessGFNWidgetActionZones)
	{
		FSlateWidgetTracker::Get().RemoveAllTrackedWidgetListenersForObject<FEditableTextMetaData>(this);
	}

#endif //WITH_SLATE_WIDGET_TRACKING
}

void GeForceNOWActionZoneProcessor::HandleTrackedWidgetChanges(const SWidget* Widget, const FName& MetaDataTypeId, ETrackedSlateWidgetOperations Operation)
{
#if WITH_SLATE_WIDGET_TRACKING

	switch (Operation)
	{
		case ETrackedSlateWidgetOperations::AddedTrackedWidget :
			HandleEditableTextWidgetRegistered(Widget);
			break;
		case ETrackedSlateWidgetOperations::RemovedTrackedWidget :
			HandleEditableTextWidgetUnregistered(Widget);
			break;
	}

#endif //WITH_SLATE_WIDGET_TRACKING
}

void GeForceNOWActionZoneProcessor::HandleEditableTextWidgetRegistered(const SWidget* Widget)
{
	UE_LOG(LogTemp, Warning, TEXT("[GeForceNOWActionZoneProcessor::HandleEditableTextWidgetRegistered]"));

	if (GeForceNOWWrapper::Get().IsRunningInGFN() || bForceProcessGFNWidgetActionZones)
	{
		if (GFNWidgetActionZones.Num() == 0)
		{
			StartProcess();
		}
		GFNWidgetActionZones.Add(Widget);
	}
}

void GeForceNOWActionZoneProcessor::HandleEditableTextWidgetUnregistered(const SWidget* Widget)
{
	UE_LOG(LogTemp, Warning, TEXT("[GeForceNOWActionZoneProcessor::HandleEditableTextWidgetUnregistered]"));

	if (GeForceNOWWrapper::Get().IsRunningInGFN() || bForceProcessGFNWidgetActionZones)
	{
		if (FWidgetGFNActionZone* GFNWidgetActionZone = GFNWidgetActionZones.FindByKey(Widget))
		{
			GFNWidgetActionZone->ClearActionZone();
			int32 Index = GFNWidgetActionZones.Find(*GFNWidgetActionZone);
			if (Index != INDEX_NONE)
			{
				GFNWidgetActionZones.RemoveAt(Index);
			}
		}

		if (GFNWidgetActionZones.Num() == 0)
		{
			StopProcess();
		}
	}
}

bool GeForceNOWActionZoneProcessor::ProcessGFNWidgetActionZones(float DeltaTime)
{
	UE_LOG(LogTemp, Warning, TEXT("[SlateGFNAccessibility::ProcessRegisteredWidgets] Start"));

	TArray<TSharedRef<SWindow>> SlateWindows;
	FSlateApplication::Get().GetAllVisibleWindowsOrdered(SlateWindows);

	for (FWidgetGFNActionZone& GFNWidgetActionZone : GFNWidgetActionZones)
	{
		GFNWidgetActionZone.UpdateActionZone(SlateWindows);
	}

	UE_LOG(LogTemp, Warning, TEXT("[SlateGFNAccessibility::ProcessRegisteredWidgets] End"));
	return true;
}

void GeForceNOWActionZoneProcessor::StartProcess()
{
	if (!ProcessDelegateHandle.IsValid())
	{
		ProcessDelegateHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &GeForceNOWActionZoneProcessor::ProcessGFNWidgetActionZones), GFNWidgetActionZonesProcessDelay);
	}
}

void GeForceNOWActionZoneProcessor::StopProcess()
{
	FTicker::GetCoreTicker().RemoveTicker(ProcessDelegateHandle);
	ProcessDelegateHandle.Reset();
}

#endif //NV_GEFORCENOW
