// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "SlateGlobals.h"
#include "Types/SlateConstants.h"
#include "Styling/SlateWidgetStyle.h"

/** How much to scroll for each click of the mouse wheel (in Slate Screen Units). */
TAutoConsoleVariable<float> GlobalScrollAmount(
	TEXT("Slate.GlobalScrollAmount"),
	32.0f,
	TEXT("How much to scroll for each click of the mouse wheel (in Slate Screen Units)."));


float GSlateContrast = 1;

FAutoConsoleVariableRef CVarSlateContrast(
	TEXT("Slate.Contrast"),
	GSlateContrast,
	TEXT("The amount of contrast to apply to the UI (default 1).")
);


// Enable fast widget paths outside the editor by default.  Only reason we don't enable them everywhere
// is that the editor is more complex than a game, and there are likely a larger swath of edge cases.
int32 GSlateFastWidgetPath = 0;

FAutoConsoleVariableRef CVarSlateFastWidgetPath(
	TEXT("Slate.EnableFastWidgetPath"),
	GSlateFastWidgetPath,
	TEXT("Whether or not we enable fast widget pathing.  This mode relies on parent pointers to work correctly.")
);


int32 GSlateEnableGlobalInvalidation = 0;
static FAutoConsoleVariableRef CVarSlateNewUpdateMethod(
	TEXT("Slate.EnableGlobalInvalidation"), 
	GSlateEnableGlobalInvalidation, 
	TEXT("")
);

bool GSlateIsOnFastUpdatePath = false;
bool GSlateIsInInvalidationSlowPath = false;

#if WITH_SLATE_DEBUGGING

bool GSlateInvalidationDebugging = false;
/** True if we should allow widgets to be cached in the UI at all. */
FAutoConsoleVariableRef CVarInvalidationDebugging(
	TEXT("Slate.InvalidationDebugging"),
	GSlateInvalidationDebugging,
	TEXT("Whether to show invalidation debugging visualization"));


bool GSlateHitTestGridDebugging = false;
/** True if we should allow widgets to be cached in the UI at all. */
FAutoConsoleVariableRef CVarHitTestGridDebugging(
	TEXT("Slate.HitTestGridDebugging"),
	GSlateHitTestGridDebugging,
	TEXT("Whether to show a visualization of everything in the hit teest grid"));

#endif

FSlateWidgetStyle::FSlateWidgetStyle()
{ }
