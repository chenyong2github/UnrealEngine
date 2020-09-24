// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EditorUndoClient.h"

#include "Algo/Transform.h"
#include "Widgets/Views/SListView.h"
#include "GameFramework/Actor.h"
#include "IDetailTreeNode.h"
#include "Input/Reply.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"

class FDragDropEvent;
class FExposedFieldDragDropOp;
struct FListEntry;
struct FRemoteControlTarget;
class IPropertyRowGenerator;
class IPropertyHandle;
struct SExposedFieldWidget;
class SRemoteControlPanel;
class SRemoteControlTarget;
class URemoteControlPreset;

DECLARE_DELEGATE_TwoParams(FOnEditModeChange, TSharedPtr<SRemoteControlPanel> /* Panel */, bool /* bEditModeChange */);

/**
 * Holds information about a group which contains fields.
 */
struct FFieldGroup
{
	FFieldGroup(FName InName, FGuid InId, const TSharedPtr<SRemoteControlPanel>& OwnerPanel)
		: Name(InName)
		, Id(InId)
		, WeakOwnerPanel(OwnerPanel)
	{}

	FFieldGroup(FName InName, FGuid InId, const TSharedPtr<SRemoteControlPanel>& OwnerPanel, TArray<TSharedPtr<SExposedFieldWidget>> InFields)
		: Name(InName)
		, Id(InId)
		, WeakOwnerPanel(OwnerPanel)
		, Fields(MoveTemp(InFields))
	{}

public:
	/** Name of the group. */
	FName Name;
	/** Id for this group. (Matches the one in the preset layout data. */
	FGuid Id;
	/** This field's owner panel. */
	TWeakPtr<SRemoteControlPanel> WeakOwnerPanel;
	/** This group's fields' widget. */
	TArray<TSharedPtr<SExposedFieldWidget>> Fields;
};

/** Widget representing a group. */
class SFieldGroup : public STableRow<TSharedPtr<FFieldGroup>>
{
public:
	DECLARE_DELEGATE_RetVal_ThreeParams(FReply, FOnFieldDropEvent, const TSharedPtr<FDragDropOperation>& /* Event */, const TSharedPtr<SExposedFieldWidget>& /* TargetField */, const TSharedPtr<FFieldGroup>& /* DragTargetGroup */);
	DECLARE_DELEGATE_RetVal_OneParam(FGuid, FOnGetGroupId, const TSharedPtr<SExposedFieldWidget>& /* TargetField */);
	DECLARE_DELEGATE_OneParam(FOnDeleteGroup, const TSharedPtr<FFieldGroup>&);

	SLATE_BEGIN_ARGS(SFieldGroup)
		: _EditMode(true) 
		{}
		SLATE_EVENT(FOnFieldDropEvent, OnFieldDropEvent)
		SLATE_EVENT(FOnGetGroupId, OnGetGroupId)
		SLATE_EVENT(FOnDeleteGroup, OnDeleteGroup)
		SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

	void Tick(const FGeometry&, const double, const float);
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FFieldGroup>& FieldGroup, const TSharedPtr<SRemoteControlPanel>& OwnerPanel);

	/** Refresh this groups' fields' list. */
	void Refresh();

	/** Get this group's name. */
	FName GetGroupName() const;
	/** Get this widget's underlying group. */
	TSharedPtr<FFieldGroup> GetGroup() const;

private:
	//~ Handle drag/drop events
	void OnDragEnterGroup(const FDragDropEvent& Event, TSharedPtr<SExposedFieldWidget> TargetField);
	void OnDragLeaveGroup(const FDragDropEvent& Event, TSharedPtr<SExposedFieldWidget> TargetField);
	FReply OnFieldDropGroup(const FDragDropEvent& Event, TSharedPtr<SExposedFieldWidget> TargetField);
	FReply OnFieldDropGroup(TSharedPtr<FDragDropOperation> DragDropOperation, TSharedPtr<SExposedFieldWidget> TargetField);
	bool OnAllowDropFromOtherGroup(TSharedPtr<FDragDropOperation> DragDropOperation);

	/** Generates a row wrapping an exposed field widget. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<SExposedFieldWidget> Field, const TSharedRef<STableViewBase>& InnerOwnerTable);
	/** Handles group deletion */
	FReply HandleDeleteGroup();
	/** Returns group name's text color according to the current selection. */
	FSlateColor GetGroupNameTextColor() const;
	/** Get the border image according to the current selection. */
	const FSlateBrush* GetBorderImage() const;
	/** Get the visibility according to the panel's current mode. */
	EVisibility GetVisibilityAccordingToEditMode() const;

	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage);

	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo);
private:
	/** Holds the list view widget. */
	TSharedPtr<SListView<TSharedPtr<SExposedFieldWidget>>> FieldsListView;
	/** The field group that interfaces with the underlying data. */
	TSharedPtr<FFieldGroup> FieldGroup;
	/** Event called when something is dropped on this group. */
	FOnFieldDropEvent OnFieldDropEvent;
	/** Getter for this group's name. */
	FOnGetGroupId OnGetGroupId;
	/** Event called then the user deletes the group. */
	FOnDeleteGroup OnDeleteGroup;
	/** Weak pointer to the parent panel. */
	TWeakPtr<SRemoteControlPanel> WeakPanel;
	/** Holds the text box for the group name. */
	TSharedPtr<SInlineEditableTextBlock> NameTextBox;
	/** Whether the panel is currently in edit mode. */
	TAttribute<bool> bEditMode;
	/** Whether the group needs to be renamed. (As requested by a click on the rename button) */
	bool bNeedsRename = false;
};

/**
 * Holds and manages the exposed field widgets for a remote control target.
 */
class SRemoteControlTarget
{
public:
	SRemoteControlTarget(FName Alias, TSharedRef<SRemoteControlPanel>& InOwnerPanel);

	/** Refreshes the exposed field widgets under this target. */
	void RefreshTargetWidgets();

	/** Get the objects bound to this target. */
	TSet<UObject*> GetBoundObjects() const;

	/** Callback called when objects get replaced. (ie. when an actor gets moved) */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap);

	/** Get the underlying section */
	FRemoteControlTarget& GetUnderlyingTarget();

	/** Get the common class of the section's top level objects */
	UClass* GetTargetClass() { return GetUnderlyingTarget().Class; }

	/** Get the alias for this target. */
	FName GetTargetAlias() const { return TargetAlias; }

	/** Get the field widgets under this target. */
	TArray<TSharedRef<SExposedFieldWidget>>& GetFieldWidgets() { return ExposedFieldWidgets; }

private:

	/** Generate widgets for the exposed properties. */
	void GenerateExposedPropertyWidgets();

	/** Generate widgets for the exposed functions. */
	void GenerateExposedFunctionWidgets();

	/** Bind the property widgets to the section's top level objects. */
	void BindPropertyWidgets();

	/** Handle clicking on an exposed function's button. */
	FReply OnClickFunctionButton(FRemoteControlFunction FunctionField);

	/** Handle getting the visibility of certain widgets according to the panel's mode. */
	EVisibility GetVisibilityAccordingToEditMode() const;

	/** Get whether the panel is in edit mode. */
	bool GetPanelEditMode() const;

	/** Get this target's owner panel. */
	URemoteControlPreset* GetPreset();

private:
	/** The section's underlying alias. */
	FName TargetAlias;
	/** Weak pointer to the parent panel. */
	TWeakPtr<SRemoteControlPanel> WeakPanel;
	/** Holds the exposed fields. */
	TArray<TSharedRef<SExposedFieldWidget>> ExposedFieldWidgets;
};


/**
 * Structure that holds all the information necessary to expose a property in the panel.
 */
struct FExposableProperty
{
	/** Construct using all the property handle's objects */
	FExposableProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle, const TArray<UObject*>& InOwnerObjects);

	bool IsValid() const
	{
		return !PropertyDisplayName.IsEmpty() && !PropertyName.IsNone() && OwnerObjects.Num() > 0;
	}

	/** The property being exposed. */
	FString PropertyDisplayName;

	/** The field's top level parents. */
	TArray<UObject*> OwnerObjects;

	/** The property's name. */
	FName PropertyName;

	/** Path information for this property */
	FFieldPathInfo FieldPathInfo;
};

/**
 * UI representation of a remote control preset.
 * Allows a user to expose/unexpose properties and functions from actors and blueprint libraries.
 */
class SRemoteControlPanel : public SCompoundWidget, public FEditorUndoClient
{
	SLATE_BEGIN_ARGS(SRemoteControlPanel) {}
		SLATE_EVENT(FOnEditModeChange, OnEditModeChange)
		SLATE_ARGUMENT(bool, AllowGrouping)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset);
	~SRemoteControlPanel();

	virtual void Tick(const FGeometry&, const double, const float) override;

	//~ FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) { Refresh(); }
	virtual void PostRedo(bool bSuccess) { Refresh(); }

	/**
	 * @return The preset represented by the panel.
	 */
	URemoteControlPreset* GetPreset() { return Preset.Get(); }

	/**
	 * @return The preset represented by the panel.
	 */
	const URemoteControlPreset* GetPreset() const { return Preset.Get(); }

	/**
	 * @param Handle The handle representing the property to check.
	 * @return Whether a property is exposed or not.
	 */
	bool IsExposed(const TSharedPtr<IPropertyHandle>& PropertyHandle);

	/**
	 * Exposes or unexposes a property.
	 * @param Handle The handle of the property to toggle.
	 */
	void ToggleProperty(const TSharedPtr<IPropertyHandle>& PropertyHandle);

	/**
	 * @return Whether or not the panel is in edit mode.
	 */
	bool IsInEditMode() const { return bIsInEditMode; }

	/**
	 * Set the edit mode of the panel.
	 * @param bEditMode The desired mode.
	 */
	void SetEditMode(bool bEditMode) { bIsInEditMode = bEditMode; }

	/**
	 * Get this preset's visual layout.
	 */
	FRemoteControlPresetLayout& GetLayout();

	//~ FWidget interface
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent&) override;

private:
	/** Holds information about a group drag and drop event  */
	struct FGroupDragEvent
	{
		FGroupDragEvent(FFieldGroup& InDragOriginGroup, FFieldGroup& InDragTargetGroup)
			: DragOriginGroup(InDragOriginGroup)
			, DragTargetGroup(InDragTargetGroup)
		{
		}

		bool IsDraggedFromSameGroup() const
		{
			return DragOriginGroup.Name == DragTargetGroup.Name;
		}

		/** Group the drag originated in. */
		FFieldGroup& DragOriginGroup;
		/** Group where the element was dropped. */
		FFieldGroup& DragTargetGroup;
	};

private:
	/** Register events needed to handle reloading objets and blueprint libraries. */
	void RegisterEvents();

	/** Re-create the sections of the panel. */
	void Refresh();

	/** Refresh the layout of the panel */
	void RefreshLayout();

	/** Select a section by name */
	void SelectGroup(const TSharedPtr<FFieldGroup>& FieldGroup);

	/** Clear the section selection. */
	void ClearSelection();

	/** Expose a property using its handle. */
	FRemoteControlTarget* Expose(FExposableProperty&& Property);

	/** Unexpose a field from the preset. */
	void Unexpose(const TSharedPtr<IPropertyHandle>& Handle);

	/** Create a blueprint library picker widget. */
	TSharedRef<SWidget> CreateBlueprintLibraryPicker();

	/** Generates a row widget representing a group. */
	TSharedRef<ITableRow> OnGenerateGroupRow(TSharedPtr<FFieldGroup> Group, const TSharedRef<STableViewBase>& OwnerTable);

	/** Handles selection changing at the group level. */
	void OnGroupSelectionChanged(TSharedPtr<FFieldGroup> InGroup, ESelectInfo::Type InSelectInfo);

	/** Select actors in the current level. */
	void SelectActorsInlevel(const TArray<UObject*>& Objects);

	/** Create the section widgets. */
	void GenerateFieldWidgets();

	/** Generate the groups using the preset's layout data. */
	void RefreshGroups();

	/** Handle the using toggling the edit mode check box. */
	void OnEditModeCheckboxToggle(ECheckBoxState State);

	/** Rebuild the blueprint library list. */
	void ReloadBlueprintLibraries();

	/** Handler called when objects are replaced ie. When an actor construction script runs.  */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap);
	
	/** Handler called when an object's property is modified. */
	void OnObjectPropertyChange(UObject* InObject, struct FPropertyChangedEvent& InChangeEvent);

	/** Get the id of the group that holds a particular widget. */
	FGuid GetGroupId(const TSharedPtr<SExposedFieldWidget>& Field);

	/** Handles creating a new group. */
	FReply OnCreateGroup();

	/** Handles group deletion. */
	void OnDeleteGroup(const TSharedPtr<FFieldGroup>& FieldGroup);

	/** Exposes a function  */
	void ExposeFunction(UObject* Object, UFunction* Function);

	//~ Handlers for drag/drop events.
	FReply OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SExposedFieldWidget>& TargetField, const TSharedPtr<FFieldGroup>& DragTargetGroup);
	
private:
	
	/** Holds the preset asset. */
	TStrongObjectPtr<URemoteControlPreset> Preset;

	/** Holds the section list view. */
	TSharedPtr<SListView<TSharedPtr<FFieldGroup>>> GroupsListView;

	TArray<TSharedPtr<FFieldGroup>> FieldGroups;

	/** Holds the section widgets. */
	TArray<TSharedRef<SRemoteControlTarget>> RemoteControlTargets;

	/** Whether the panel is in edit mode. */
	bool bIsInEditMode;

	/** Holds the blueprint library picker widget. */
	TSharedPtr<class SMenuAnchor> BlueprintLibraryPicker;

	/** Holds a list of all the blueprint libraries. */
	TArray<TSharedPtr<FListEntry>> BlueprintLibraries;

	/** Delegate called when the edit mode changes. */
	FOnEditModeChange OnEditModeChange;
	
	/** Id of the last selected section. */
	FGuid LastSelectedGroupId;

	/** Handle to the delegate called when an object property change is detected. */
	FDelegateHandle OnPropertyChangedHandle;

	/** Map of field ids to field widgets. */
	TMap<FGuid, TSharedPtr<SExposedFieldWidget>> FieldWidgetMap;

	TSet<FGuid> GroupsPendingRefresh;

	friend SRemoteControlTarget;
	friend SExposedFieldWidget;
	friend class SFieldGroup;
};
