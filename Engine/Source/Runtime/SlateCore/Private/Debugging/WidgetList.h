// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Debugging/SlateDebugging.h"

// Enabled to build a list of all the SWidget currently constructed
#ifndef UE_WITH_SLATE_DEBUG_WIDGETLIST
	#define UE_WITH_SLATE_DEBUG_WIDGETLIST 0
#endif

#if !WITH_SLATE_DEBUGGING && UE_WITH_SLATE_DEBUG_WIDGETLIST
	#error "UE_WITH_SLATE_DEBUG_WIDGETLIST is defined in a none debug build"
#endif

#if UE_WITH_SLATE_DEBUG_WIDGETLIST

class SWidget;

namespace UE
{
namespace Slate
{

class FWidgetList 
{
public:
	static void AddWidget(const SWidget* Widget) { AllWidgets.Add(Widget); }
	static void RemoveWidget(const SWidget* Widget) { AllWidgets.RemoveSingleSwap(Widget); }

	static const TArray<const SWidget*>& GetAllWidgets() { return AllWidgets; }

private:
	FWidgetList() = delete;

	static TArray<const SWidget*> AllWidgets;
};

#define UE_SLATE_DEBUG_WIDGETLIST_ADD_WIDGET(Widget) { ::UE::Slate::FWidgetList::AddWidget(Widget); }
#define UE_SLATE_DEBUG_WIDGETLIST_REMOVE_WIDGET(Widget) { ::UE::Slate::FWidgetList::RemoveWidget(Widget); }

} //Slate
} //UE

#else
#define UE_SLATE_DEBUG_WIDGETLIST_ADD_WIDGET(Widget)
#define UE_SLATE_DEBUG_WIDGETLIST_REMOVE_WIDGET(Widget)
#endif //UE_WITH_SLATE_DEBUG_WIDGETLIST