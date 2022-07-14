// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FRCLogicModeBase;
class URemoteControlPreset;

/*
* ~ SRCLogicPanelListBase ~
*
* Base UI Widget for Lists in Logic Panels.
* Can represent Controllers / Behaviours / Actions, etc.
*/
class REMOTECONTROLUI_API SRCLogicPanelListBase : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCLogicPanelListBase)
		{
		}

	SLATE_END_ARGS()

	/** Constructs this widget with InArgs */
	void Construct(const FArguments& InArgs);

	/** Returns true if the underlying list is valid and empty. */
	virtual bool IsEmpty() const = 0;
	
	/** Returns number of items in the list. */
	virtual int32 Num() const = 0;

	/** Whether the List View currently has focus.*/
	virtual bool IsListFocused() const = 0;

	/** Deletes currently selected items from the list view*/
	virtual void DeleteSelectedPanelItem()  = 0;

private:

	/** Refreshes the list from the latest state of the model*/
	virtual void Reset() = 0;

	/** Handles broadcasting of a successful remove item operation.
	* This is handled uniquely by each type of child list widget*/
	virtual void BroadcastOnItemRemoved() = 0;

	/** Fetches the Remote Control preset associated with the parent panel */
	virtual URemoteControlPreset* GetPreset() = 0;

	/** Removes the given UI model item from the list of UI models for this panel list*/
	virtual int32 RemoveModel(const TSharedPtr<FRCLogicModeBase> InModel) = 0;

protected:

	/** Helper function for handling common Delete Item functionality across all child panels (Actions/Behaviours/Controllers)
	* Currently invoked from each Panel List child class with appropriate template class*/
	template<class T>
	void DeleteItemFromLogicPanel(TArray<TSharedPtr<T>>& ItemsSource, const TArray<TSharedPtr<T>>& SelectedItems);
};