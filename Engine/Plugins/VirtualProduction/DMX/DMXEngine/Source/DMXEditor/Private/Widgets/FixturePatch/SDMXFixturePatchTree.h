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

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Destructor */
	virtual ~SDMXFixturePatchTree() {};

protected:
	//~ Begin SDMXEntityTreeViewBase interface
	virtual TSharedRef<SWidget> GenerateAddNewEntityButton();
	virtual void RebuildNodes(const TSharedPtr<FDMXEntityTreeRootNode>& InRootNode);
	virtual TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FDMXEntityTreeNodeBase> Node, const TSharedRef<STableViewBase>& OwnerTable);
	virtual void OnExpansionChanged(TSharedPtr<FDMXEntityTreeNodeBase> Node, bool bInExpansionState) override;
	virtual TSharedPtr<SWidget> OnContextMenuOpen();
	virtual void OnSelectionChanged(TSharedPtr<FDMXEntityTreeNodeBase> InSelectedNode, ESelectInfo::Type SelectInfo);
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
	/** Creates a Node for the entity */
	TSharedPtr<FDMXEntityTreeEntityNode> CreateEntityNode(UDMXEntity* Entity);

	/** Returns the row that corresponds to the node */
	TSharedPtr<SDMXFixturePatchTreeFixturePatchRow> FindEntityRowByNode(const TSharedRef<FDMXEntityTreeEntityNode>& EntityNode) const;

	/** Returns the category node that corresponds to the Universe ID */
	TSharedPtr<FDMXEntityTreeCategoryNode> FindCategoryNodeByUniverseID(int32 UniverseID, TSharedPtr<FDMXEntityTreeNodeBase> StartNode = nullptr) const;

	/** Called when Entities in the DMX Library were added or removed */
	void OnEntitiesAddedOrRemoved(UDMXLibrary* DMXLibrary, TArray<UDMXEntity*> Entities);

	/** Called when a Fixture Patch changed */
	void OnFixturePatchChanged(const UDMXEntityFixturePatch* FixturePatch);

	/** Called when a Fixture Type changed */
	void OnFixtureTypeChanged(const UDMXEntityFixtureType* FixtureType);

	/** Called when Fixture Patches were selected in Fixture Patch Shared Data */
	void OnFixturePatchesSelected();

	/** Called when a Universe was selected in Fixture Patch Shared Data */
	void OnUniverseSelected();

	/** Auto assigns a patch */
	void AutoAssignCopiedPatch(UDMXEntityFixturePatch* Patch) const;

	/** Returns error message or FText::GetEmpty() if no error found */
	FText CheckForPatchError(UDMXEntityFixturePatch* FixturePatch) const;

	/** Called when the user selects a Fixture Type to create a Fixture Patch from */
	void OnAddNewFixturePatchClicked(UDMXEntity* InSelectedFixtureType);

	/** Called when Auto Assign Channel is changed for a Patch */
	void OnAutoAssignChannelStateChanged(bool NewState, TSharedPtr<FDMXEntityTreeEntityNode> InNode);

	/** True while the widget is changing the selection */
	bool bChangingFixturePatchSelection = false;

	/** The auto expanded universe category node */
	TSharedPtr<FDMXEntityTreeCategoryNode> AutoExpandedUniverseCategoryNode;

	/** The user expanded universe category nodes */
	TArray<TSharedRef<FDMXEntityTreeCategoryNode>> UserExpandedUniverseCategoryNodes;

	/** Map of entity nodes and their row in the tree */
	TMap<TSharedRef<FDMXEntityTreeEntityNode>, TSharedRef<SDMXFixturePatchTreeFixturePatchRow>> EntityNodeToEntityRowMap;

	/** A dropdown menu that shows fixture types from which patches can be created */
	TSharedPtr<SDMXEntityDropdownMenu<UDMXEntityFixtureType>> AddButtonDropdownList;

	/** Shared data for fixture patch editors */
	TSharedPtr<FDMXFixturePatchSharedData> FixturePatchSharedData;
};
