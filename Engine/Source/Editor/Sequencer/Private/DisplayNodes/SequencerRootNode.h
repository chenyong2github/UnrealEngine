// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DisplayNodes/SequencerDisplayNode.h"
#include "Widgets/SNullWidget.h"

/**
 * A single symbolic root node for the whole sequencer tree
 */
class FSequencerRootNode
	: public FSequencerDisplayNode
{
public:

	/**
	 * Create and initialize a new instance.
	 * 
	 * @param InParentTree The tree this node is in.
	 */
	explicit FSequencerRootNode(FSequencerNodeTree& InParentTree)
		: FSequencerDisplayNode(NAME_None, InParentTree)
	{}

public:

	// FSequencerDisplayNode interface

	virtual bool CanRenameNode() const override { return false; }
	virtual FText GetDisplayName() const override { return FText(); }
	virtual float GetNodeHeight() const override { return 0.f; }
	virtual FNodePadding GetNodePadding() const override { return FNodePadding(0.f); }
	virtual ESequencerNode::Type GetType() const override { return ESequencerNode::Root; }
	virtual void SetDisplayName(const FText& NewDisplayName) override { }
	virtual TSharedRef<SWidget> GenerateContainerWidgetForOutliner(const TSharedRef<SSequencerTreeViewRow>& InRow) override { return SNullWidget::NullWidget; }
	virtual bool IsSelectable() const override { return false; }
};
