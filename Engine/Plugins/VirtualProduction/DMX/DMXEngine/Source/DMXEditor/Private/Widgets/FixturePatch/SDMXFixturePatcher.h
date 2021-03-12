// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Styling/SlateColor.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FDMXEditor;
class FDMXFixturePatchNode;
class FDMXFixturePatchSharedData;
class FDMXTreeNodeBase;
class SDMXPatchedUniverse;
class UDMXLibrary;
class UDMXEntity;
class UDMXEntityFixturePatch;
class UDMXEntityFixtureType;

class SCheckBox;
class SScrollBox;
class SDockTab;
enum class ECheckBoxState : uint8;


/** Widget to assign fixture patches to universes and their channels */
class SDMXFixturePatcher
	: public SCompoundWidget
	, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SDMXFixturePatcher)
		: _DMXEditor(nullptr)
	{}

		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

		SLATE_EVENT(FSimpleDelegate, OnPatched)

	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);

	/** Updates the patcher, should be called on property changes */
	void NotifyPropertyChanged(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Refreshes the whole view from properties, does not consider changes in the library */
	void RefreshFromProperties();

	/** Refreshes the whole view from the library, considers library changes */
	void RefreshFromLibrary();

	/** Selects a universe that contains selected patches, if possible */
	void SelectUniverseThatContainsSelectedPatches();

protected:
	/** Called when the active tab changed */
	void OnActiveTabChanged(TSharedPtr<SDockTab> PreviouslyActive, TSharedPtr<SDockTab> NewlyActivated);

	/** Called when entities in the library got updated */
	void OnEntitiesUpdated(UDMXLibrary* DMXLibrary);

	// Begin SWidget Interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual FReply OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent) override;
	// ~End SWidget Interface

	/** Called when drag enters a channel */
	void OnDragEnterChannel(int32 UniverseID, int32 ChannelID, const FDragDropEvent& DragDropEvent);

	/** Called when drag dropped onto a channel */
	FReply OnDropOntoChannel(int32 UniverseID, int32 ChannelID, const FDragDropEvent& DragDropEvent);

	/** Initilizes the widget for an incoming dragged patch, and the patch so it can be dragged here */
	TSharedPtr<FDMXFixturePatchNode> GetDraggedNode(const TArray<TWeakObjectPtr<UDMXEntity>>& DraggedEntities);
	
	/** Creates an info widget for drag dropping */
	TSharedRef<SWidget> CreateDragDropDecorator(TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch, int32 ChannelID) const;

	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End of FEditorUndoClient

protected:
	/** Finds specified fixture patch in all universes */
	TSharedPtr<FDMXFixturePatchNode> FindPatchNode(TWeakObjectPtr<UDMXEntityFixturePatch> Patch) const;

	/** Returns first node with same fixture type */
	TSharedPtr<FDMXFixturePatchNode> FindPatchNodeOfType(UDMXEntityFixtureType* Type, const TSharedPtr<FDMXFixturePatchNode>& IgoredNode) const;

protected:
	/** Selects a universe */
	void SelectUniverse(int32 NewUniverseID);

	/** Returns the selected universe */
	int32 GetSelectedUniverse() const;

	/** Called when a fixture patch was selected */
	void OnFixturePatchSelectionChanged();

	/** Called when a universe was selected */
	void OnUniverseSelectionChanged();

	/** Shows the selected universe only */
	void ShowSelectedUniverse(bool bForceReconstructWidget = false);

	/** Shows all universes */
	void ShowAllPatchedUniverses(bool bForceReconstructWidgets = false);

	/** Adds a universe widget */
	void AddUniverse(int32 UniverseID);

	/** Updates the grid depending on entities and selection */
	void OnToggleDisplayAllUniverses(ECheckBoxState CheckboxState);

	/** Returns true if the patcher allows for the selection of a single universe and show only that */
	bool IsUniverseSelectionEnabled() const;

	/** Returns true if the library has any ports */
	bool HasAnyPorts() const;

	/** If true updates selection once during tick and resets */
	int32 UniverseToSetNextTick = INDEX_NONE;

protected:
	/** Returns a tooltip for the whole widget, used to hint why patching is not possible */
	FText GetTooltipText() const;

protected:
	/** Clamps the starting channel to remain within a valid channel range */
	int32 ClampStartingChannel(int32 StartingChannel, int32 ChannelSpan) const;

	/** Helper disabling bAutoAssignAdress in fixture patch transacted */
	void DisableAutoAssignAdress(TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch);

	/** Returns the DMXLibrary or nullptr if not available */
	UDMXLibrary* GetDMXLibrary() const;

	/** Broadcast when a fixture patch was patched */
	FSimpleDelegate OnPatched;

	/** Checkbox to determine if all check boxes shoudl be displayed */
	TSharedPtr<SCheckBox> ShowAllUniversesCheckBox;

	/** Scrollbox containing all patch universes */
	TSharedPtr<SScrollBox> PatchedUniverseScrollBox;

	/** Universe widgets by ID */
	TMap<int32, TSharedPtr<SDMXPatchedUniverse>> PatchedUniversesByID;

	/** Shared data for fixture patch editors */
	TSharedPtr<FDMXFixturePatchSharedData> SharedData;

	/** The owning editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
