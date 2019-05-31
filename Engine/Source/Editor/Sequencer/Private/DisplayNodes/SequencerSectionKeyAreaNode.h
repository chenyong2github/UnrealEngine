// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "DisplayNodes/SequencerDisplayNode.h"

class IKeyArea;
class SKeyAreaEditorSwitcher;
class UMovieSceneSection;

/**
 * Represents an area inside a section where keys are displayed.
 *
 * There is one key area per section that defines that key area.
 */
class FSequencerSectionKeyAreaNode
	: public FSequencerDisplayNode
{
public:

	/** The display name of the key area. */
	FText DisplayName;

	/**
	 * Create and initialize a new instance.
	 * 
	 * @param InNodeName The name identifier of then node.
	 * @param InParentTree The tree this node is in.
	 */
	FSequencerSectionKeyAreaNode(FName NodeName, FSequencerNodeTree& InParentTree);

public:

	/**
	 * Adds a key area to this node.
	 *
	 * @param KeyArea The key area interface to add.
	 */
	void AddKeyArea(TSharedRef<IKeyArea> KeyArea);

	/**
	 * Returns a key area that corresponds to the specified section
	 * 
	 * @param Section	The section to find a key area for
	 * @return the key area at the index.
	 */
	TSharedPtr<IKeyArea> GetKeyArea(UMovieSceneSection* Section) const;

	/**
	 * Returns all key area for this node
	 * 
	 * @return All key areas
	 */
	const TArray<TSharedRef<IKeyArea>>& GetAllKeyAreas() const
	{
		return KeyAreas;
	}

	void ClearKeyAreas();

	/** Retrieve the key area editor switcher widget, creating it if it doesn't yet exist */
	TSharedRef<SWidget> GetOrCreateKeyAreaEditorSwitcher();

public:

	// FSequencerDisplayNode interface

	virtual bool CanRenameNode() const override;
	virtual TSharedRef<SWidget> GetCustomOutlinerContent() override;
	virtual FText GetDisplayName() const override;
	virtual float GetNodeHeight() const override;
	virtual FNodePadding GetNodePadding() const override;
	virtual ESequencerNode::Type GetType() const override;
	virtual void SetDisplayName(const FText& NewDisplayName) override;

	// ICurveEditorTreeItem interface
	virtual void CreateCurveModels(TArray<TUniquePtr<FCurveModel>>& OutCurveModels) override;

private:

	/** All key areas on this node (one per section). */
	TArray<TSharedRef<IKeyArea>> KeyAreas;

	/** The outliner key editor switcher widget. */
	TSharedPtr<SKeyAreaEditorSwitcher> KeyEditorSwitcher;
};
