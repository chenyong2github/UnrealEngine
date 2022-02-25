// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IRemoteControlUIModule.h"
#include "Templates/SharedPointer.h"
#include "Containers/Array.h"
#include "Misc/Guid.h"
#include "Widgets/SCompoundWidget.h"

class SWidget;
class SRCPanelGroup;
struct SRCPanelExposedField;
struct SRCPanelExposedActor;
struct SRCPanelExposedMaterial;

/** A node in the panel tree view. */
struct SRCPanelTreeNode : public SCompoundWidget
{
	enum ENodeType
	{
		Invalid,
		Group,
		Field,
		FieldChild,
		Actor,
		Material
	};

	virtual ~SRCPanelTreeNode() {}

	/** Get this tree node's childen. */
	virtual void GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const {}
	/** Get this node's ID if any. */
	virtual FGuid GetRCId() const { return FGuid(); }
	/** Get get this node's type. */
	virtual ENodeType GetRCType() const { return ENodeType::Invalid; };
	/** Refresh the node. */
	virtual void Refresh() {};
	/** Get the context menu for this node. */
	virtual TSharedPtr<SWidget> GetContextMenu() { return nullptr; }
	/** Set whether this widget is currently hovered when drag and dropping. */
	virtual void SetIsHovered(bool bIsBeingHovered) {}

protected:
	struct FMakeNodeWidgetArgs
	{
		TSharedPtr<SWidget> DragHandle;
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> RenameButton;
		TSharedPtr<SWidget> ValueWidget;
		TSharedPtr<SWidget> UnexposeButton;
	};
	
	/** Create a widget that represents a row with a splitter. */
	TSharedRef<SWidget> MakeSplitRow(TSharedRef<SWidget> LeftColumn, TSharedRef<SWidget> RightColumn);
	/** Create a widget that represents a node in the panel tree hierarchy. */
	TSharedRef<SWidget> MakeNodeWidget(const FMakeNodeWidgetArgs& Args);

private:
	/** Stub handler for column resize callback to prevent the splitter from handling it internally.  */
	void OnLeftColumnResized(float) const;

	//~ Wrappers around ColumnSizeData's delegate needed in order to offset the splitter for RC Groups. 
	float GetLeftColumnWidth() const;
	float GetRightColumnWidth() const;
	void SetColumnWidth(float InWidth);

protected:
	/** Holds the row's columns' width. */
	FRCColumnSizeData ColumnSizeData;

private:
	/** The splitter offset to align the group splitter with the other row's splitters. */
	static constexpr float SplitterOffset = 0.008f;
};
