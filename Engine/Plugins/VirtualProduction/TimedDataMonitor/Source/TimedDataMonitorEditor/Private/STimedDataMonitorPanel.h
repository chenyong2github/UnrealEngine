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



class FWorkspaceItem;
class IMessageLogListing;
class STimedDataInputListView;
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

private:
	FReply OnCalibrateClicked();
	TSharedRef<SWidget> OnCalibrateBuildMenu();
	FText GetCalibrateButtonTooltip() const;
	FText GetCalibrateButtonText() const;
	FReply OnResetErrorsClicked();

	EVisibility ShowMessageLog() const;
	EVisibility ShowEditorPerformanceThrottlingWarning() const;
	FReply DisableEditorPerformanceThrottling();

	void BuildCalibrationArray();
	void CalibrateWithTimecode();
	void Jam(bool bWithTimecode);

private:
	TSharedPtr<STimedDataInputListView> TimedDataSourceList;
	TSharedPtr<IMessageLogListing> MessageLogListing;

	static const int32 CalibrationArrayCount = (int32)ETimedDataMonitorEditorCalibrationType::Max;
	FUIAction CalibrationUIAction[CalibrationArrayCount];
	FSlateIcon CalibrationSlateIcon[CalibrationArrayCount];
	FText CalibrationName[CalibrationArrayCount];
	FText CalibrationTooltip[CalibrationArrayCount];

	static FDelegateHandle LevelEditorTabManagerChangedHandle;

};