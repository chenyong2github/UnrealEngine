// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteControlPreset.h"
#include "SRCPanelTreeNode.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

struct FRCPanelGroup;
struct FRemoteControlTarget;
class FDragDropOperation;
struct FGuid;
class FReply;
struct FRemoteControlProperty;
struct FRemoteControlPresetGroup;
struct FRemoteControlFunction;
class ITableRow;
class SFieldGroup;
struct SRCPanelTreeNode;
class SRemoteControlTarget;
struct SRCPanelExposedField;
class STableViewBase;
class URemoteControlPreset;

/** Holds information about a group drag and drop event  */
struct FGroupDragEvent
{
	FGroupDragEvent(TSharedPtr<SFieldGroup> InDragOriginGroup, TSharedPtr<SFieldGroup> InDragTargetGroup)
		: DragOriginGroup(MoveTemp(InDragOriginGroup))
		, DragTargetGroup(MoveTemp(InDragTargetGroup))
	{
	}

	bool IsDraggedFromSameGroup() const;

	/** Group the drag originated in. */
	TSharedPtr<SFieldGroup> DragOriginGroup;
	/** Group where the element was dropped. */
	TSharedPtr<SFieldGroup> DragTargetGroup;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelectionChange, const TSharedPtr<SRCPanelTreeNode>&/*SelectedNode*/);

class SRCPanelExposedEntitiesList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCPanelExposedEntitiesList)
		: _EditMode(true)
		, _DisplayValues(true)
	{}
		SLATE_ATTRIBUTE(bool, EditMode)
		SLATE_ARGUMENT(bool, DisplayValues)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset);
	~SRCPanelExposedEntitiesList();

	/** Get the currently selected group or exposed entity. */
	TSharedPtr<SRCPanelTreeNode> GetSelection() const;
	
	/** Set the currently selected group or exposed entity. */
	void SetSelection(const TSharedPtr<SRCPanelTreeNode>& Node);

	/** Recreate everything in the panel. */
	void Refresh();
	
	/** Returns delegate called on selection change. */
	FOnSelectionChange& OnSelectionChange() { return OnSelectionChangeDelegate; }

private:
	/** Handles object property changes, used to update arrays correctly.  */
	void OnObjectPropertyChange(UObject* InObject, FPropertyChangedEvent& InChangeEvent);
	/** Create exposed entity widgets. */
	void GenerateListWidgets();
	/** Generate the groups using the preset's layout data. */
	void RefreshGroups();
	/** Generate row widgets for groups and exposed entities. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<SRCPanelTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable);
	/** Handle getting a node's children. */
	void OnGetNodeChildren(TSharedPtr<SRCPanelTreeNode> Node, TArray<TSharedPtr<SRCPanelTreeNode>>& OutNodes);
	/** Handle selection changes. */
	void OnSelectionChanged(TSharedPtr<SRCPanelTreeNode> Node, ESelectInfo::Type SelectInfo);
	/** Handlers for drag/drop events. */
	FReply OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SRCPanelTreeNode>& TargetEntity, const TSharedPtr<SFieldGroup>& DragTargetGroup);
	/** Get the id of the group that holds a particular widget. */
	FGuid GetGroupId(const FGuid& EntityId);
	/** Handles group deletion. */
	void OnDeleteGroup(const TSharedPtr<SFieldGroup>& PanelGroup);
	/** Select actors in the current level. */
	void SelectActorsInlevel(const TArray<UObject*>& Objects);
	//~ Register to engine/editor events in order to correctly update widgets.
	void RegisterEvents();
	void UnregisterEvents();

	//~ Handlers for getting/setting the entity list's column width.
	float OnGetLeftColumnWidth() const { return 1.0f - ColumnWidth; }
	float OnGetRightColumnWidth() const { return ColumnWidth; }
	void OnSetColumnWidth(float InWidth) { ColumnWidth = InWidth; }

	/** Find a group using its id. */
	TSharedPtr<SFieldGroup> FindGroupById(const FGuid& Id);

	//~ Register and handle preset delegates.
	void RegisterPresetDelegates();
	void UnregisterPresetDelegates();
	void OnEntityAdded(const FGuid& EntityId);
	void OnEntityRemoved(const FGuid& InGroupId, const FGuid& EntityId);
	void OnGroupAdded(const FRemoteControlPresetGroup& Group);
	void OnGroupDeleted(FRemoteControlPresetGroup DeletedGroup);
	void OnGroupOrderChanged(const TArray<FGuid>& GroupIds);
	void OnGroupRenamed(const FGuid& GroupId, FName NewName);
	void OnFieldAdded(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition);
	void OnFieldDeleted(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition);
	void OnFieldOrderChanged(const FGuid& GroupId, const TArray<FGuid>& Fields);
	void OnEntitiesUpdated(URemoteControlPreset*, const TSet<FGuid>& UpdatedEntities);

private:
	/** Holds the fields list view. */
	TSharedPtr<STreeView<TSharedPtr<SRCPanelTreeNode>>> TreeView;
	/** Holds all the field groups. */
	TArray<TSharedPtr<SFieldGroup>> FieldGroups;
	/** Map of field ids to field widgets. */
	TMap<FGuid, TSharedPtr<SRCPanelTreeNode>> FieldWidgetMap;
	/** Whether the panel is in edit mode. */
	TAttribute<bool> bIsInEditMode;
	/** Holds the preset asset. */
	TStrongObjectPtr<URemoteControlPreset> Preset;
	/** Handle to the delegate called when an object property change is detected. */
	FDelegateHandle OnPropertyChangedHandle;
	/** Delegate called on selected group change. */
	FOnSelectionChange OnSelectionChangeDelegate;
	/** Whether to display the values in the list. */
	bool bDisplayValues = false;
	/** The column data shared between all tree nodes in order to share a splitter amongst all rows. */
	FRCColumnSizeData ColumnSizeData;
	/** The actual width of the right column.  The left column is 1-ColumnWidth */
	float ColumnWidth = 0.65f;
};