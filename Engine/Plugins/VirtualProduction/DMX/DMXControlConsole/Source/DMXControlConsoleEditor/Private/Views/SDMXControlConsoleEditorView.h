// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/EngineTypes.h"
#include "Widgets/SCompoundWidget.h"

class SDMXControlConsoleEditorFaderGroupRowView;
class SDMXControlConsoleEditorFixturePatchVerticalBox;
class SDMXControlConsoleEditorPresetWidget;
class UDMXControlConsoleFaderGroupRow;
class UDMXControlConsole;

class FUICommandList;
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
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End of SWidget interface

private:
	/** Registers commands for this view */
	void RegisterCommands();

	/** Generates the toolbar for this view */
	TSharedRef<SWidget> GenerateToolbar();

	/** Requests to update the Details Views on the next tick */
	void RequestUpdateDetailsViews();

	/** Updates the Details Views */
	void ForceUpdateDetailsViews();

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

	/** Called when the search text changed */
	void OnSearchTextChanged(const FText& SearchText);

	/** Called to add first first Fader Group */
	FReply OnAddFirstFaderGroup();

	/** Called when the browse to preset button was clicked */
	void OnBrowseToPresetClicked();

	/** Returns true if any preset was loaded to the console */
	bool IsAnyPresetLoaded() const;

	/** Called when a preset is loaded */
	void OnPresetLoaded();

	/** Called when a preset is saved */
	void OnPresetSaved();

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

	/** Timer handle in use while updating details views is requested but not carried out yet */
	FTimerHandle UpdateDetailsViewTimerHandle;

	/** Command list for the Control Console Editor View */
	TSharedPtr<FUICommandList> CommandList;
};
