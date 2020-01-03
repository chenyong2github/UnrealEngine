// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Delegates/DelegateCombinations.h"
#include "Framework/Views/ITypedTableView.h"
#include "Types/SlateEnums.h"

#include "ISceneOutliner.h"
#include "SceneOutlinerFwd.h"

// Forward Declarations
namespace SceneOutliner
{
	template<typename TDataType>
	struct TTreeItemGetter;

	struct ITreeItem;

	DECLARE_EVENT_OneParam(SSceneOutliner, FTreeItemPtrEvent, FTreeItemPtr);

	DECLARE_EVENT_TwoParams(SSceneOutliner, FOnItemSelectionChanged, FTreeItemPtr, ESelectInfo::Type);
}


/**
 * The public interface extension for the custom scene outliner
 * Use it to customize the some of the behavior of a scene outliner while keeping it's ux and it's capacity to track the content of a world
 * Note that those functions aren't made to work with a scene outliner that isn't in the custom mode
 */
class ICustomSceneOutliner : public ISceneOutliner
{
public:

	/**
	 * Set the selection mode of the scene outliner.
	 * @param SelectionMode The new selection mode
	 */
	virtual ICustomSceneOutliner& SetSelectionMode(ESelectionMode::Type SelectionMode) = 0;

	/**
	 * Tell the scene outliner to use this visitor before accepting a rename request from a actor or from the prebuild column Item Label 
	 * @param CanRenameItem The visitor that will be used to validate that a item can be renamed (return true to rename)
	 */
	virtual ICustomSceneOutliner& SetCanRenameItem(TUniquePtr<SceneOutliner::TTreeItemGetter<bool>>&& CanRenameItem) = 0;

	/**
	 * Tell the scene outliner to use this visitor to select or not a newly added item
	 * @param ShouldSelectItemWhenAdded The visitor that will be used to select a new item (return true if the item should be selected)
	 */
	virtual ICustomSceneOutliner& SetShouldSelectItemWhenAdded(TUniquePtr<SceneOutliner::TTreeItemGetter<bool>>&& ShouldSelectItemWhenAdded) = 0;

	/**
	 * Set the behavior for when a item is dragged
	 * Note: to avoid having a different user experience from the world outliner. The callback is only called from a left click drag.
	 * @param Callback The function that will be called when a drag from a item row is detected
	 */
	virtual ICustomSceneOutliner& SetOnItemDragDetected(TUniqueFunction<FReply(const SceneOutliner::ITreeItem&)> Callback) = 0;

	/**
	 * Set the behavior for when a drag pass over a Item of the scene outliner
	 * @param Callback The function that will be called at each update when there is a drag over a item
	 */
	virtual ICustomSceneOutliner& SetOnDragOverItem(TUniqueFunction<FReply (const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback) = 0;

	/**
	 * Set the behavior for when a drag and drop is dropped on the scene outliner
	 * @param Callback The function that will be called
	 */
	virtual ICustomSceneOutliner& SetOnDropOnItem(TUniqueFunction<FReply (const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback) = 0;

	/**
	 * Set the behavior for when a drag and drop enter the zone of a item
	 * @param Callback The function that will be called
	 */
	virtual ICustomSceneOutliner& SetOnDragEnterItem(TUniqueFunction<void (const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback) = 0;

	/**
	 * Set the behavior for when a drag and drop leave the zone of a item
	 * @param Callback The function that will be called
	 */
	virtual ICustomSceneOutliner& SetOnDragLeaveItem(TUniqueFunction<void (const FDragDropEvent&, const SceneOutliner::ITreeItem&)> Callback) = 0;

	/**
	 * Tell this scene outliner to use the shared setting or not. 
	 * The shared settings are those used by the world ouliner tab in the level editor
	 * Note: by default it does not use the shared settings
	 */
	virtual ICustomSceneOutliner& SetUseSharedSceneOutlinerSettings(bool bUseSharedSettings) = 0;

	/** Set the hide temporary actors filter */
	virtual ICustomSceneOutliner& SetHideTemporaryActors(bool bHideTemporaryActors) = 0;

	/** Set the show only in current level setting  */
	virtual ICustomSceneOutliner& SetShowOnlyCurrentLevel(bool bShowOnlyCurrentLevel) = 0;

	/** Set the show only selected setting */
	virtual ICustomSceneOutliner& SetShownOnlySelected(bool bShownOnlySelected) = 0;

	/** Set the show actor components setting */
	virtual ICustomSceneOutliner& SetShowActorComponents(bool bShowActorComponents) = 0;

	/**
	 * Is the scene outliner using the shared settings? (The same as the world outliner)
	 * @return True if the scene outliner use the shared settings
	 */
	virtual bool IsUsingSharedSceneOutlinerSettings() const = 0;

	/** @return whether we are hiding temporary Actors */
	virtual bool IsHidingTemporaryActors() const = 0;

	/** @return whether we are showing only Actors that are in the Current Level */
	virtual bool IsShowingOnlyCurrentLevel() const = 0;

	/** @return whether we are displaying only selected Actors */
	virtual bool IsShowingOnlySelected()  const = 0;

	/** @return whether we are showing the components of the Actors */
	virtual bool IsShowingActorComponents() const = 0;

	/** Event to react to a user double click on a item */
	virtual SceneOutliner::FTreeItemPtrEvent& GetDoubleClickEvent() = 0;

	/**
	 * Allow the system that use the scene outliner to react when it's selection is changed
	 * Note: This event will only be broadcast on a user input.
	 */
	virtual SceneOutliner::FOnItemSelectionChanged& GetOnItemSelectionChanged() = 0;

	/**
	 * Set the selection of the scene outliner
	 * The items that return true will be the ones selected
	 * @param ItemSelector A visitor that will be used set the selection.
	 */
	virtual void SetSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemSelector) = 0;

	/**
	 * Add some items to selection of the scene outliner
	 * The items that return true will be the ones added to the selection
	 * @param ItemSelector A visitor that will be used to add some items to the selection.
	 */
	virtual void AddToSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemSelector) = 0;

	/**
	 * Remove some items from selection of the scene outliner
	 * The items that return true will be the ones removed from the selection
	 * @param ItemSelector A visitor that will be used to remove some items from the selection.
	 */
	virtual void RemoveFromSelection(const SceneOutliner::TTreeItemGetter<bool>& ItemDeselector) = 0;

	/**
	 * Add a object to the selection of the scene outliner
	 * @param Object The Object that will be added to the selection
	 */
	virtual void AddObjectToSelection(const UObject* Object) = 0;

	/**
	 * Remove a object from the selection of the scene outliner
	 * @param Object The Object that will be removed from the selection
	 */
	virtual void RemoveObjectFromSelection(const UObject* Object) = 0;

	/**
	 * Add a folder to the selection of the scene outliner
	 * @param FolderName The name of the folder to add to selection
	 */
	virtual void AddFolderToSelection(const FName& FolderName) = 0;

	/**
	 * Remove a folder from the selection of the scene outliner
	 * @param FolderName The name of the folder to remove from the selection
	 */
	virtual void RemoveFolderFromSelection(const FName& FolderName) = 0;

	/** Deselect all selected items */
	virtual void ClearSelection() = 0;

};
