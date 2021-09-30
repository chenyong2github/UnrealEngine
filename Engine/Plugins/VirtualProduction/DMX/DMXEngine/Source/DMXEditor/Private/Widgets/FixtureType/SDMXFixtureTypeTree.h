// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SDMXEntityTreeViewBase.h"

class FDMXEntityTreeEntityNode;
class SDMXFixtureTypeTreeFixtureTypeRow;


/**
 * A tree of Fixture Types in a DMX Library.
 */
class SDMXFixtureTypeTree
	: public SDMXEntityTreeViewBase
{
public:
	SLATE_BEGIN_ARGS(SDMXFixtureTypeTree)
	{}
		/** The DMX Editor that owns this widget */
		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

		/** Exectued when entites were added to the DMXEditor's library */
		SLATE_EVENT(FSimpleDelegate, OnEntitiesAdded)

		/** Exectued when entites were reorderd in the list, and potentially in the library */
		SLATE_EVENT(FSimpleDelegate, OnEntityOrderChanged)

		/** Exectued when entites were removed from the DMXEditor's library */
		SLATE_EVENT(FSimpleDelegate, OnEntitiesRemoved)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Destructor */
	virtual ~SDMXFixtureTypeTree() {}

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
	/** Called when the selection changed */
	void OnEntitySelected(const TArray<UDMXEntity*>& NewSelection);

	/** Called when the Add button was clicked */
	FReply OnAddNewFixtureTypeClicked();

	/** Returns the row that corresponds to the node */
	TSharedPtr<SDMXFixtureTypeTreeFixtureTypeRow> FindEntityRowByNode(const TSharedRef<FDMXEntityTreeEntityNode>& EntityNode);

	/** Map of entity nodes and their row in the tree */
	TMap<TSharedRef<FDMXEntityTreeEntityNode>, TSharedRef<SDMXFixtureTypeTreeFixtureTypeRow>> EntityNodeToEntityRowMap;
};
