// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Debugging/SlateDebugging.h"

#if WITH_SLATE_DEBUGGING

#include "CoreMinimal.h"
#include "Delegates/Delegate.h"
#include "Input/Reply.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/DrawElements.h"
#include "FastUpdate/WidgetUpdateFlags.h"

struct FGeometry;
class FPaintArgs;
class FSlateWindowElementList;
class SWidget;

/**
 * Allows debugging the behavior of SWidget::Paint from the console.
 * Basics:
 *   Start - SlateDebugger.Update.Start
 *   Stop  - SlateDebugger.Update.Stop
 */
class FConsoleSlateDebuggerUpdate
{
public:
	FConsoleSlateDebuggerUpdate();
	virtual ~FConsoleSlateDebuggerUpdate();

	void StartDebugging();
	void StopDebugging();
	bool IsEnabled() const { return bEnabled; }

	void ToggleDisplayLegend();
	void ToogleDisplayWidgetNameList();
	void ToogleDisplayUpdateFromPaint();

	void SaveConfig();

private:
	void HandleEnabled(IConsoleVariable* Variable);
	void HandleSetWidgetUpdateFlagsFilter(const TArray<FString>& Params);

	void HandleEndFrame();
	void HandlePaintDebugInfo(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry, FSlateWindowElementList& InOutDrawElements, int32& InOutLayerId);
	void HandleWidgetUpdate(const FSlateDebuggingWidgetUpdatedEventArgs& Args);

private:
	bool bEnabled;
	bool bEnabledCVarValue;

	//~ Settings
	bool bDisplayWidgetsNameList;
	bool bUseWidgetPathAsName;
	bool bDisplayUpdateFromPaint;
	bool bShowLegend;
	bool bShowQuad;
	EWidgetUpdateFlags WidgetUpdateFlagsFilter;
	FLinearColor DrawVolatilePaintColor;
	FLinearColor DrawRepaintColor;
	FLinearColor DrawTickColor;
	FLinearColor DrawActiveTimerColor;
	FLinearColor DrawWidgetNameColor;
	int32 MaxNumberOfWidgetInList;
	int32 InvalidationRootIdFilter;
	float CacheDuration;

	//~ Console objects
	FAutoConsoleCommand StartCommand;
	FAutoConsoleCommand StopCommand;
	FAutoConsoleVariableRef EnabledRefCVar;
	FAutoConsoleCommand ToggleLegendCommand;
	FAutoConsoleCommand ToogleWidgetsNameListCommand;
	FAutoConsoleCommand ToogleDisplayUpdateFromPaintCommand;
	FAutoConsoleCommand SetWidgetUpdateFlagsFilterCommand;
	FAutoConsoleVariableRef InvalidationRootFilterRefCVar;

	using TSWidgetId = UPTRINT;
	using TSWindowId = UPTRINT;
	static const TSWidgetId InvalidWidgetId = 0;
	static const TSWindowId InvalidWindowId = 0;

	struct FWidgetInfo
	{
		FWidgetInfo(const SWidget* Widget, EWidgetUpdateFlags InUpdateFlags);
		void Update(const SWidget* Widget, EWidgetUpdateFlags InUpdateFlags);

		TSWindowId WindowId;
		FVector2D PaintLocation;
		FVector2D PaintSize;
		FString WidgetName;
		EWidgetUpdateFlags UpdateFlags;
		double LastInvalidationTime;
	};

	using TWidgetMap = TMap<TSWidgetId, FWidgetInfo>;
	TWidgetMap UpdatedWidgets;
};

#endif //WITH_SLATE_DEBUGGING