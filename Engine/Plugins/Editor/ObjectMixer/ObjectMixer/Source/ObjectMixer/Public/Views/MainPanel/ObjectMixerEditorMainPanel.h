// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectFilter/ObjectMixerEditorObjectFilter.h"

#include "ObjectMixerEditorModule.h"

#include "Folder.h"
#include "Widgets/SWidget.h"

class FObjectMixerEditorList;
class FObjectMixerEditorListFilter_Collection;
class FUICommandList;
class IObjectMixerEditorListFilter;
class SObjectMixerEditorMainPanel;
class UObjectMixerEditorSerializedData;
class UObjectMixerObjectFilter;

struct FObjectMixerEditorListRow;

DECLARE_MULTICAST_DELEGATE(FOnPreFilterChange)
DECLARE_MULTICAST_DELEGATE(FOnPostFilterChange)

class OBJECTMIXEREDITOR_API FObjectMixerEditorMainPanel : public TSharedFromThis<FObjectMixerEditorMainPanel>, public FGCObject
{
public:
	FObjectMixerEditorMainPanel(const FName InModuleName)
	: ModuleName(InModuleName)
	{}

	virtual ~FObjectMixerEditorMainPanel() override = default;

	void Initialize();
	
	void RegisterAndMapContextMenuCommands();

	TSharedRef<SWidget> GetOrCreateWidget();

	void RegenerateListModel();

	/**
	 * Regenerate the list items and refresh the list. Call when adding or removing items.
	 */
	void RequestRebuildList() const;

	/**
	 * Refresh list filters and sorting.
	 * Useful for when the list state has gone stale but the variable count has not changed.
	 */
	void RefreshList() const;

	/** Called when the Rename command is executed from the UI or hotkey. */
	void OnRenameCommand();

	void OnRequestNewFolder(TOptional<FFolder> ExplicitParentFolder = TOptional<FFolder>());

	void OnRequestMoveFolder(const FFolder& FolderToMove, const FFolder& TargetNewParentFolder);

	void RequestSyncEditorSelectionToListSelection() const;

	TWeakPtr<FObjectMixerEditorList> GetEditorListModel() const
	{
		return EditorListModel;
	}

	void RebuildCollectionSelector();

	FText GetSearchTextFromSearchInputField() const;
	FString GetSearchStringFromSearchInputField() const;

	void SetDefaultFilterClass(UClass* InNewClass);
	bool IsClassSelected(UClass* InClass) const;

	const TArray<TObjectPtr<UObjectMixerObjectFilter>>& GetObjectFilterInstances();

	const UObjectMixerObjectFilter* GetMainObjectFilterInstance();

	void CacheObjectFilterObjects();

	/**
	 * Get the style of the tree (flat list or hierarchy)
	 */
	EObjectMixerTreeViewMode GetTreeViewMode()
	{
		return TreeViewMode;
	}
	/**
	 * Set the style of the tree (flat list or hierarchy)
	 */
	void SetTreeViewMode(EObjectMixerTreeViewMode InViewMode)
	{
		TreeViewMode = InViewMode;
		RequestRebuildList();
	}

	/**
	 * Returns result from Filter->GetObjectClassesToFilter.
	 */
	TSet<UClass*> GetObjectClassesToFilter();

	/**
	 * Returns result from Filter->GetObjectClassesToPlace.
	 */
	TSet<TSubclassOf<AActor>> GetObjectClassesToPlace();

	const TArray<TSharedRef<IObjectMixerEditorListFilter>>& GetListFilters() const;
	TArray<TWeakPtr<IObjectMixerEditorListFilter>> GetWeakActiveListFiltersSortedByName() const;

	const TArray<TSubclassOf<UObjectMixerObjectFilter>>& GetObjectFilterClasses() const
	{
		return ObjectFilterClasses;
	}

	void AddObjectFilterClass(UClass* InObjectFilterClass, const bool bCacheAndRebuild = true);

	void RemoveObjectFilterClass(UClass* InObjectFilterClass, const bool bCacheAndRebuild = true);

	void ResetObjectFilterClasses(const bool bCacheAndRebuild = true)
	{
		ObjectFilterClasses.Empty(ObjectFilterClasses.Num());

		if (bCacheAndRebuild)
		{
			CacheAndRebuildFilters();
		}
	}

	void CacheAndRebuildFilters()
	{
		CacheObjectFilterObjects();
		RequestRebuildList();
	}

	FName GetModuleName() const
	{
		return ModuleName;
	}

	// User Collections

	/**
	 * Get a pointer to the UObjectMixerEditorSerializedData object along with the name of the filter represented by this MainPanel instance.
	 */
	UObjectMixerEditorSerializedData* GetSerializedData() const;
	bool RequestAddObjectsToCollection(const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToAdd) const;
	bool RequestRemoveObjectsFromCollection(const FName& CollectionName, const TSet<FSoftObjectPath>& ObjectsToRemove) const;
	bool RequestRemoveCollection(const FName& CollectionName) const;
	bool RequestDuplicateCollection(const FName& CollectionToDuplicateName, FName& DesiredDuplicateName) const;
	bool RequestReorderCollection(const FName& CollectionToMoveName, const FName& CollectionInsertBeforeName) const;
	bool RequestRenameCollection(const FName& CollectionNameToRename, const FName& NewCollectionName) const;
	bool DoesCollectionExist(const FName& CollectionName) const;
	bool IsObjectInCollection(const FName& CollectionName, const FSoftObjectPath& InObject) const;
	TSet<FName> GetCollectionsForObject(const FSoftObjectPath& InObject) const;
	TArray<FName> GetAllCollectionNames() const;

	/**
	 * Returns the collections selected by the user. If the set is empty, consider "All" collections to be selected.
	 */
	TSet<TSharedRef<FObjectMixerEditorListFilter_Collection>> GetCurrentCollectionSelection() const;

	/**
	 * This is the filter class used to initialize the MainPanel.
	 * This filter class cannot be turned off by the end user.
	 */
	const TSubclassOf<UObjectMixerObjectFilter>& GetDefaultFilterClass() const;

	FOnPreFilterChange OnPreFilterChange;
	FOnPostFilterChange OnPostFilterChange;

	TSharedPtr<FUICommandList> ObjectMixerElementEditCommands;
	TSharedPtr<FUICommandList> ObjectMixerFolderEditCommands;

protected:

	virtual void AddReferencedObjects( FReferenceCollector& Collector )  override
	{
		Collector.AddReferencedObjects(ObjectFilterInstances);
	}
	virtual FString GetReferencerName() const override
	{
		return TEXT("FObjectMixerEditorMainPanel");
	}

private:

	TSharedPtr<SObjectMixerEditorMainPanel> MainPanelWidget;

	TSharedPtr<FObjectMixerEditorList> EditorListModel;

	TArray<TObjectPtr<UObjectMixerObjectFilter>> ObjectFilterInstances;

	/**
	 * The classes used to generate property edit columns
	 */
	TArray<TSubclassOf<UObjectMixerObjectFilter>> ObjectFilterClasses;

	/**
	 * The FObjectMixerEditorModule subclass that created this instance.
	 */
	TWeakPtr<FObjectMixerEditorModule> SpawningModulePtr;

	/**
	 * If set, this is the filter class used to initialize the MainPanel.
	 * This filter class cannot be turned off by the end user.
	 */
	TSubclassOf<UObjectMixerObjectFilter> DefaultFilterClass;

	/**
	 * Determines the style of the tree (flat list or hierarchy)
	 */
	EObjectMixerTreeViewMode TreeViewMode = EObjectMixerTreeViewMode::Folders;

	FName ModuleName = NAME_None;
};
