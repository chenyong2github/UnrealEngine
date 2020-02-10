// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "SlateOptMacros.h"

#include "Framework/Commands/UIAction.h"
#include "Input/Reply.h"
#include "Textures/SlateIcon.h"
#include "TimedDataMonitorEditorSettings.h"


struct FSlateBrush;
class FWorkspaceItem;
class IMessageLogListing;
class STimedDataGenlock;
class STimedDataInputListView;
class STimedDataTimecodeProvider;
class SWidget;


class STimedDataMonitorPanel : public SCompoundWidget
{
public:
	static void RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceItem);
	static void UnregisterNomadTabSpawner();
	static TSharedPtr<STimedDataMonitorPanel> GetPanelInstance();

private:
	using Super = SCompoundWidget;
	static TWeakPtr<STimedDataMonitorPanel> WidgetInstance;

public:
	SLATE_BEGIN_ARGS(STimedDataMonitorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	void RequestRefresh() { bRefreshRequested = true; }

private:
	FReply OnCalibrateClicked();
	TSharedRef<SWidget> OnCalibrateBuildMenu();
	FText GetCalibrateButtonTooltip() const;
	const FSlateBrush* GetCalibrateButtonImage() const;
	FText GetCalibrateButtonText() const;
	FReply OnResetErrorsClicked();
	FReply OnShowBuffersClicked();
	FReply OnGeneralUserSettingsClicked();

	EVisibility ShowMessageLog() const;
	EVisibility ShowEditorPerformanceThrottlingWarning() const;
	FReply DisableEditorPerformanceThrottling();

	EVisibility GetThrobberVisibility() const;

	void BuildCalibrationArray();
	void CalibrateWithTimecode();
	void ApplyTimeCorrection();

private:
	TSharedPtr<STimedDataGenlock> TimedDataGenlockWidget;
	TSharedPtr<STimedDataTimecodeProvider> TimedDataTimecodeWidget;
	TSharedPtr<STimedDataInputListView> TimedDataSourceList;
	TSharedPtr<IMessageLogListing> MessageLogListing;

	static const int32 CalibrationArrayCount = (int32)ETimedDataMonitorEditorCalibrationType::Max;
	FUIAction CalibrationUIAction[CalibrationArrayCount];
	FSlateIcon CalibrationSlateIcon[CalibrationArrayCount];
	FText CalibrationName[CalibrationArrayCount];
	FText CalibrationTooltip[CalibrationArrayCount];

	bool bIsWaitingForCalibration = false;
	bool bRefreshRequested = true;
	double LastCachedValueUpdateTime = 0.0;

	static FDelegateHandle LevelEditorTabManagerChangedHandle;
};