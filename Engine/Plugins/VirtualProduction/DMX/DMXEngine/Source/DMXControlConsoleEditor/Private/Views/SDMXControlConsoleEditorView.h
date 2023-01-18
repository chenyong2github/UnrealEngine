// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Widgets/SCompoundWidget.h"

class SDMXControlConsoleEditorFaderGroupRowView;
class SDMXControlConsoleEditorFixturePatchVerticalBox;
class SDMXControlConsoleEditorPresetWidget;
class UDMXControlConsoleFaderGroupRow;
class UDMXControlConsole;

class IDetailsView;
class SDockTab;
class SVerticalBox;


/** Widget for the DMX Control Console */
class SDMXControlConsoleEditorView
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXControlConsoleEditorView)
	{}

	SLATE_END_ARGS()

	/** Destructor */
	~SDMXControlConsoleEditorView();

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Gets DMX Control Console */
	UDMXControlConsole* GetControlConsole() const;

protected:
	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End of SWidget interface

private:
	/** Generates the toolbar for this DMX Control Console View */
	TSharedRef<SWidget> GenerateToolbar();

	/** Updates Details View's object arrays */
	void UpdateDetailsViews();

	/** Updates FixturePatchVerticalBox widget */
	void UpdateFixturePatchRows();

	/** Should be called when a Fader Group Row was added to the this view displays */
	void OnFaderGroupRowAdded();

	/** Adds a Fader Group Row slot widget */
	void AddFaderGroupRow(UDMXControlConsoleFaderGroupRow* FaderGroupRow);

	/** Should be called when a Fader Group was deleted from the this view displays */
	void OnFaderGroupRowRemoved();

	/** Checks if FaderGroupRows array contains a reference to the given */
	bool IsFaderGroupRowContained(UDMXControlConsoleFaderGroupRow* FaderGroupRow);

	/** Called to add first first Fader Group */
	FReply OnAddFirstFaderGroup();

	/** Called when the active tab in the editor changes */
	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated);

	/** Searches this widget's parents to see if it's a child of InDockTab */
	bool IsWidgetInTab(TSharedPtr<SDockTab> InDockTab, TSharedPtr<SWidget> InWidget) const;

	/** Gets add button visibility */
	EVisibility GetAddButtonVisibility() const;

	/** Reference to the container widget of this DMX Control Console's Fader Group Rows slots */
	TSharedPtr<SVerticalBox> FaderGroupRowsVerticalBox;

	/** Reference to FixturePatchRows widgets container */
	TSharedPtr<SDMXControlConsoleEditorFixturePatchVerticalBox> FixturePatchVerticalBox;

	/** Widget to handle saving/loading of Control Console's data */
	TSharedPtr<SDMXControlConsoleEditorPresetWidget> ControlConsolePresetWidget;

	/** Shows DMX Control Console's details */
	TSharedPtr<IDetailsView> ControlConsoleDetailsView;

	/** Shows details of the current selected Fader Groups */
	TSharedPtr<IDetailsView> FaderGroupsDetailsView;

	/** Shows details of the current selected Faders */
	TSharedPtr<IDetailsView> FadersDetailsView;

	/** Array of weak references to Fader Group Row widgets */
	TArray<TWeakPtr<SDMXControlConsoleEditorFaderGroupRowView>> FaderGroupRowViews;

	/** Delegate handle bound to the FGlobalTabmanager::OnActiveTabChanged delegate */
	FDelegateHandle OnActiveTabChangedDelegateHandle;
};
