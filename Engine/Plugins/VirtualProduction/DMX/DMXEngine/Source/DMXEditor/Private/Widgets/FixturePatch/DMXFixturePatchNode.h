// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

class FDMXEditor;
class SDMXFixturePatchFragment;
class SDMXPatchedUniverse;
class UDMXEntityFixturePatch;

class SGridPanel;
struct EVisibility;

/** A fixture patch fragment in a grid. CreateFragments creates DMXFixturePatchFragment from a DMXFixturePatchNode. */
class FDMXFixturePatchFragment
	: public TSharedFromThis<FDMXFixturePatchFragment>
{
public:
	/** Creates fragments of a patch spread across a grid */
	static void CreateFragments(TWeakObjectPtr<UDMXEntityFixturePatch> InFixturePatch, int32 ChannelID, TArray<TSharedPtr<FDMXFixturePatchFragment>>& OutFragments);

	/** Gets all fragments of a patch */
	TArray<TSharedPtr<FDMXFixturePatchFragment>> GetFragments();

	bool IsHead() const { return !LhsFragment.IsValid(); }
	bool IsTail() const { return !RhsFragment.IsValid(); }

	int32 ID;
	int32 Row;
	int32 Column;
	int32 ColumnSpan;

	TSharedPtr<FDMXFixturePatchFragment> LhsFragment;
	TSharedPtr<FDMXFixturePatchFragment> RhsFragment;

	TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch;
};

////////////////////////////////////////////////////////////////////////////////

/** 
 * A fixture patch in a grid, consists of several fragments, displayed as SDMXFixturePatchNodes. 
 * The patch node does not necessarily have to present the state of the object, e.g. when
 * dragged or when containing an occuppied universe/channel. 
 */
class FDMXFixturePatchNode
	: public TSharedFromThis<FDMXFixturePatchNode>
{
public:
	/** Creates a new node from patch */
	static TSharedPtr<FDMXFixturePatchNode> Create(TWeakPtr<FDMXEditor> InDMXEditor, TWeakObjectPtr<UDMXEntityFixturePatch> InFixturePatch);
	
	/** Updates the patch node. NewUniverse can be nullptr */
	void Update(TSharedPtr<SDMXPatchedUniverse> NewUniverse, int32 NewStartingChannel, int32 NewChannelSpan);

	/** Commits the patch to the object */
	void CommitPatch(bool bTransacted);

	/** Returns wether the patch uses specified channesl */
	bool OccupiesChannels(int32 Channel, int32 Span) const;

	/** Returns true if the node is patched in a universe */
	bool IsPatched() const;

	/** Returns true if the node is selected */
	bool IsSelected() const;

	/** Returns the fixture patch this node connects */
	TWeakObjectPtr<UDMXEntityFixturePatch> GetFixturePatch() const { return FixturePatch; }

	/** Returns the universe this node resides in */
	const TSharedPtr<SDMXPatchedUniverse>& GetUniverse() const { return Universe; }

	/** Returns fragmented widgets to visualize the node in a grid */
	const TArray<TSharedPtr<SDMXFixturePatchFragment>>& GetFragmentedWidgets() const { return FragmentedWidgets; }

private:
	/** Called when a patch node was selected */
	void OnSelectionChanged();

	/** Universe the patch is assigned to */
	TSharedPtr<SDMXPatchedUniverse> Universe;

	/** Starting channel of the patch */
	int32 StartingChannel;

	/** Channel span of the patch */
	int32 ChannelSpan;

	/** Last transacted Universe ID, required for propert undo/redo */
	int32 LastTransactedUniverseID;

	/** Last transacted Channel ID, required for propert undo/redo */
	int32 LastTransactedChannelID;

	/** If true the node is selected */
	bool bSelected = false;

	/** The widget fragments that visualize the patch */
	TArray<TSharedPtr<SDMXFixturePatchFragment>> FragmentedWidgets;

	/** Weak refrence of the actual patch in the library */
	TWeakObjectPtr<UDMXEntityFixturePatch> FixturePatch;

	/** Weak DMXEditor refrence */
	TWeakPtr<FDMXEditor> DMXEditor;
};
