// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "IKRetargetEditorController.h"
#include "SBaseHierarchyTreeView.h"
#include "DragAndDrop/DecoratedDragDropOp.h"
#include "Widgets/SCompoundWidget.h"

class SIKRetargetHierarchy;

class FIKRetargetTreeElement : public TSharedFromThis<FIKRetargetTreeElement>
{
public:
	
	FIKRetargetTreeElement(const FText& InKey, const TSharedRef<FIKRetargetEditorController>& InEditorController);

	TSharedRef<ITableRow> MakeTreeRowWidget(
		TSharedRef<FIKRetargetEditorController> InEditorController,
		const TSharedRef<STableViewBase>& InOwnerTable,
		TSharedRef<FIKRetargetTreeElement> InRigTreeElement,
		TSharedRef<FUICommandList> InCommandList,
		TSharedPtr<SIKRetargetHierarchy> InHierarchy);

	FText Key;
	TSharedPtr<FIKRetargetTreeElement> Parent;
	TArray<TSharedPtr<FIKRetargetTreeElement>> Children;
	FName Name;

private:
	
	TWeakPtr<FIKRetargetEditorController> EditorController;
};

class SIKRetargetHierarchyItem : public STableRow<TSharedPtr<FIKRetargetTreeElement>>
{
public:
	
	void Construct(
		const FArguments& InArgs,
		TSharedRef<FIKRetargetEditorController> InEditorController,
		const TSharedRef<STableViewBase>& OwnerTable,
		TSharedRef<FIKRetargetTreeElement> InTreeElement,
		TSharedRef<FUICommandList> InCommandList,
		TSharedPtr<SIKRetargetHierarchy> InHierarchy);

private:
	
	FText GetName() const;

	TWeakPtr<FIKRetargetTreeElement> WeakTreeElement;
	TWeakPtr<FIKRetargetEditorController> EditorController;
	TWeakPtr<SIKRetargetHierarchy> HierarchyView;
};

typedef SBaseHierarchyTreeView<FIKRetargetTreeElement> SIKRetargetHierarchyTreeView;

class SIKRetargetHierarchy : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SIKRetargetHierarchy) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedRef<FIKRetargetEditorController> InEditorController);

	void ShowItemAfterSelection(FName ItemKey);

private:
	
	/** centralized editor controls (facilitate cross-communication between multiple UI elements)*/
	TWeakPtr<FIKRetargetEditorController> EditorController;
	
	/** command list we bind to */
	TSharedPtr<FUICommandList> CommandList;
	
	/** tree view widget */
	TSharedPtr<SIKRetargetHierarchyTreeView> TreeView;
	TArray<TSharedPtr<FIKRetargetTreeElement>> RootElements;
	TArray<TSharedPtr<FIKRetargetTreeElement>> AllElements;
	
	/** tree view callbacks */
	void RefreshTreeView(bool IsInitialSetup=false);
	TSharedRef<ITableRow> MakeTableRowWidget(TSharedPtr<FIKRetargetTreeElement> InItem, const TSharedRef<STableViewBase>& OwnerTable);
	void HandleGetChildrenForTree(TSharedPtr<FIKRetargetTreeElement> InItem, TArray<TSharedPtr<FIKRetargetTreeElement>>& OutChildren);
	void OnSelectionChanged(TSharedPtr<FIKRetargetTreeElement> Selection, ESelectInfo::Type SelectInfo);
	void OnItemClicked(TSharedPtr<FIKRetargetTreeElement> InItem);
	void OnItemDoubleClicked(TSharedPtr<FIKRetargetTreeElement> InItem);
	void OnSetExpansionRecursive(TSharedPtr<FIKRetargetTreeElement> InItem, bool bShouldBeExpanded);
	void SetExpansionRecursive(TSharedPtr<FIKRetargetTreeElement> InElement, bool bTowardsParent, bool bShouldBeExpanded);
	/** END tree view callbacks */

	friend SIKRetargetHierarchyItem;
	friend FIKRetargetEditorController;
};
