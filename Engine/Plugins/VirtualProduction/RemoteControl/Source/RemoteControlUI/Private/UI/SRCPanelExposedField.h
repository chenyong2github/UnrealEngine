// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "EditorStyleSet.h"
#include "RemoteControlField.h"
#include "SRCPanelTreeNode.h"
#include "SlateFwd.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"

struct EVisibility;
enum class EExposedFieldType : uint8;
struct FSlateBrush;
struct FGuid;
class IPropertyRowGenerator;
class IDetailTreeNode;
class SInlineEditableTextBlock;
struct SRCPanelFieldChildNode;
class URemoteControlPreset;

/**
 * Widget that displays an exposed field.
 */
struct SRCPanelExposedField : public SCompoundWidget, public SRCPanelTreeNode
{
	SLATE_BEGIN_ARGS(SRCPanelExposedField)
		: _EditMode(true)
		, _Preset(nullptr)
		, _DisplayValues(true)
	{}
		SLATE_ATTRIBUTE(bool, EditMode)
		SLATE_ATTRIBUTE(URemoteControlPreset*, Preset)
		SLATE_ARGUMENT(bool, DisplayValues)
	SLATE_END_ARGS()

	using SWidget::SharedThis;
	using SWidget::AsShared;

	void Construct(const FArguments& InArgs, const FRemoteControlField& Field);

	void Tick(const FGeometry&, const double, const float);

	//~ SRCPanelTreeNode Interface 
	virtual void GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const override;
	virtual TSharedPtr<SRCPanelExposedField> AsField() override;
	virtual FGuid GetId() const override;
	virtual SRCPanelTreeNode::ENodeType GetType() const override;
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
	void HandleUnexposeField();
	/** Verifies that the field's label doesn't already exist. */
	bool OnVerifyItemLabelChanged(const FText& InLabel, FText& OutErrorMessage);
	/** Handles committing a field label. */
	void OnLabelCommitted(const FText& InLabel, ETextCommit::Type InCommitInfo);
	/** Construct this field widget as a property widget. */
	void ConstructPropertyWidget();
	/** Construct this field widget as a function widget. */
	void ConstructFunctionWidget();
	/** Construct a button  */
	TSharedRef<SWidget> ConstructCallFunctionButton();
	/** Handles calling an exposed function.*/
	FReply OnClickFunctionButton();
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
	TArray<TSharedPtr<SRCPanelFieldChildNode>> ChildWidgets;
	/** Holds the generator that creates the widgets. */
	TSharedPtr<IPropertyRowGenerator> RowGenerator;
	/** Whether the panel is in edit mode or not. */
	TAttribute<bool> bEditMode;
	/** The underlying preset. */
	TAttribute<URemoteControlPreset*> Preset;
	/** The textbox for the row's name. */
	TSharedPtr<SInlineEditableTextBlock> NameTextBox;
	/** Whether to display the call function button and the property values. */
	bool bDisplayValues;
};


/** Represents a child of an exposed field widget. */
struct SRCPanelFieldChildNode : public SCompoundWidget, public SRCPanelTreeNode
{
	SLATE_BEGIN_ARGS(SRCPanelFieldChildNode)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<IDetailTreeNode>& InNode);
	virtual void GetNodeChildren(TArray<TSharedPtr<SRCPanelTreeNode>>& OutChildren) const { return OutChildren.Append(ChildrenNodes); }
	virtual FGuid GetId() const { return FGuid(); }
	virtual ENodeType GetType() const { return SRCPanelTreeNode::FieldChild; }
	virtual TSharedPtr<SWidget> AsFieldChild() { return AsShared(); }

	TArray<TSharedPtr<SRCPanelFieldChildNode>> ChildrenNodes;
};
