// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetailTreeNode.h"
#include "PropertyCustomizationHelpers.h"
#include "SDetailTableRowBase.h"
#include "SDetailsViewBase.h"
#include "ScopedTransaction.h"

#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Framework/Commands/Commands.h"
#include "Input/Reply.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STableViewBase.h"

class IDetailKeyframeHandler;
struct FDetailLayoutCustomization;
class SDetailSingleItemRow;

class SArrayRowHandle : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SArrayRowHandle)
	{}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_ARGUMENT(TSharedPtr<SDetailSingleItemRow>, ParentRow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	};

	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	TSharedPtr<class FArrayRowDragDropOp> CreateDragDropOperation(TSharedPtr<SDetailSingleItemRow> InRow);

private:
	TWeakPtr<SDetailSingleItemRow> ParentRow;
};

/**
 * A widget for details that span the entire tree row and have no columns                                                              
 */
class SDetailSingleItemRow : public SDetailTableRowBase
{
public:
	SLATE_BEGIN_ARGS( SDetailSingleItemRow )
		: _ColumnSizeData() {}

		SLATE_ARGUMENT( FDetailColumnSizeData, ColumnSizeData )
		SLATE_ARGUMENT( bool, AllowFavoriteSystem)
	SLATE_END_ARGS()

	/**
	 * Construct the widget
	 */
	void Construct( const FArguments& InArgs, FDetailLayoutCustomization* InCustomization, bool bHasMultipleColumns, TSharedRef<FDetailTreeNode> InOwnerTreeNode, const TSharedRef<STableViewBase>& InOwnerTableView );

	void SetIsDragDrop(bool bInIsDragDrop);

	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime);
protected:
	virtual bool OnContextMenuOpening( FMenuBuilder& MenuBuilder ) override;
private:
	void OnCopyProperty();
	void OnPasteProperty();
	bool CanPasteProperty() const;
	FSlateColor GetOuterBackgroundColor() const;
	FSlateColor GetInnerBackgroundColor() const;

	void CreateGlobalExtensionWidgets(TArray<FPropertyRowExtensionButton>& ExtensionButtons) const;
	TSharedRef<SWidget> CreateExtensionWidget() const;
	void OnResetToDefaultClicked() const;
	bool IsResetToDefaultEnabled() const;

	bool IsHighlighted() const;

	void OnFavoriteMenuToggle();
	bool CanFavorite() const;
	bool IsFavorite() const;

	/** UIActions to help populate the PropertyEditorWhitelist, which must first be turned on through FPropertyEditorWhitelist::Get().SetShouldShowMenuEntries */
	void CopyWhitelistText() const;
	void OnToggleWhitelist() const;
	bool IsWhitelistChecked() const;
	void OnToggleBlacklist() const;
	bool IsBlacklistChecked() const;


	void OnArrayDragEnter(const FDragDropEvent& DragDropEvent);
	void OnArrayDragLeave(const FDragDropEvent& DragDropEvent);
	FReply OnArrayDrop(const FDragDropEvent& DragDropEvent);
	FReply OnArrayHeaderDrop(const FDragDropEvent& DragDropEvent);

	TOptional<EItemDropZone> OnArrayCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr< FDetailTreeNode > Type);

	TSharedPtr<FPropertyNode> GetCopyPastePropertyNode() const;

	/** Checks if the current drop event is being dropped into a valid location
	 */
	bool CheckValidDrop(const TSharedPtr<SDetailSingleItemRow> RowPtr) const;
	
	TSharedPtr<FPropertyNode> GetPropertyNode() const;
	TSharedPtr<IPropertyHandle> GetPropertyHandle() const;

	bool UpdateResetToDefault();
private:
	/** Customization for this widget */
	FDetailLayoutCustomization* Customization;
	FDetailWidgetRow WidgetRow;
	bool bAllowFavoriteSystem;
	bool bIsHoveredDragTarget;
	bool bIsDragDropObject;
	bool bCachedResetToDefaultEnabled;
	TSharedPtr<FPropertyNode> SwappablePropertyNode;
	TSharedPtr<SButton> ExpanderArrow;
};

class FArrayRowDragDropOp : public FDecoratedDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FArrayRowDragDropOp, FDecoratedDragDropOp)

	FArrayRowDragDropOp(TSharedPtr<SDetailSingleItemRow> InRow);

	TSharedPtr<SWidget> DecoratorWidget;

	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return DecoratorWidget;
	}

	TWeakPtr<SDetailSingleItemRow> Row;
	bool IsValidTarget;

private:
	FText GetDecoratorText() const;
	const FSlateBrush* GetDecoratorIcon() const;
};
