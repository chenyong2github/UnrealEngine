// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "CoreMinimal.h"
#include "SlateFwd.h"
#include "Styling/SlateColor.h"
#include "Widgets/DeclarativeSyntaxSupport.h"



class SDataProviderListView;
class SDataProviderActivities;
class FWorkspaceItem;

/**
 * Panel used to show stage monitoring data
 */
class SStageMonitorPanel : public SCompoundWidget
{
public:
	virtual ~SStageMonitorPanel() = default;

	static void RegisterNomadTabSpawner(TSharedRef<FWorkspaceItem> InWorkspaceItem);
	static void UnregisterNomadTabSpawner();
	static TSharedPtr<SStageMonitorPanel> GetPanelInstance();

private:
	using Super = SCompoundWidget;
	static TWeakPtr<SStageMonitorPanel> PanelInstance;
	static FDelegateHandle LevelEditorTabManagerChangedHandle;

public:
	SLATE_BEGIN_ARGS(SStageMonitorPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	/** Make the toolbar widgets */
	TSharedRef<SWidget> MakeToolbarWidget();

	/** HAndles clear entries button clicked */
	FReply OnClearEntriesClicked();
	
	/** Handles StageMonitor settings button clicked */
	FReply OnShowProjectSettingsClicked();

	/** Get the stage status */
	FSlateColor GetStageStatus() const;

	/** Get the top reason why the stage is considered active */
	FText GetStageActiveStateReasonText() const;

	/** Returns the monitor status whether it's actively listening for data providers or not */
	FText GetMonitorStatus() const;

private:

	/** Used to show all providers with their frame data */
	TSharedPtr<SDataProviderListView> DataProviderList;

	/** Used to show every activities received by the monitor */
	TSharedPtr<SDataProviderActivities> DataProviderActivities;
};
