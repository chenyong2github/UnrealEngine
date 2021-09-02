// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SDMXEntityTreeViewBase.h"

#include "CoreMinimal.h"

class FDMXEntityTreeEntityNode;
class FDMXFixturePatchSharedData;
template<typename TEntityType> class SDMXEntityDropdownMenu;
class SDMXFixturePatchTreeFixturePatchRow;
class UDMXEntityFixturePatch;
class UDMXEntityFixtureType;


/** 
 * A tree of Fixture Patches in a DMX Library. 
 */
class SDMXFixturePatchTree
	: public SDMXEntityTreeViewBase
{
	DECLARE_DELEGATE_OneParam(FDMXOnAutoAssignAddressChanged, TArray<UDMXEntityFixturePatch*>);

public:
	SLATE_BEGIN_ARGS(SDMXFixturePatchTree)
	{}
		/** The DMX Editor that owns this widget */
		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

		/** Exectued when the list changed its selection */
		SLATE_EVENT(FDMXOnSelectionChanged, OnSelectionChanged)

		/** Exectued when entites were added to the DMXEditor's library */
		SLATE_EVENT(FSimpleDelegate, OnEntitiesAdded)

		/** Exectued when entites were reorderd in the list, and potentially in the library */
		SLATE_EVENT(FSimpleDelegate, OnEntityOrderChanged)

		/** Exectued when entites were removed from the DMXEditor's library */
		SLATE_EVENT(FSimpleDelegate, OnEntitiesRemoved)

		/** Exectued when the auto assign address of fixture patches changed */
		SLATE_EVENT(FDMXOnAutoAssignAddressChanged, OnAutoAssignAddressChanged)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Destructor */
	virtual ~SDMXFixturePatchTree() {};

	/** Creates a Node for the entity */
	TSharedPtr<FDMXEntityTreeEntityNode> CreateEntityNode(UDMXEntity* Entity);

protected:
	//~ Begin SDMXEntityTreeViewBase interface
	virtual TSharedRef<SWidget> GenerateAddNewEntityButton();
	virtual void RebuildNodes(const TSharedPtr<FDMXEntityTreeRootNode>& InRootNode);
	virtual TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FDMXEntityTreeNodeBase> Node, const TSharedRef<STableViewBase>& OwnerTable);
	virtual TSharedPtr<SWidget> OnContextMenuOpen() override;
	virtual void OnCutSelectedNodes();
	virtual bool CanCutNodes() const;
	virtual void OnCopySelectedNodes();
	virtual bool CanCopyNodes() const;
	virtual void OnPasteNodes();
	virtual bool CanPasteNodes() const;
	virtual bool CanDuplicateNodes() const;
	virtual void OnDuplicateNodes();
	virtual void OnDeleteNodes();
	virtual bool CanDeleteNodes() const;
	virtual void OnRenameNode();
	virtual bool CanRenameNode() const;
	//~ End SDMXEntityTreeViewBase interface

private:
	/** Returns the row that corresponds to the node */
	TSharedPtr<SDMXFixturePatchTreeFixturePatchRow> FindEntityRowByNode(const TSharedRef<FDMXEntityTreeEntityNode>& EntityNode);

	/** Auto assigns a patch */
	void AutoAssignCopiedPatch(UDMXEntityFixturePatch* Patch) const;

	/** Called when fixture patches were selected in shared data */
	void OnSharedDataSelectedFixturePatches();

	/** Returns error message or FText::GetEmpty() if no error found */
	FText CheckForPatchError(UDMXEntityFixturePatch* FixturePatch) const;

	/** Called when the user selects a Fixture Type to create a Fixture Patch from */
	void OnFixtureTypeSelected(UDMXEntity* InSelectedFixtureType);

	/** Called by the editor to set a base name for an Entity about to be created */
	void OnEditorGetBaseNameForNewFixturePatch(TSubclassOf<UDMXEntity> InEntityClass, FString& OutBaseName, UDMXEntityFixtureType* InSelectedFixtureType);

	/** Called by the editor to setup the properties of a new Entity before its selection and renaming in the editor */
	void OnEditorSetupNewFixturePatch(UDMXEntity* InNewEntity, UDMXEntityFixtureType* InSelectedFixtureType);

	/** Called when Auto Assign Channel is changed for a Patch */
	void OnAutoAssignChannelStateChanged(bool NewState, TSharedPtr<FDMXEntityTreeEntityNode> InNodePtr);

	/** Map of entity nodes and their row in the tree */
	TMap<TSharedRef<FDMXEntityTreeEntityNode>, TSharedRef<SDMXFixturePatchTreeFixturePatchRow>> EntityNodeToEntityRowMap;

	/** A dropdown menu that shows fixture types from which patches can be created */
	TSharedPtr<SDMXEntityDropdownMenu<UDMXEntityFixtureType>> AddButtonDropdownList;

	/** Delegate to invoke when a fixture patch changed the auto assign address property */
	FDMXOnAutoAssignAddressChanged OnAutoAssignAddressChanged;

	/** Delegate broadcast when a new entity is setup */
	FDelegateHandle OnSetupNewEntityHandle;

	/** Handle to the registered OnActiveTabChanged delegate */
	FDelegateHandle OnActiveTabChangedDelegateHandle;

	/** Handle to the base name for a new entity requested delegate */
	FDelegateHandle OnGetBaseNameForNewEntityHandle;

	/** Shared data for fixture patch editors */
	TSharedPtr<FDMXFixturePatchSharedData> FixturePatchSharedData;
};
