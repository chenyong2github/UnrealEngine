// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ObjectMixerEditorListRow.h"

#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/IToolTip.h"
#include "Widgets/Views/STableRow.h"

class SObjectMixerEditorListValueInput;
class SObjectMixerEditorListRowHoverWidgets;

class OBJECTMIXEREDITOR_API SObjectMixerEditorListRow : public SMultiColumnTableRow<FObjectMixerEditorListRowPtr>
{
public:
	
	SLATE_BEGIN_ARGS(SObjectMixerEditorListRow)
	{}

	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, const TWeakPtr<FObjectMixerEditorListRow> InRow);
	
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override;

	// Begin SWidget
	virtual void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	// End SWidget
	
	virtual ~SObjectMixerEditorListRow() override;	

	// FReply HandleDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	// void HandleDragLeave(const FDragDropEvent& DragDropEvent);
	// TOptional<EItemDropZone> HandleCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FObjectMixerEditorListRowPtr TargetItem);
	// FReply HandleAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, FObjectMixerEditorListRowPtr TargetItem);
	
	void FlashRow();

private:

	bool IsVisible() const;

	FSlateColor GetVisibilityIconForegroundColor() const;
	FSlateColor GetSoloIconForegroundColor() const;
	
	/** Get the brush for this widget */
	const FSlateBrush* GetVisibilityBrush() const;
	
	EVisibility GetFlashImageVisibility() const;
	FSlateColor GetFlashImageColorAndOpacity() const;

	static const FSlateBrush* GetBorderImage(const FObjectMixerEditorListRow::EObjectMixerEditorListRowType InRowType);

	TSharedRef<SWidget> GenerateCells(const FName& InColumnName, const TSharedPtr<FObjectMixerEditorListRow> PinnedItem);

	void OnPropertyChanged(const FProperty* Property, void* ContainerWithChangedProperty);
	
	TWeakPtr<FObjectMixerEditorListRow> Item;
	
	TSharedPtr<IToolTip> HoverToolTip;

	TArray<TSharedPtr<SImage>> FlashImages;

	TSet<FDelegateHandle> StructureChangeDelegateHandles;

	TSharedPtr<SObjectMixerEditorListValueInput> ValueChildInputWidget;
	
	TSharedPtr<SObjectMixerEditorListRowHoverWidgets> HoverableWidgetsPtr;

	FCurveSequence FlashAnimation;

	const float FlashAnimationDuration = 0.75f;
	const FLinearColor FlashColor = FLinearColor::White;
	
	const FSlateBrush* VisibleHoveredBrush = nullptr;
	const FSlateBrush* VisibleNotHoveredBrush = nullptr;
	const FSlateBrush* NotVisibleHoveredBrush = nullptr;
	const FSlateBrush* NotVisibleNotHoveredBrush = nullptr;

	/** The offset applied to text widgets so that the text aligns with the column header text */
	float TextBlockLeftPadding = 3.0f;
	
	bool bIsHovered = false;
};
