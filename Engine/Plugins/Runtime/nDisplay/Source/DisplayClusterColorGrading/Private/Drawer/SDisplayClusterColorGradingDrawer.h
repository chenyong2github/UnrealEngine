// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "SDisplayClusterColorGradingObjectList.h"
#include "SDisplayClusterColorGradingColorWheel.h"

class ADisplayClusterRootActor;
class FDisplayClusterOperatorStatusBarExtender;
class FDisplayClusterColorGradingDataModel;
class IDisplayClusterOperatorViewModel;
class IPropertyRowGenerator;
class SHorizontalBox;
class SDisplayClusterColorGradingColorWheelPanel;

/** Stores the state of the drawer UI that can be reloaded in cases where the drawer or any of its elements are reloaded (such as when the drawer is reopened or docked) */
struct FDisplayClusterColorGradingDrawerState
{
	/** The objects that are selected in the list */
	TArray<TWeakObjectPtr<UObject>> SelectedObjects;

	/** The color grading group that is selected */
	int32 SelectedColorGradingGroup = INDEX_NONE;

	/** The color grading element that is selected */
	int32 SelectedColorGradingElement = INDEX_NONE;

	/** Indicates which color wheels are hidden */
	TArray<bool> HiddenColorWheels;

	/** The selected orientation of the color wheels */
	EOrientation ColorWheelOrientation = EOrientation::Orient_Vertical;

	/** The color display mode of the color wheels */
	SDisplayClusterColorGradingColorWheel::EColorDisplayMode ColorDisplayMode;
};

/** Color grading drawer widget, which displays a list of color gradable items, and the color wheel panel */
class SDisplayClusterColorGradingDrawer : public SCompoundWidget, public FEditorUndoClient
{
public:
	~SDisplayClusterColorGradingDrawer();

	SLATE_BEGIN_ARGS(SDisplayClusterColorGradingDrawer)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, bool bInIsInDrawer);

	/** Refreshes the drawer's UI to match the current state of the level and active root actor, optionally preserving UI state */
	void Refresh(bool bPreserveDrawerState = false);

	//~ SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SWidget interface

	//~ FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	//~ End FEditorUndoClient interface

	/** Gets the state of the drawer UI */
	FDisplayClusterColorGradingDrawerState GetDrawerState() const;

	/** Sets the state of the drawer UI */
	void SetDrawerState(const FDisplayClusterColorGradingDrawerState& InDrawerState);

private:
	/** Creates the button used to dock the drawer in the operator panel */
	TSharedRef<SWidget> CreateDockInLayoutButton();

	/** Gets the name of the current level the active root actor is in */
	FText GetCurrentLevelName() const;

	/** Gets the name of the active root actor */
	FText GetCurrentRootActorName() const;

	/** Binds a callback to the BlueprintCompiled delegate of the specified class */
	void BindBlueprintCompiledDelegate(const UClass* Class);

	/** Unbinds a callback to the BlueprintCompiled delegate of the specified class */
	void UnbindBlueprintCompiledDelegate(const UClass* Class);

	/** Fills the level color grading list with any color gradable items found in the currently loaded level */
	void FillLevelColorGradingList();

	/** Fills the root actor color grading list with any color gradable components of the active root actor */
	void FillRootActorColorGradingList();

	/** Fills the color grading group toolbar using the color grading data model */
	void FillColorGradingGroupToolBar();

	/** Gets the visibility state of the color grading group toolbar */
	EVisibility GetColorGradingGroupToolBarVisibility() const;

	/** Gets whether the color grading group at the specified index is currently selected */
	ECheckBoxState IsColorGradingGroupSelected(int32 GroupIndex) const;

	/** Raised when the user has selected the specified color grading group */
	void OnColorGradingGroupCheckedChanged(ECheckBoxState State, int32 GroupIndex);

	/** Raised when the editor replaces any UObjects with new instantiations, usually when actors have been recompiled from blueprints */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap);

	/** Raised when an actor is added to the current level */
	void OnLevelActorAdded(AActor* Actor);

	/** Raised when an actor has been deleted from the currnent level */
	void OnLevelActorDeleted(AActor* Actor);

	/** Raised when the specified blueprint has been recompiled */
	void OnBlueprintCompiled(UBlueprint* Blueprint);

	/** Raised when the user has changed the active root actor selected in the nDisplay operator panel */
	void OnActiveRootActorChanged(ADisplayClusterRootActor* NewRootActor);

	/** Raised when the user has selected a new item in any of the drawer's list views */
	void OnListSelectionChanged(TSharedRef<SDisplayClusterColorGradingObjectList> SourceList, FDisplayClusterColorGradingListItemRef SelectedItem, ESelectInfo::Type SelectInfo);

	/** Raised when the "Dock in Layout" button has been clicked */
	FReply DockInLayout();

private:
	/** The operator panel's view model */
	TSharedPtr<IDisplayClusterOperatorViewModel> OperatorViewModel;

	TSharedPtr<SDisplayClusterColorGradingObjectList> LevelActorsList;
	TSharedPtr<SDisplayClusterColorGradingObjectList> RootActorList;
	TSharedPtr<SHorizontalBox> ColorGradingGroupToolBarBox;
	TSharedPtr<SDisplayClusterColorGradingColorWheelPanel> ColorWheelPanel;

	/** List of color gradable items found in the current level */
	TArray<FDisplayClusterColorGradingListItemRef> LevelColorGradingItems;

	/** List of color gradable actors and components attached to the root actor */
	TArray<FDisplayClusterColorGradingListItemRef> RootActorColorGradingItems;

	/** The color grading data model for the currently selected objects */
	TSharedPtr<FDisplayClusterColorGradingDataModel> ColorGradingDataModel;

	/** Gets whether this widget is in a drawer or docked in a tab */
	bool bIsInDrawer = false;

	/** Indicates that the drawer should refresh itself on the next tick */
	bool bRefreshOnNextTick = false;

	/** Delegate handle for the OnActiveRootActorChanged delegate */
	FDelegateHandle ActiveRootActorChangedHandle;
};