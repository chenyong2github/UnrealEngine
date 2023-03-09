// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"

enum class ECheckBoxState : uint8;
namespace ESelectInfo { enum Type : int; }
namespace ETextCommit { enum Type : int; }
template <typename ItemType> class SListView;

class ITableRow;
class SComboButton;
class SImage;
class SSearchBox;
class STableViewBase;
class SVerticalBox;
class SWidget;
class UCustomizableObjectNodeObject;
struct FSlateBrush;
template <typename OptionType> class SComboBox;

/** Object representing a drag and drop operation for states and parameters */
class FDragAndDropOpWithWidget : public FDragAndDropVerticalBoxOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FDragAndDropOpWithWidget, FDragAndDropVerticalBoxOp)

	/** Returns a new Drag and drop operation already initialized and ready to be used */
	static TSharedRef<FDragAndDropOpWithWidget> New(int32 InSlotIndexBeingDragged, SVerticalBox::FSlot* InSlotBeingDragged, TSharedPtr<SWidget> InWidgetToShow);
	
	// FromFDragAndDropVerticalBoxOp
	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

private:
	/** Widget object that will be shown when dragging the object around */
	TSharedPtr<SWidget> WidgetToShow;
};


class SCustomizableObjectRuntimeParameter : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCustomizableObjectRuntimeParameter) {}
	SLATE_ARGUMENT(class UCustomizableObjectNodeObject*, Node)
	SLATE_ARGUMENT(int32, StateIndex)
	SLATE_ARGUMENT(int32, RuntimeParameterIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	/** Creates the content of the Combo Button */
	TSharedRef<SWidget> GetComboButtonContent();

	/** Generates the text of the combobox option */
	FText GetCurrentItemLabel() const;

	/** Callback for the Combo Button selection */
	void OnComboButtonSelectionChanged(TSharedPtr<FString> SelectedItem, ESelectInfo::Type SelectInfo);

	/** Generates the labels of the list view for the combo button */
	TSharedRef<ITableRow> RowNameComboButtonGenerateWidget(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Generate the Combo Button selected label */
	FText GetRowNameComboButtonContentText() const;

	/** Callback for the OnTextChanged of the SearchBox */
	void OnSearchBoxFilterTextChanged(const FText& InText);

	/** Callback for the OnTextCommited of the SearchBox */
	void OnSearchBoxFilterTextCommitted(const FText& InText, ETextCommit::Type CommitInfo);

	// Returns true if the item should be visible in the Combo button
	bool IsItemVisible(TSharedPtr<FString> Item);

private:

	/** Node with all the information */
	class UCustomizableObjectNodeObject* Node = nullptr;

	/** Index to identify which parameter this widget is modifying */
	int32 StateIndex = -1;

	/** Index to identify which parameter this widget is modifying */
	int32 RuntimeParameterIndex = -1;

	/** Combobox for runtime parameter options */
	TSharedPtr<SComboBox<TSharedPtr<FString>>> RuntimeParamCombobox;

	/** Options shown in the ListView widget */
	TArray<TSharedPtr<FString>> ListViewOptions;

	/** ComboButton Selection */
	TSharedPtr<FString> ComboButtonSelection;

	/** ComboButton Widget */
	TSharedPtr<SComboButton> ComboButton;
	TSharedPtr< SListView< TSharedPtr< FString >>> RowNameComboListView;

	/** Search box of the ComboButton */
	TSharedPtr<SSearchBox> SearchBoxWidget;

	/** Stores the input of the Search box widget*/
	FString SearchItem;
};



/* Widget that represents List of Runtime Parameters Widgets */
class SCustomizableObjectRuntimeParameterList : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCustomizableObjectRuntimeParameterList) {}
	SLATE_ARGUMENT(class UCustomizableObjectNodeObject*, Node)
	SLATE_ARGUMENT(int32, StateIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void BuildList();

	/** Removes a Runtime parameter and rebuilds the runtimeParameter widgets */
	FReply OnDeleteRuntimeParameter(int32 ParameterIndex);

	/** Tells the caller if the parameter list is collapsed (hidden contents) or not.*/
	bool IsCollapsed() const { return bCollapsed; }

	/** Set the collapsed state of the list of parameters.
	 * @param bShouldBeCollapsed Determines if the contents of the list should be hidden (true) or visible (false)
	 */
	void SetCollapsed(bool bShouldBeCollapsed ) { bCollapsed = bShouldBeCollapsed; }

	/** Update the state this parameter list is looking at in order to draw it's parameters. Updating this value will
	 * rebuild the list of parameters.
	 */
	void UpdateStateIndex(int32 NewStateIndex);

private:
	
	// Parameter drag and drop callbacks

	/** Method invoked each time a new drag operation is found to be happening using one parameter slate part of the parameters list */
	FReply OnParamDragDetected(const FGeometry& Geometry, const FPointerEvent& PointerEvent, int SlotBeingDraggedIndex, SVerticalBox::FSlot* Slot);

	/** Method invoked each time the dragged parameter intersects with another slate. It determines if the drop can be performed over the
	 * intersected slate object.
	 */
	TOptional<SDragAndDropVerticalBox::EItemDropZone> OnCanAcceptParamDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone ItemDropZone, SVerticalBox::FSlot* Slot);

	/** Method invoked if the drop operation has been verified by OnCanAcceptParamDrop.
	 * @note It will perform such drop by updating the
	 * runtime parameters array (from the set StateIndex) and then rebuild the list of parameters.
	 */
	FReply OnAcceptParamDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone ItemDropZone, int NewIndex, SVerticalBox::FSlot* Slot);


	/** Node with all the information */
	class UCustomizableObjectNodeObject* Node = nullptr;
	
	/** Index to identify which parameter this widget is modifying */
	int32 StateIndex = -1;

	/** Bool that determines if a RuntimeParameter should be collapsed or not*/
	bool bCollapsed = false;

	/** Vertical box widget for the RuntimeParameter Widgets  */
	TSharedPtr<SDragAndDropVerticalBox> VerticalSlots;
};


/** Structure representing a mutable state. */
class SCustomizableObjectState : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCustomizableObjectState) {}
	SLATE_ARGUMENT(class UCustomizableObjectNodeObject*, Node)
	SLATE_ARGUMENT(int32, StateIndex)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Callback for the collapsing arrow checkbox */
	void OnCollapseChanged(ECheckBoxState NewCheckedState);

	/** Gets if a runtime parameter widget is collapsed */
	EVisibility GetCollapsed();

	/** Return the brush for the collapsed arrow */
	const FSlateBrush* GetExpressionPreviewArrow() const;

	/** Return a text with the amount of parameters the targeted state has. It is formatted for UI usage*/
	FText GetStateParameterCountText() const;

	/** Adds a new runtime parameter and rebuild the runtimeparameterList Widgets */
	FReply OnAddRuntimeParameterPressed();

	/** Update the state index this slate object is using for drawing it's content. Will also affect child structures of this
	 * slate*/
	void UpdateStateIndex(int32 NewStateIndex);

	
private:

	/** Node with all the information */
	class UCustomizableObjectNodeObject* Node = nullptr;

	/** Index to identify which parameter this widget is modifying */
	int32 StateIndex = -1;

	/** Vertical Boxes to store the widget of each state */
	TSharedPtr<SVerticalBox> VerticalSlots;
	
	/** Array to edit widgets when needed */
	TSharedPtr<SCustomizableObjectRuntimeParameterList> RuntimeParametersWidget;

	/** SImage to control the collapsing buttons */
	TSharedPtr<SImage> CollapsedArrow;
	
};



class SCustomizableObjectNodeObjectSatesView : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SCustomizableObjectNodeObjectSatesView) {}
	SLATE_ARGUMENT(UCustomizableObjectNodeObject*, Node)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	
private:
	
	/** Method invoked each time a new drag operation is found to be happening using one state slate part of the parameters list */
	FReply OnStateDragDetected(const FGeometry& Geometry, const FPointerEvent& PointerEvent, int SlotBeingDraggedIndex, SVerticalBox::FSlot* Slot);

	/** Method invoked each time the dragged state intersects with another slate. It determines if the drop can be performed over the
	 * intersected slate object.
	 */
	TOptional<SDragAndDropVerticalBox::EItemDropZone> OnCanAcceptStateDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone ItemDropZone, SVerticalBox::FSlot* Slot);

	/** Method invoked if the drop operation has been verified by OnCanAcceptParamDrop.
	 * @note It will perform such drop by updating the
	 * state array structure to later update their respective targeted state indices based on the new position they will
	 * hold as children of this slate.
	 */
	FReply OnAcceptStateDrop(const FDragDropEvent& DragDropEvent, SDragAndDropVerticalBox::EItemDropZone ItemDropZone, int NewIndex, SVerticalBox::FSlot* Slot);
	
	/** Updates each state slate internal stateIndex to match their children index inside the VerticalBox.
	 * @note Method that works together with the drag and drop operation. */
	void UpdateStatesIndex();

private:

	/** Variable keeping track if a state drag and drop has been performed. If so it will correct the slates of the
	 * child SCustomizableObjectState to match the new UI structure.
	 */
	bool bWasStateDropPerformed = false;
	
	/** Pointer to the current Node */
	UCustomizableObjectNodeObject* Node = nullptr;

	/** Vertical Boxes to store the widget of each state */
	TSharedPtr<SDragAndDropVerticalBox> VerticalSlots;
};





