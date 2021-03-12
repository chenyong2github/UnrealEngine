// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SDMXChannelConnector.h"
#include "Library/DMXEntityFixtureType.h"

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class FDMXEditor;
class FDMXFixturePatchNode;
class FDMXFixturePatchSharedData;
class SDMXChannelConnector;
class UDMXLibrary;
class UDMXEntityFixturePatch;

class SBorder;
class SGridPanel;


enum class EDMXPatchedUniverseReachability
{
	Reachable,
	UnreachableForInputPorts,
	UnreachableForOutputPorts,
	UnreachableForInputAndOutputPorts
};


/** A universe with assigned patches */
class SDMXPatchedUniverse
	: public SCompoundWidget
{
	DECLARE_DELEGATE_ThreeParams(FOnDragOverChannel, int32 /** UniverseID */, int32 /** ChannelID */, const FDragDropEvent&);

	DECLARE_DELEGATE_RetVal_ThreeParams(FReply, FOnDropOntoChannel, int32 /** UniverseID */, int32 /** ChannelID */, const FDragDropEvent&);

public:
	SDMXPatchedUniverse()
		: PatchedUniverseReachability(EDMXPatchedUniverseReachability::UnreachableForInputAndOutputPorts)
	{}

	SLATE_BEGIN_ARGS(SDMXPatchedUniverse)
		: _UniverseID(0)
		, _DMXEditor(nullptr)
		, _OnDragEnterChannel()
		, _OnDragLeaveChannel()
		, _OnDropOntoChannel()
	{}
		SLATE_ARGUMENT(int32, UniverseID)

		SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)

		/** Called when drag enters a channel in this universe */
		SLATE_EVENT(FOnDragOverChannel, OnDragEnterChannel)

		/** Called when drag leaves a channel in this universe */
		SLATE_EVENT(FOnDragOverChannel, OnDragLeaveChannel)

		/** Called when dropped onto a channel in this universe */
		SLATE_EVENT(FOnDropOntoChannel, OnDropOntoChannel)

	SLATE_END_ARGS()

	/** Constructs this widget */
	void Construct(const FArguments& InArgs);
	
	/** 
	 * Shows specified Universe ID in the widget. 
	 * If the same Universe ID is set anew, refreshes the widget. 
	 */
	void SetUniverseID(int32 NewUniverseID);

	/** 
	 * Patches the node.
	 * Patches that have bAutoAssignAddress use their auto assigned address.
	 * Others are assigned to the specified new starting channel
	 * Returns false if the patch cannot be patched.
	 */
	bool Patch(const TSharedPtr<FDMXFixturePatchNode>& Node, int32 NewStartingChannel, bool bCreateTransaction);

protected:
	/** Removes the node. Should be called when the node is Patched in another instance */
	void Unpatch(const TSharedPtr<FDMXFixturePatchNode>& Node);

	/** Adds the node to the grid */
	void AddNodeToGrid(const TSharedPtr<FDMXFixturePatchNode>& Node);

	/** Removes the node from the grid */
	void RemoveNodeFromGrid(const TSharedPtr<FDMXFixturePatchNode>& Node);

public:
	/** If set to true, shows a universe name above the patcher universe */
	void SetShowUniverseName(bool bShow);

	/** Returns the grid of the universe */
	TSharedPtr<SGridPanel> GetGrid() const { return Grid; }

	/** Returns wether the patch can be patched to its current channels */
	bool CanAssignFixturePatch(TWeakObjectPtr<UDMXEntityFixturePatch> TestedPatch) const;

	/** Returns wether the patch can be patched to specified channel */
	bool CanAssignFixturePatch(TWeakObjectPtr<UDMXEntityFixturePatch> TestedPatch, int32 StartingChannel) const;

	/** Returns wether the node can be patched to specified channel */
	bool CanAssignNode(const TSharedPtr<FDMXFixturePatchNode>& TestedNode, int32 StartingChannel) const;

	/** Returns if the node is patched in the unvierse */
	TSharedPtr<FDMXFixturePatchNode> FindPatchNode(const TWeakObjectPtr<UDMXEntityFixturePatch>& FixturePatch) const;

	/** Returns first node with same fixture type as specified node */	
	TSharedPtr<FDMXFixturePatchNode> FindPatchNodeOfType(UDMXEntityFixtureType* Type, const TSharedPtr<FDMXFixturePatchNode>& IgoredNode) const;

	/** Returns the ID of the universe */
	int32 GetUniverseID() const { return UniverseID; }

	/** Gets all nodes patched to this universe */
	const TArray<TSharedPtr<FDMXFixturePatchNode>>& GetPatchedNodes() const { return PatchedNodes; }

protected:
	/** Creates a a new grid of channels */
	void CreateChannelConnectors();

	/** Returns the name of the universe displayed */
	FText GetHeaderText() const;
	
protected:
	/** Called when drag enters a channel */
	void HandleDragEnterChannel(int32 ChannelID, const FDragDropEvent& DragDropEvent);

	/** Called when drag leaves a channel */
	void HandleDragLeaveChannel(int32 ChannelID, const FDragDropEvent& DragDropEvent);

	/** Called when drag dropped onto a channel */
	FReply HandleDropOntoChannel(int32 ChannelID, const FDragDropEvent& DragDropEvent);

	// Drag drop events
	FOnDragOverChannel OnDragEnterChannel;
	FOnDragOverChannel OnDragLeaveChannel;
	FOnDropOntoChannel OnDropOntoChannel;

protected:
	/** Returns wether the out of controllers' ranges banner should be visible */
	EVisibility GetPatchedUniverseReachabilityBannerVisibility() const;

	/** Updates bOutOfControllersRanges member */
	void UpdatePatchedUniverseReachability();

	/** Returns the DMXLibrary or nullptr if not available */
	UDMXLibrary* GetDMXLibrary() const;

	/** The universe being displayed */
	int32 UniverseID;

	/** If true the universe ID is out of controllers' ranges */
	EDMXPatchedUniverseReachability PatchedUniverseReachability;
	
	/** Widget showing the Name of the Universe */
	TSharedPtr<SBorder> UniverseName; 

	/** Grid laying out available channels */
	TSharedPtr<SGridPanel> Grid;

	/** Patches in the grid */
	TArray<TSharedPtr<FDMXFixturePatchNode>> PatchedNodes;

	/** The Channel connectors in this universe */
	TArray<TSharedPtr<SDMXChannelConnector>> ChannelConnectors;

	/** Shared data for fixture patch editors */
	TSharedPtr<FDMXFixturePatchSharedData> SharedData;

	/** The owning editor */
	TWeakPtr<FDMXEditor> DMXEditorPtr;
};
