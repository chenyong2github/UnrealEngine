// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

#include "Widgets/SCompoundWidget.h"

class SDMXControlConsoleEditorFaderGroup;
class UDMXControlConsoleFaderBase;
class UDMXControlConsoleFaderGroup;

class ITableRow;
template <typename ItemType> class SListView;
class STableViewBase;


/** A widget which gathers a collection of Faders */
class SDMXControlConsoleEditorFaderGroupView
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXControlConsoleEditorFaderGroupView)
	{}

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs, const TObjectPtr<UDMXControlConsoleFaderGroup>& InFaderGroup);

	/** Gets the Fader Group this Fader Group is based on */
	UDMXControlConsoleFaderGroup* GetFaderGroup() const { return FaderGroup.Get(); }

	/** Gets the index of this Fader Group according to the referenced Fader Group Row */
	int32 GetIndex() const;

	/** Gets Fader Group's name */
	FString GetFaderGroupName() const;

	/** Forces expansion of Faders widget */
	void ExpandFadersWidget();

	/** Gets wether this Fader Group's Faders widget is expanded or not */
	bool IsExpanded() const { return bIsExpanded; }

protected:
	//~ Begin SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End of SWidget interface

private:
	/** Generates Faders widget */
	TSharedRef<SWidget> GenerateFadersWidget();

	/** Generates a Fader widget for each Fader referenced in Faders array */
	TSharedRef<ITableRow> OnGenerateFader(TWeakObjectPtr<UDMXControlConsoleFaderBase> Item, const TSharedRef<STableViewBase>& OwnerTable);
	
	/** Changes Faders ListView selection */
	void OnFaderSelectionChanged(UDMXControlConsoleFaderBase* Fader);

	/** Clears Faders ListView selection */
	void OnClearFaderSelection() const;

	/** Shows/Hides this Fader Group Faders widget  */
	FReply OnFaderGroupExpanded();

	/** Notifies this Fader Group's owner row to add a new Fader Group */
	FReply OnAddFaderGroupClicked() const;

	/** Notifies this Fader Group's owner row to add a new Fader Group Row */
	FReply OnAddFaderGroupRowClicked() const;

	/** Notifies this Fader Group to add a new Fader */
	FReply OnAddFaderClicked();

	/** Should be called when a Fader was added to the Fader Group this view displays */
	void OnFaderAdded();

	/** Should be called when a Fader  was deleted from the Fader Group this view displays */
	void OnFaderRemoved();

	/** Gets Faders widget's visibility */
	EVisibility GetFadersWidgetVisibility() const;

	/** Gets add fader button visibility */
	EVisibility GetAddFaderButtonVisibility() const;

	/** Weak Reference to this Fader Group Row */
	TWeakObjectPtr<UDMXControlConsoleFaderGroup> FaderGroup;

	/** Reference to the Fader Group main widget */
	TSharedPtr<SDMXControlConsoleEditorFaderGroup> FaderGroupWidget;
	
	/** Array of this Fader Group's Faders */
	TArray<TWeakObjectPtr<UDMXControlConsoleFaderBase>> Faders;

	/** Reference to the container widget of this Fader Group's Faders */
	TSharedPtr<SWidget> FadersWidget;

	/** Reference to the Faders ListView */
	TSharedPtr<SListView<TWeakObjectPtr<UDMXControlConsoleFaderBase>>> FadersListView;

	/** Shows is Faders widget is expanded */
	bool bIsExpanded = false;
};
