// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Framework/Text/SlateHyperlinkRun.h"
#include "EditorUndoClient.h"
#include "Widgets/Views/SListView.h"
#include "IDetailPropertyRow.h"
#include "ToolMenuContext.h"


class AActor;
class FSCSEditorTreeNode;
class FTabManager;
class FUICommandList;
class IDetailsView;
class SBox;
class SSCSEditor;
class SSplitter;
class UBlueprint;
class FDetailsViewObjectFilter;
class IDetailTreeNode;
class IPropertyRowGenerator;
class ILevelEditor;
class UToolMenu;
class SDockingTabStack;

/** A Sample implementation of IDragDropOperation */
class FInViewportUIDragOperation : public FDragDropOperation
{
public:

	DRAG_DROP_OPERATOR_TYPE(FInViewportUIDragOperation, FDragDropOperation)

	/**
	* Invoked when the drag and drop operation has ended.
	*
	* @param bDropWasHandled   true when the drop was handled by some widget; false otherwise
	*/
	virtual void OnDrop(bool bDropWasHandled, const FPointerEvent& MouseEvent) override;

	/**
	 * Called when the mouse was moved during a drag and drop operation
	 *
	 * @param DragDropEvent    The event that describes this drag drop operation.
	 */
	virtual void OnDragged(const FDragDropEvent& DragDropEvent) override;

	/**
	 * Create this Drag and Drop Content
	 *
	 * @param InUIToBeDragged	  The UI being dragged 
	 * @param InTabGrabOffset     Where within the tab we grabbed, so we're not dragging by the upper left of the tab.
	 * @param OwnerAreaSize       Size of the DockArea at the time when we start dragging.
	 *
	 * @return a new FDockingDragOperation
	 */
	static TSharedRef<FInViewportUIDragOperation> New(const TSharedRef<class SInViewportDetails>& InUIToBeDragged, const FVector2D InTabGrabOffset, const FVector2D& OwnerAreaSize);

	/** @return location where the user grabbed within the tab as a fraction of the tab's size */
	FVector2D GetTabGrabOffsetFraction() const;

	virtual ~FInViewportUIDragOperation();

	/** @return The offset into the tab where the user grabbed in Slate Units. */
	const FVector2D GetDecoratorOffsetFromCursor();

protected:
	/** The constructor is protected, so that this class can only be instantiated as a shared pointer. */
	FInViewportUIDragOperation(const TSharedRef<class SInViewportDetails>& InUIToBeDragged, const FVector2D InTabGrabOffsetFraction, const FVector2D& OwnerAreaSize);

	/** What is actually being dragged in this operation */
	TSharedPtr<class SInViewportDetails> UIBeingDragged;

	/** Where the user grabbed the tab as a fraction of the tab's size */
	FVector2D TabGrabOffsetFraction;

	/** Decorator widget where we add temp doc tabs to */
	TSharedPtr<SDockingTabStack> CursorDecoratorStackNode;

	/** What the size of the content was when it was last shown. The user drags splitters to set this size; it is legitimate state. */
	FVector2D LastContentSize;
};

/**
 * Wraps a details panel customized for viewing actors
 */
class SInViewportDetails : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SInViewportDetails) {}
	SLATE_ARGUMENT(TSharedPtr<class SEditorViewport>, InOwningViewport)
	SLATE_ARGUMENT(TSharedPtr<ILevelEditor>, InOwningLevelEditor)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	void GenerateWidget();

	EVisibility GetHeaderVisibility() const;
	TSharedRef<SWidget> MakeDetailsWidget();
	~SInViewportDetails();

	/**
	 * Sets the objects to be viewed by the details panel
	 *
	 * @param InObjects	The objects to set
	 */
	void SetObjects(const TArray<UObject*>& InObjects, bool bForceRefresh = false);

	/** FEditorUndoClient Interface */
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	FReply StartDraggingDetails(FVector2D InTabGrabOffsetFraction, const FPointerEvent& MouseEvent);
	FDetailColumnSizeData& GetColumnSizeData() { return ColumnSizeData; }
	AActor* GetSelectedActorInEditor() const;
	UToolMenu* GetGeneratedToolbarMenu() const;

	friend class SInViewportDetailsToolbar;

private:
	AActor* GetActorContext() const;
	TSharedRef<ITableRow> GenerateListRow(TSharedPtr<IDetailTreeNode> InItem, const TSharedRef<STableViewBase>& InOwningTable);
	void OnEditorSelectionChanged(UObject* Object);

private:
	TSharedPtr<SSplitter> DetailsSplitter;
	TSharedPtr<SListView<TSharedPtr<class IDetailTreeNode>>> NodeList;
	TArray<TSharedPtr<class IDetailTreeNode>> Nodes;
	TSharedPtr<IPropertyRowGenerator> PropertyRowGenerator;
	FDetailColumnSizeData ColumnSizeData;
	TWeakPtr<class SEditorViewport> OwningViewport;
	TWeakPtr<ILevelEditor> ParentLevelEditor;
	TWeakObjectPtr<UToolMenu> GeneratedToolbarMenu;
};

class SInViewportDetailsHeader : public SCompoundWidget
{

public:
	SLATE_BEGIN_ARGS(SInViewportDetailsHeader) {}
	SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_ARGUMENT(TSharedPtr<SInViewportDetails>, Parent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
	};


	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	TSharedPtr<class FDragDropOperation> CreateDragDropOperation();

	/** The parent in-viewport details */
	TWeakPtr<SInViewportDetails> ParentPtr;
};

class SInViewportDetailsToolbar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInViewportDetailsToolbar) {}
	SLATE_ARGUMENT(TSharedPtr<SInViewportDetails>, Parent)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	FName GetQuickActionMenuName(UClass* InClass);

};