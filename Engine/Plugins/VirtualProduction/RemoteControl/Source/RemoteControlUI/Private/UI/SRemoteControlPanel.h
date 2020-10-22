// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "EditorUndoClient.h"

#include "Algo/Transform.h"
#include "Widgets/Views/SListView.h"
#include "GameFramework/Actor.h"
#include "IDetailTreeNode.h"
#include "Input/Reply.h"
#include "Layout/Visibility.h"
#include "RemoteControlField.h"
#include "RemoteControlPreset.h"
#include "SSearchableTreeView.h"
#include "Templates/SubclassOf.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

class FDragDropEvent;
class FExposedFieldDragDropOp;
struct FListEntry;
struct FRemoteControlTarget;
class IPropertyRowGenerator;
class IPropertyHandle;
struct SRCPanelExposedField;
class SRemoteControlPanel;
class SRemoteControlTarget;
class URemoteControlPreset;
class UBlueprintFunctionLibrary;

DECLARE_DELEGATE_TwoParams(FOnEditModeChange, TSharedPtr<SRemoteControlPanel> /* Panel */, bool /* bEditModeChange */);

/** A node in the blueprint picker tree view. */
struct FRCBlueprintPickerTreeNode
{
	virtual ~FRCBlueprintPickerTreeNode() = default;
	virtual const FString& GetName() const = 0;
	virtual bool IsFunctionNode() const = 0;
	virtual UBlueprintFunctionLibrary* GetLibrary() const = 0;
	virtual UFunction* GetFunction() const = 0;
	virtual void GetChildNodes(TArray<TSharedPtr<FRCBlueprintPickerTreeNode>>& OutNodes) const = 0;
};

/** A node in the panel tree view. */
struct FRCPanelTreeNode 
{
	enum ENodeType
	{
		Invalid,
		Group,
		Field,
		FieldChild
	};

	virtual ~FRCPanelTreeNode() {}

	/** Get this tree node's childen. */
	virtual void GetNodeChildren(TArray<TSharedPtr<FRCPanelTreeNode>>& OutChildren) {}
	/** Get this node's ID if any. */
	virtual FGuid GetId() = 0;
	/** Get get this node's type. */
	virtual ENodeType GetType() = 0;

	//~ Utiliy methods for not having to downcast 
	virtual TSharedPtr<SRCPanelExposedField> AsField() { return nullptr; }
	virtual TSharedPtr<SWidget> AsFieldChild() { return nullptr; }
	virtual TSharedPtr<struct FRCPanelGroup> AsGroup() { return nullptr; }
};

/** Represents a child of an exposed field widget. */
struct FRCPanelFieldChildNode : public SCompoundWidget, public FRCPanelTreeNode
{
	SLATE_BEGIN_ARGS(FRCPanelFieldChildNode)
		{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IDetailTreeNode>& InNode);
	virtual void GetNodeChildren(TArray<TSharedPtr<FRCPanelTreeNode>>& OutChildren) { return OutChildren.Append(ChildrenNodes); }
	virtual FGuid GetId() { return FGuid(); }
	virtual ENodeType GetType() { return FRCPanelTreeNode::FieldChild; }
	virtual TSharedPtr<SWidget> AsFieldChild() { return AsShared(); }

	TArray<TSharedPtr<FRCPanelFieldChildNode>> ChildrenNodes;
};

/**
 * Widget that displays an exposed field.
 */
struct SRCPanelExposedField : public SCompoundWidget, public FRCPanelTreeNode
{
	SLATE_BEGIN_ARGS(SRCPanelExposedField)
		: _Content()
		, _ChildWidgets(nullptr)
		, _EditMode(true)
	{}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_NAMED_SLOT(FArguments, OptionsContent)
	SLATE_ARGUMENT(const TArray<TSharedPtr<FRCPanelFieldChildNode>>*, ChildWidgets)
	SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

	using SWidget::SharedThis;
	using SWidget::AsShared;

	void Construct(const FArguments& InArgs, const FRemoteControlField& Field, TSharedRef<IPropertyRowGenerator> InRowGenerator, TWeakPtr<SRemoteControlPanel> InPanel);

	void Tick(const FGeometry&, const double, const float);

	//~ SRCPanelTreeNode Interface 
	virtual void GetNodeChildren(TArray<TSharedPtr<FRCPanelTreeNode>>& OutChildren) override;
	virtual TSharedPtr<SRCPanelExposedField> AsField() override;
	virtual FGuid GetId() override;
	virtual FRCPanelTreeNode::ENodeType GetType() override;
	//~ End SRCPanelTreeNode Interface
	
	/** Get this field's label. */ 
	FName GetFieldLabel() const;

	/** Get this field's id. */
	FGuid GetFieldId() const;

	/** Get this field's type. */
	EExposedFieldType GetFieldType() const;

	/** Set whether this widet is currently hovered when drag and dropping. */
	void SetIsHovered(bool bInIsHovered);

	/** Handler called when objects get replaced (Usually called when you move an object) */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap);

	/** Refresh the widget. */
	void Refresh();

	/** Returns this widget's underlying objects. */
	void GetBoundObjects(TSet<UObject*>& OutBoundObjects) const;

	/** Set this widget's underlying objects. */
	void SetBoundObjects(const TArray<UObject*>& InObjects);

private:
	/** Construct a property widget. */
	TSharedRef<SWidget> ConstructWidget();
	/** Create the wrapper around the field value widget. */
	TSharedRef<SWidget> MakeFieldWidget(const TSharedRef<SWidget>& InWidget);
	/** Get the widget's visibility according to the panel's mode. */
	EVisibility GetVisibilityAccordingToEditMode(EVisibility NonEditModeVisibility) const;
	/** Get the widget's border. */
	const FSlateBrush* GetBorderImage() const;
	/** Handle clicking on the unexpose button. */
	FReply HandleUnexposeField();
	/** Get the options button visibility. */
	EVisibility GetOptionsButtonVisibility() const;
	/** Verifies that the field's label doesn't already exist. */
	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage);
	/** Handles committing a field label. */
	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo);
private:
	/** Type of the exposed field. */
	EExposedFieldType FieldType;
	/** Name of the field's underlying property. */
	FName FieldName;
	/** Display name of the field. */
	FName FieldLabel;
	/** Qualified field name, with its path to parent */
	FRCFieldPathInfo FieldPathInfo;
	/** Id of the field. */
	FGuid FieldId;
	/** Whether the row should display its options. */
	bool bShowOptions = false;
	/** Whether the widget is currently hovered by a drag and drop operation. */
	bool bIsHovered = false;
	/** Whether the editable text box for the label needs to enter edit mode. */
	bool bNeedsRename = false;
	/** The widget that displays the field's options ie. Function arguments or metadata. */
	TSharedPtr<SWidget> OptionsWidget;
	/** This exposed field's child widgets (ie. An array's rows) */
	TArray<TSharedPtr<FRCPanelFieldChildNode>> ChildWidgets;
	/** Holds the generator that creates the widgets. */
	TSharedPtr<IPropertyRowGenerator> RowGenerator;
	/** Whether the panel is in edit mode or not. */
	TAttribute<bool> bEditMode;
	/** Weak ptr to the panel */
	TWeakPtr<SRemoteControlPanel> WeakPanel;
	/** The textbox for the row's name. */
	TSharedPtr<SInlineEditableTextBlock> NameTextBox;
};

/**
 * Holds information about a group which contains fields.
 */
struct FRCPanelGroup : public FRCPanelTreeNode, public TSharedFromThis<FRCPanelGroup>
{
	FRCPanelGroup(FName InName, FGuid InId, const TSharedPtr<SRemoteControlPanel>& OwnerPanel)
		: Name(InName)
		, Id(InId)
		, WeakOwnerPanel(OwnerPanel)
	{}

	FRCPanelGroup(FName InName, FGuid InId, const TSharedPtr<SRemoteControlPanel>& OwnerPanel, TArray<TSharedPtr<SRCPanelExposedField>> InFields)
		: Name(InName)
		, Id(InId)
		, WeakOwnerPanel(OwnerPanel)
		, Fields(MoveTemp(InFields))
	{}

	//~ SRCPanelTreeNode Interface
	virtual void GetNodeChildren(TArray<TSharedPtr<FRCPanelTreeNode>>& OutChildren) override;
	virtual FGuid GetId() override;
	virtual ENodeType GetType() override;
	virtual TSharedPtr<FRCPanelGroup> AsGroup() override;

public:
	/** Name of the group. */
	FName Name;
	/** Id for this group. (Matches the one in the preset layout data. */
	FGuid Id;
	/** This field's owner panel. */
	TWeakPtr<SRemoteControlPanel> WeakOwnerPanel;
	/** This group's fields' widget. */
	TArray<TSharedPtr<SRCPanelExposedField>> Fields;
};

/** Widget representing a group. */
class SFieldGroup : public STableRow<TSharedPtr<FRCPanelGroup>>
{
public:
	DECLARE_DELEGATE_RetVal_ThreeParams(FReply, FOnFieldDropEvent, const TSharedPtr<FDragDropOperation>& /* Event */, const TSharedPtr<SRCPanelExposedField>& /* TargetField */, const TSharedPtr<FRCPanelGroup>& /* DragTargetGroup */);
	DECLARE_DELEGATE_RetVal_OneParam(FGuid, FOnGetGroupId, const TSharedPtr<SRCPanelExposedField>& /* TargetField */);
	DECLARE_DELEGATE_OneParam(FOnDeleteGroup, const TSharedPtr<FRCPanelGroup>&);

	SLATE_BEGIN_ARGS(SFieldGroup)
		: _EditMode(true) 
		{}
		SLATE_EVENT(FOnFieldDropEvent, OnFieldDropEvent)
		SLATE_EVENT(FOnGetGroupId, OnGetGroupId)
		SLATE_EVENT(FOnDeleteGroup, OnDeleteGroup)
		SLATE_ATTRIBUTE(bool, EditMode)
	SLATE_END_ARGS()

	void Tick(const FGeometry&, const double, const float);
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FRCPanelGroup>& FieldGroup, const TSharedPtr<SRemoteControlPanel>& OwnerPanel);
		
	/** Refresh this groups' fields' list. */
	void Refresh();

	/** Get this group's name. */
	FName GetGroupName() const;
	/** Get this widget's underlying group. */
	TSharedPtr<FRCPanelGroup> GetGroup() const;
	
	/** Set this widget's name. */
	void SetName(FName Name);

private:
	//~ Handle drag/drop events
	FReply OnFieldDropGroup(const FDragDropEvent& Event, TSharedPtr<SRCPanelExposedField> TargetField);
	FReply OnFieldDropGroup(TSharedPtr<FDragDropOperation> DragDropOperation, TSharedPtr<SRCPanelExposedField> TargetField);
	bool OnAllowDropFromOtherGroup(TSharedPtr<FDragDropOperation> DragDropOperation);

	/** Handles group deletion */
	FReply HandleDeleteGroup();
	/** Returns group name's text color according to the current selection. */
	FSlateColor GetGroupNameTextColor() const;
	/** Get the border image according to the current selection. */
	const FSlateBrush* GetBorderImage() const;
	/** Get the visibility according to the panel's current mode. */
	EVisibility GetVisibilityAccordingToEditMode(EVisibility DefaultHiddenVisibility) const;

	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage);

	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo);

private:
	/** Holds the list view widget. */
	TSharedPtr<SListView<TSharedPtr<SRCPanelExposedField>>> FieldsListView;
	/** The field group that interfaces with the underlying data. */
	TSharedPtr<FRCPanelGroup> FieldGroup;
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
	SRemoteControlTarget(FName Alias, TSharedRef<SRemoteControlPanel> InOwnerPanel);

	/** Refreshes the exposed field widgets under this target. */
	void RefreshTargetWidgets();

	/** Add a field to this target */
	TSharedPtr<SRCPanelExposedField> AddExposedProperty(const FRemoteControlProperty& RCProperty);

	TSharedPtr<SRCPanelExposedField> AddExposedFunction(const FRemoteControlFunction& RCFunction);

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
	TArray<TSharedRef<SRCPanelExposedField>>& GetFieldWidgets() { return ExposedFieldWidgets; }

private:
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
	TArray<TSharedRef<SRCPanelExposedField>> ExposedFieldWidgets;
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
	FRCFieldPathInfo FieldPathInfo;

	/** Component hierarchy above the property (SceneComponent, NestedComponent1, NestedComponent2*/
	TArray<FString> ComponentChain;
};

/**
 * UI representation of a remote control preset.
 * Allows a user to expose/unexpose properties and functions from actors and blueprint libraries.
 */
class SRemoteControlPanel : public SCompoundWidget, public FSelfRegisteringEditorUndoClient
{
	SLATE_BEGIN_ARGS(SRemoteControlPanel) {}
		SLATE_EVENT(FOnEditModeChange, OnEditModeChange)
		SLATE_ARGUMENT(bool, AllowGrouping)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, URemoteControlPreset* InPreset);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	~SRemoteControlPanel();

	//~ FEditorUndoClient interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

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

private:
	/** Holds information about a group drag and drop event  */
	struct FGroupDragEvent
	{
		FGroupDragEvent(FRCPanelGroup& InDragOriginGroup, FRCPanelGroup& InDragTargetGroup)
			: DragOriginGroup(InDragOriginGroup)
			, DragTargetGroup(InDragTargetGroup)
		{
		}

		bool IsDraggedFromSameGroup() const
		{
			return DragOriginGroup.Name == DragTargetGroup.Name;
		}

		/** Group the drag originated in. */
		FRCPanelGroup& DragOriginGroup;
		/** Group where the element was dropped. */
		FRCPanelGroup& DragTargetGroup;
	};

private:
	/** Register editor events needed to handle reloading objects and blueprint libraries. */
	void RegisterEvents();
	/** Unregister editor events */
	void UnregisterEvents();

	/** Re-create the sections of the panel. */
	void Refresh();

	void OnSelectionChanged(TSharedPtr<FRCPanelTreeNode> Node, ESelectInfo::Type SelectInfo);

	/** Expose a property using its handle. */
	FRemoteControlTarget* Expose(FExposableProperty&& Property);

	/** Unexpose a field from the preset. */
	void Unexpose(const TSharedPtr<IPropertyHandle>& Handle);
	
	/** Regenerates the list of available blueprint libraries. */
	void RefreshBlueprintLibraryNodes();

	/** Create a blueprint library picker widget. */
	TSharedRef<SWidget> CreateBlueprintLibraryPicker();

	/** Generates a tree row. */
	TSharedRef<ITableRow> OnGenerateRow(TSharedPtr<FRCPanelTreeNode> Node, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get this group's children */
	void OnGetGroupChildren(TSharedPtr<FRCPanelTreeNode> Node, TArray<TSharedPtr<FRCPanelTreeNode>>& OutNodes);

	/** Select actors in the current level. */
	void SelectActorsInlevel(const TArray<UObject*>& Objects);

	/** Create the section widgets. */
	void GenerateFieldWidgets();

	/** Generate the groups using the preset's layout data. */
	void RefreshGroups();

	/** Handle the using toggling the edit mode check box. */
	void OnEditModeCheckboxToggle(ECheckBoxState State);

	/** Handler called when objects are replaced ie. When an actor construction script runs.  */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& ReplacementObjectMap);
	
	/** Handler called when an object's property is modified. */
	void OnObjectPropertyChange(UObject* InObject, struct FPropertyChangedEvent& InChangeEvent);

	/** Get the id of the group that holds a particular widget. */
	FGuid GetGroupId(const TSharedPtr<SRCPanelExposedField>& Field);

	/** Handles creating a new group. */
	FReply OnCreateGroup();

	/** Handles group deletion. */
	void OnDeleteGroup(const TSharedPtr<FRCPanelGroup>& FieldGroup);

	/** Exposes a function  */
	void ExposeFunction(UObject* Object, UFunction* Function);

	void OnActorDeleted(AActor* Actor);

	//~ Handlers for drag/drop events.
	FReply OnDropOnGroup(const TSharedPtr<FDragDropOperation>& DragDropOperation, const TSharedPtr<SRCPanelExposedField>& TargetField, const TSharedPtr<FRCPanelGroup>& DragTargetGroup);
	
	//~ Preset specific delegates
	void RegisterPresetDelegates();
	void UnregisterPresetDelegates();

	//~ PIE Start/Stop handler.
	void OnPieEvent(bool);

	void OnGroupAdded(const FRemoteControlPresetGroup& Group);
	void OnGroupDeleted(FRemoteControlPresetGroup DeletedGroup);
	void OnGroupOrderChanged(const TArray<FGuid>& GroupIds);
	void OnGroupRenamed(const FGuid& GroupId, FName NewName);
	void OnFieldAdded(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition);
	void OnFieldDeleted(const FGuid& GroupId, const FGuid& FieldId, int32 FieldPosition);
	void OnFieldOrderChanged(const FGuid& GroupId, const TArray<FGuid>& Fields);
private:
	
	/** Holds the preset asset. */
	TStrongObjectPtr<URemoteControlPreset> Preset;
	/** Holds the section list view. */
	TSharedPtr<STreeView<TSharedPtr<FRCPanelTreeNode>>> TreeView;
	/** Holds all the field groups. */
	TArray<TSharedPtr<FRCPanelGroup>> FieldGroups;
	/** Holds the section widgets. */
	TArray<TSharedRef<SRemoteControlTarget>> RemoteControlTargets;
	/** Whether the panel is in edit mode. */
	bool bIsInEditMode = true;
	/** Whether objects need to be re-resolved because PIE Started or ended. */
	bool bTriggerRefreshForPIE = false;
	/** Holds the blueprint library picker tree view. */
	TSharedPtr<SSearchableTreeView<TSharedPtr<FRCBlueprintPickerTreeNode>>> BlueprintLibrariesTreeView;
	/** Holds the blueprint library nodes. */
	TArray<TSharedPtr<FRCBlueprintPickerTreeNode>> BlueprintLibraryNodes;
	/** Delegate called when the edit mode changes. */
	FOnEditModeChange OnEditModeChange;
	/** Handle to the delegate called when an object property change is detected. */
	FDelegateHandle OnPropertyChangedHandle;
	/** Handle to the delegate called when the map changes. */
	FDelegateHandle MapChangedHandle;
	/** Map of field ids to field widgets. */
	TMap<FGuid, TSharedPtr<SRCPanelExposedField>> FieldWidgetMap;

	friend SRemoteControlTarget;
	friend SRCPanelExposedField;
	friend class SFieldGroup;
};
