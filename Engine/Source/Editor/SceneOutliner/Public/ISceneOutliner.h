// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Attribute.h"
#include "SceneOutlinerFwd.h"
#include "Widgets/SCompoundWidget.h"

// Forward declaration
template<typename ItemType> class STreeView;
namespace SceneOutliner { struct FColumnInfo; }

/**
 * The public interface for the Scene Outliner widget
 */
class ISceneOutliner : public SCompoundWidget
{
public:

	/** Sends a requests to the Scene Outliner to refresh itself the next chance it gets */
	virtual void Refresh() = 0;

	/** Tells the scene outliner that it should do a full refresh, which will clear the entire tree and rebuild it from scratch. */
	virtual void FullRefresh() = 0;

	/** @return Returns a string to use for highlighting results in the outliner list */
	virtual TAttribute<FText> GetFilterHighlightText() const = 0;

	/** @return Returns the common data for this outliner */
	virtual const SceneOutliner::FSharedOutlinerData& GetSharedData() const = 0;

	/** Get a const reference to the actual tree hierarchy */
	virtual const STreeView<SceneOutliner::FTreeItemPtr>& GetTree() const = 0;

	/** Set the keyboard focus to the outliner */
	virtual void SetKeyboardFocus() = 0;

	/** Gets the cached icon for this class name */
	virtual const FSlateBrush* GetCachedIconForClass(FName InClassName) const = 0;

	/** Sets the cached icon for this class name */
	virtual void CacheIconForClass(FName InClassName, const FSlateBrush* InSlateBrush) = 0;

	/** Should the scene outliner accept a request to rename a item of the tree */
	virtual bool CanExecuteRenameRequest(const SceneOutliner::FTreeItemPtr& ItemPtr) const = 0;

	/** 
	 * Add a filter to the scene outliner 
	 * @param Filter The filter to apply to the scene outliner
	 * @return The index of the filter.
	 */
	virtual int32 AddFilter(const TSharedRef<SceneOutliner::FOutlinerFilter>& Filter) = 0;

	/** 
	 * Remove a filter from the scene outliner
	 * @param Filter The Filter to remove
	 * @return True if the filter was removed.
	 */
	virtual bool RemoveFilter(const TSharedRef<SceneOutliner::FOutlinerFilter>& Filter) = 0;

	/** 
	 * Retrieve the filter at the specified index
	 * @param Index The index of the filter to retrive
	 * @return A valid poiter to a filter if the index was valid
	 */
	virtual TSharedPtr<SceneOutliner::FOutlinerFilter> GetFilterAtIndex(int32 Index) = 0;

	/** Get number of filters applied to the scene outliner */
	virtual int32 GetFilterCount() const = 0;

	/**
	 * Add or replace a column of the scene outliner
	 * Note: The column id must match the id of the column returned by the factory
	 * @param ColumnId The id of the column to add
	 * @param ColumInfo The struct that contains the information on how to present and retrieve the column
	 */
	virtual void AddColumn(FName ColumnId, const SceneOutliner::FColumnInfo& ColumnInfo) = 0;

	/**
	 * Remove a column of the scene outliner
	 * @param ColumnId The name of the column to remove
	 */
	virtual void RemoveColumn(FName ColumnId) = 0;

	/** Return the name/Id of the columns of the scene outliner */
	virtual TArray<FName> GetColumnIds() const = 0;

	/** Returns true if edit delete can be executed */
	virtual bool Delete_CanExecute() = 0;

	/** Returns true if edit rename can be executed */
	virtual bool Rename_CanExecute() = 0;

	/** Executes rename. */
	virtual void Rename_Execute() = 0;

	/** Returns true if edit cut can be executed */
	virtual bool Cut_CanExecute() = 0;

	/** Returns true if edit copy can be executed */
	virtual bool Copy_CanExecute() = 0;

	/** Returns true if edit paste can be executed */
	virtual bool Paste_CanExecute() = 0;
};
