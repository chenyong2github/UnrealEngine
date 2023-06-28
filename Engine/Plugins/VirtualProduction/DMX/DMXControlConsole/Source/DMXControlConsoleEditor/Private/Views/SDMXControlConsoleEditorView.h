// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Engine/EngineTypes.h"
#include "Widgets/SCompoundWidget.h"

enum class EDMXControlConsoleEditorViewMode : uint8;
class FUICommandList;
class IDetailsView;
class SDMXControlConsoleEditorFaderGroupRowView;
class SDMXControlConsoleEditorFixturePatchVerticalBox;
class SDMXControlConsoleEditorPortSelector;
class SDockTab;
class SScrollBox;
class SSearchBox;
class SVerticalBox;
class UDMXControlConsoleEditorModel;
class UDMXControlConsoleFaderGroup;
class UDMXControlConsoleFaderGroupRow;
class UDMXControlConsoleData;


/** Widget for the DMX Control Console */
class SDMXControlConsoleEditorView
	: public SCompoundWidget
	, public FSelfRegisteringEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SDMXControlConsoleEditorView)
	{}

	SLATE_END_ARGS()

	/** Destructor */
	~SDMXControlConsoleEditorView();

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Gets DMX Control Console Editor Model instance reference */
	UDMXControlConsoleEditorModel& GetEditorConsoleModel() const;

	/** Gets current DMX Control Console Data */
	UDMXControlConsoleData* GetControlConsoleData() const;

protected:
	//~ Begin SWidget interface
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End of SWidget interface
	
	//~ Begin FSelfRegisteringEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	//~ End FSelfRegisteringEditorUndoClient interface

private:
	/** Registers commands for this view */
	void RegisterCommands();

	/** Generates the toolbar for this view */
	TSharedRef<SWidget> GenerateToolbar();

	/** Generates a widget to select the current input mode */
	TSharedRef<SWidget> GenerateInputModeMenuWidget();

	/** Generates a widget to select the current view mode */
	TSharedRef<SWidget> GenerateViewModeMenuWidget();

	/** Generates a widget for selection options */
	TSharedRef<SWidget> GenerateSelectionMenuWidget();

	/** Finds FaderGroupRowView by the given FaderGroupRow, if valid */
	TSharedPtr<SDMXControlConsoleEditorFaderGroupRowView> FindFaderGroupRowView(const UDMXControlConsoleFaderGroupRow* FaderGroupRow);

	/** Restores global search filter text from Constrol Console Data */
	void RestoreGlobalFilter();

	/** Requests to update the Details Views on the next tick */
	void RequestUpdateDetailsViews();

	/** Updates the Details Views */
	void ForceUpdateDetailsViews();

	/** Updates FixturePatchVerticalBox widget */
	void UpdateFixturePatchVerticalBox();

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

	/** Called when a Fader Groups view mode is selected */
	void OnFaderGroupsViewModeSelected(const EDMXControlConsoleEditorViewMode ViewMode) const;

	/** Called when a Faders view mode is selected */
	void OnFadersViewModeSelected(const EDMXControlConsoleEditorViewMode ViewMode) const;

	/** Called when a Selection option is selected */
	void OnSelectAll(bool bOnlyMatchingFilter = false) const;

	/** Called when Control Console gets cleared */
	void OnClearAll();

	/** Called when Port selection changes */
	void OnSelectedPortsChanged();

	/** Called when a Property has changed in current Control Console Data */
	void OnControlConsoleDataPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Called when the browse to asset button was clicked */
	void OnBrowseToAssetClicked();

	/** Called when a console was loaded */
	void OnConsoleLoaded();

	/** Called when a console was saved */
	void OnConsoleSaved();

	/** Called when a FaderGroupView needs to be scrolled into view */
	void OnScrollIntoView(const UDMXControlConsoleFaderGroup* FaderGroup);

	/** Called when the active tab in the editor changes */
	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated);

	/** Searches this widget's parents to see if it's a child of InDockTab */
	bool IsWidgetInTab(TSharedPtr<SDockTab> InDockTab, TSharedPtr<SWidget> InWidget) const;

	/** Gets visibility for each FaderGroupRowView widget */
	EVisibility GetFaderGroupRowViewVisibility(UDMXControlConsoleFaderGroupRow* FaderGroupRow) const;

	/** Gets add button visibility */
	EVisibility GetAddButtonVisibility() const;

	/** Gets Details Views section visibility */
	EVisibility GetDetailViewsSectionVisibility() const;

	/** Reference to the container widget of this DMX Control Console's Fader Group Rows slots */
	TSharedPtr<SVerticalBox> FaderGroupRowsVerticalBox;

	/** Reference to FixturePatchRows widgets container */
	TSharedPtr<SDMXControlConsoleEditorFixturePatchVerticalBox> FixturePatchVerticalBox;

	/** Reference to horizontal ScrollBox widget */
	TSharedPtr<SScrollBox> HorizontalScrollBox;

	/** Reference to vertical ScrollBox widget */
	TSharedPtr<SScrollBox> VerticalScrollBox;

	/** Reference to Control Console searchbox used for global filtering */
	TSharedPtr<SSearchBox> GlobalFilterSearchBox;

	/** Widget to handle Port selection */
	TSharedPtr<SDMXControlConsoleEditorPortSelector> PortSelector;

	/** Shows DMX Control Console Data's details */
	TSharedPtr<IDetailsView> ControlConsoleDataDetailsView;

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
