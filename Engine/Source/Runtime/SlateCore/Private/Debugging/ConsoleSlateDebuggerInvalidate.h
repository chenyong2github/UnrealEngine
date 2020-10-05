// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Debugging/SlateDebugging.h"

#if WITH_SLATE_DEBUGGING

#include "CoreMinimal.h"
#include "Debugging/ConsoleSlateDebuggerUtility.h"
#include "Delegates/Delegate.h"
#include "HAL/IConsoleManager.h"

struct FGeometry;
class FPaintArgs;
class FSlateRect;
class FSlateWindowElementList;
class SWidget;
class SWindow;

/**
 * Allows debugging the invalidation from the console.
 * Basics:
 *   Start - SlateDebugger.Invalidate.Start
 *   Stop  - SlateDebugger.Invalidate.Stop
 */
class FConsoleSlateDebuggerInvalidate
{
public:
	FConsoleSlateDebuggerInvalidate();
	~FConsoleSlateDebuggerInvalidate();

	void StartDebugging();
	void StopDebugging();
	bool IsEnabled() const { return bEnabled; }

	void LoadConfig();
	void SaveConfig();

private:	
	struct FInvalidationInfo
	{
		FInvalidationInfo(const FSlateDebuggingInvalidateArgs& Args, int32 InvalidationPriority, bool bBuildWidgetName, bool bUseWidgetPathAsName);

		void ReplaceInvalidated(const FSlateDebuggingInvalidateArgs& Args, int32 InvalidationPriority, bool bBuildWidgetName, bool bUseWidgetPathAsName);
		void ReplaceInvalidator(const FSlateDebuggingInvalidateArgs& Args, int32 InvalidationPriority, bool bBuildWidgetName, bool bUseWidgetPathAsName);
		void UpdateInvalidationReason(const FSlateDebuggingInvalidateArgs& Args, int32 InInvalidationPriority);

		FConsoleSlateDebuggerUtility::TSWidgetId WidgetInvalidatedId;
		FConsoleSlateDebuggerUtility::TSWidgetId WidgetInvalidatorId;
		TWeakPtr<const SWidget> WidgetInvalidated;
		TWeakPtr<const SWidget> WidgetInvalidator;
		FConsoleSlateDebuggerUtility::TSWindowId WindowId;
		FString WidgetInvalidatedName;
		FString WidgetInvalidatorName;
		FVector2D InvalidatedPaintLocation;
		FVector2D InvalidatedPaintSize;
		FVector2D InvalidatorPaintLocation;
		FVector2D InvalidatorPaintSize;
		EInvalidateWidgetReason WidgetReason;
		ESlateDebuggingInvalidateRootReason InvalidationRootReason;
		int32 InvalidationPriority;
		FLinearColor DisplayColor;
		double InvalidationTime;
		bool bIsInvalidatorPaintValid;
	};

	void ToggleLegend();
	void ToggleWidgetNameList();
	void ToggleLogInvalidatedWidget();

	void HandleEnabled(IConsoleVariable* Variable);
	void HandleSetInvalidateWidgetReasonFilter(const TArray<FString>& Params);
	void HandleSetInvalidateRootReasonFilter(const TArray<FString>& Params);

	void HandleEndFrame();
	void HandleWidgetInvalidated(const FSlateDebuggingInvalidateArgs& Args);
	void HandlePaintDebugInfo(const FPaintArgs& InArgs, const FGeometry& InAllottedGeometry, FSlateWindowElementList& InOutDrawElements, int32& InOutLayerId);

	int32 GetInvalidationPriority(EInvalidateWidgetReason InvalidationInfo, ESlateDebuggingInvalidateRootReason InvalidationRootReason) const;
	const FLinearColor& GetColor(const FInvalidationInfo& InvalidationInfo) const;
	void ProcessFrameList();

private:
	bool bEnabled;
	bool bEnabledCVarValue;

	//~ Settings
	bool bDisplayWidgetList;
	bool bUseWidgetPathAsName;
	bool bShowLegend;
	bool bLogInvalidatedWidget;
	EInvalidateWidgetReason InvalidateWidgetReasonFilter;
	ESlateDebuggingInvalidateRootReason InvalidateRootReasonFilter;
	FLinearColor DrawRootRootColor;
	FLinearColor DrawRootChildOrderColor;
	FLinearColor DrawRootScreenPositionColor;
	FLinearColor DrawWidgetLayoutColor;
	FLinearColor DrawWidgetPaintColor;
	FLinearColor DrawWidgetVolatilityColor;
	FLinearColor DrawWidgetChildOrderColor;
	FLinearColor DrawWidgetRenderTransformColor;
	FLinearColor DrawWidgetVisibilityColor;
	int32 MaxNumberOfWidgetInList;
	float CacheDuration;

	//~ Console objects
	FAutoConsoleCommand StartCommand;
	FAutoConsoleCommand StopCommand;
	FAutoConsoleVariableRef EnabledRefCVar;
	FAutoConsoleCommand ToggleLegendCommand;
	FAutoConsoleCommand ToogleWidgetsNameListCommand;
	FAutoConsoleCommand ToogleLogInvalidatedWidgetCommand;
	FAutoConsoleCommand SetInvalidateWidgetReasonFilterCommand;
	FAutoConsoleCommand SetInvalidateRootReasonFilterCommand;
	
	TArray<FInvalidationInfo> InvalidationInfos;
	TArray<FInvalidationInfo> FrameInvalidationInfos;
};

#endif //WITH_SLATE_DEBUGGING