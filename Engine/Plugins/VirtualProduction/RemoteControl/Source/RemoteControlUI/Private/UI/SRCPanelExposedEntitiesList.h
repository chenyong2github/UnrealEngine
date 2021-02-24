// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "RemoteControlPreset.h"
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
struct SRCPanelTreeNode;
class SRemoteControlTarget;
struct SRCPanelExposedField;
class STableViewBase;

/**
 * Holds and manages the exposed field widgets for a remote control target.
 */
class SRemoteControlTarget
{
public:
	SRemoteControlTarget(FName Alias, URemoteControlPreset* Preset, TAttribute<bool> bInIsInEditMode, bool bInDisplayValues = true);

	/** Refreshes the exposed field widgets under this target. */
	void RefreshTargetWidgets();
	/** Add a property to this target */
	TSharedPtr<SRCPanelExposedField> AddExposedProperty(const FRemoteControlProperty& RCProperty, bool bDisplayValues);
	/** Add a function to this target */
	TSharedPtr<SRCPanelExposedField> AddExposedFunction(const FRemoteControlFunction& RCFunction, bool bDisplayValues);
	/** Get the objects bound to this target. */
	TSet<UObject*> GetBoundObjects() const;
	/** Callback called when objects get replaced. (ie. when an actor gets moved) */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap);
	/** Get the underlying section */
	FRemoteControlTarget& GetUnderlyingTarget();
	/** Get the common class of the section's top level objects */
	UClass* GetTargetClass();
	/** Get the alias for this target. */
	FName GetTargetAlias() const { return TargetAlias; }
	/** Get the field widgets under this target. */
	TArray<TSharedRef<SRCPanelExposedField>>& GetFieldWidgets() { return ExposedFieldWidgets; }

private:
	/** Handle getting the visibility of certain widgets according to the panel's mode. */
	EVisibility GetVisibilityAccordingToEditMode() const;
	/** Get whether the panel is in edit mode. */
	bool GetPanelEditMode() const;
	/** Get this target's owner panel. */
	URemoteControlPreset* GetPreset();

private:
	/** The section's underlying alias. */
	FName TargetAlias;
	/** Weak pointer to the preset. */
	TWeakObjectPtr<URemoteControlPreset> Preset;
	/** Whether the owning panel is in edit mode. */
	TAttribute<bool> bIsInEditMode;
	/** Holds the exposed fields. */
	TArray<TSharedRef<SRCPanelExposedField>> ExposedFieldWidgets;
};

/** Holds information about a group drag and drop event  */
struct FGroupDragEvent
{
	FGroupDragEvent(FRCPanelGroup& InDragOriginGroup, FRCPanelGroup& InDragTargetGroup)
		: DragOriginGroup(InDragOriginGroup)
		, DragTargetGroup(InDragTargetGroup)
	{
	}

	bool IsDraggedFromSameGroup() const;

	/** Group the drag originated in. */
	FRCPanelGroup& DragOriginGroup;
	/** Group where the element was dropped. */
	FRCPanelGroup& DragTargetGroup;
};

DECLARE_DELEGATE_OneParam(FOnSelectionChange, const TSharedPtr<SRCPanelTreeNode>&/*SelectedNode*/);

class SRCPanelExposedEntitiesList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCPanelExposedEntitiesList)
		: _EditMode(true)
		, _DisplayValues(true)
	{}
		SLATE_ATTRIBUTE(bool, EditMode)
		SLATE_EVENT(FOnSelectionChange, OnSelectionChange)
		SLATE_ARGUMENT(bool, DisplayValues)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset);
	~SRCPanelExposedEntitiesList();
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

	/** Get the currently selected group or exposed entity. */
	TSharedPtr<SRCPanelTreeNode> GetSelection() const;
	/** Set the currently selected group or exposed entity. */
	void SetSelection(const FGuid& GroupId);
	/** Returns delegate called on selection change. */
	FOnSelectionChange& OnSelectionChange() { return OnSelectionChangeDelegate; }

private:
	/** Handles object property changes, used to update arrays correctly.  */
	void OnObjectPropertyChange(UObject* InObject, FPropertyChangedEvent& InChangeEvent);
	/** Handles replacing the underlying objects of the target.  */
	void ReplaceObjects(const TMap<UObject*, UObject*>& ReplacementObjectMap);
	/** Recreate everything in the panel. */
	void Refresh();
	/** Create exposed entity widgets. */
	void GenerateListWidgets();
	/** Generate the groups using the preset's layout data. */
	void RefreshGroups();
	/** Generate row widgets for groups and exposed entities. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<SRCPanelTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable);
	/** Handle getting a group's children. */
	void OnGetGroupChildren(TSharedPtr<SRCPanelTreeNode> Node, TArray<TSharedPtr<SRCPanelTreeNode>>& OutNodes);
	/** Handle selection changes. */
	void OnSelectionChanged(TSharedPtr<SRCPanelTreeNode> Node, ESelectInfo::Type SelectInfo);
	/** Handlers for drag/drop events. */
	FReply OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SRCPanelTreeNode>& TargetEntity, const TSharedPtr<FRCPanelGroup>& DragTargetGroup);
	/** Get the id of the group that holds a particular widget. */
	FGuid GetGroupId(const FGuid& EntityId);
	/** Handles group deletion. */
	void OnDeleteGroup(const TSharedPtr<FRCPanelGroup>& PanelGroup);
	/** Select actors in the current level. */
	void SelectActorsInlevel(const TArray<UObject*>& Objects);
	//~ Register to engine/editor events in order to correctly update widgets.
	void RegisterEvents();
	void UnregisterEvents();
	/** Handle unexposing properties and functions of an actor about to be delete */ 
	void OnActorDeleted(AActor* Actor);
	/** Handle correctly updating widgets when PIE starts. */
	void OnPieEvent(bool);

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
	void OnFieldRenamed(URemoteControlPreset*, FName OldName, FName NewName);

private:
	/** Holds the fields list view. */
	TSharedPtr<STreeView<TSharedPtr<SRCPanelTreeNode>>> TreeView;
	/** Holds all the field groups. */
	TArray<TSharedPtr<FRCPanelGroup>> FieldGroups;
	/** Holds the section widgets. */
	TArray<TSharedRef<SRemoteControlTarget>> RemoteControlTargets;
	/** Map of field ids to field widgets. */
	TMap<FGuid, TSharedPtr<SRCPanelTreeNode>> FieldWidgetMap;
	/** Whether the panel is in edit mode. */
	TAttribute<bool> bIsInEditMode;
	/** Holds the preset asset. */
	TStrongObjectPtr<URemoteControlPreset> Preset;
	/** Handle to the delegate called when an object property change is detected. */
	FDelegateHandle OnPropertyChangedHandle;
	/** Handle to the delegate called when the map changes. */
	FDelegateHandle MapChangedHandle;
	/** Whether to trigger a refresh on the next frame. */
	bool bTriggerRefreshForPIE;
	/** Delegate called on selected group change. */
	FOnSelectionChange OnSelectionChangeDelegate;
	/** Whether to display the values in the list. */
	bool bDisplayValues = false;
};