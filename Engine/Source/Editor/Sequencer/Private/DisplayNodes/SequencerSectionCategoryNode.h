// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "DisplayNodes/SequencerDisplayNode.h"

/**
 * A node that displays a category for other nodes
 */
class FSequencerSectionCategoryNode
	: public FSequencerDisplayNode
{
public:

	/** The display name of the category */
	FText DisplayName;

	/**
	 * Create and initialize a new instance.
	 * 
	 * @param InNodeName The name identifier of then node.
	 * @param InParentTree The tree this node is in.
	 */
	FSequencerSectionCategoryNode(FName NodeName, FSequencerNodeTree& InParentTree)
		: FSequencerDisplayNode(NodeName, InParentTree)
	{ }

public:

	// FSequencerDisplayNode interface

	virtual bool CanRenameNode() const override;
	virtual TSharedRef<SWidget> GetCustomOutlinerContent() override;
	virtual FText GetDisplayName() const override;
	virtual float GetNodeHeight() const override;
	virtual FNodePadding GetNodePadding() const override;
	virtual ESequencerNode::Type GetType() const override;
	virtual void SetDisplayName(const FText& NewDisplayName) override;

};
