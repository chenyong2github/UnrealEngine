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

namespace FRemoteControlPresetColumns
{
	static FName DragDropHandle = TEXT("DragDropHandle");
	static FName Description = TEXT("Description");
	static FName Value = TEXT("Value");
	static FName Reset = TEXT("Reset");
}

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
	/** Returns true if this tree node has childen. */
	virtual bool HasChildren() const { return false; }
	/** Refresh the node. */
	virtual void Refresh() {};
	/** Get the context menu for this node. */
	virtual TSharedPtr<SWidget> GetContextMenu() { return nullptr; }
	/** Set whether this widget is currently hovered when drag and dropping. */
	virtual void SetIsHovered(bool bIsBeingHovered) {}
	/** Make the node name's text box editable. */
	virtual void EnterRenameMode() {};
	/** Updates the highlight text to active search term. */
	virtual void SetHighlightText(const FText& InHightlightText = FText::GetEmpty()) {};
	/** Retrieves the referenced widget corresponding to the given column name. */
	virtual TSharedRef<SWidget> GetWidget(const FName& ForColumnName);

protected:
	struct FMakeNodeWidgetArgs
	{
		TSharedPtr<SWidget> DragHandle;
		TSharedPtr<SWidget> NameWidget;
		TSharedPtr<SWidget> ValueWidget;
		TSharedPtr<SWidget> ResetButton;
	};
	
	/** Create a widget that represents a row with a splitter. */
	TSharedRef<SWidget> MakeSplitRow(TSharedRef<SWidget> LeftColumn, TSharedRef<SWidget> RightColumn);
	/** Create a widget that represents a node in the panel tree hierarchy. */
	TSharedRef<SWidget> MakeNodeWidget(const FMakeNodeWidgetArgs& Args);
	/** Creates cached copies of underlying widgets. */
	void MakeNodeWidgets(const FMakeNodeWidgetArgs& Args);

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

	/** Cached widget of drag handle. */
	TSharedPtr<SWidget> DragHandleWidget;

	/** Cached widget of node name widget. */
	TSharedPtr<SWidget> NodeNameWidget;

	/** Cached widget of node value widget. */
	TSharedPtr<SWidget> NodeValueWidget;
	
	/** Cached widget of reset arrow widget. */
	TSharedPtr<SWidget> ResetValueWidget;

private:
	/** The splitter offset to align the group splitter with the other row's splitters. */
	static constexpr float SplitterOffset = 0.008f;
};
