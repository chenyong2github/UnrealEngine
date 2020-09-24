// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once
#include "Widgets/Views/STreeView.h"
#include "Rigs/RigHierarchyContainer.h"

class FControlRigEditMode;
struct FRigElementKey;
class SSearchBox;
class UControlRig;
struct FRigControl;
class SControlHierarchy;

class FControlTreeElement : public TSharedFromThis<FControlTreeElement>
{
public:
	FControlTreeElement(const FRigElementKey& InKey, TWeakPtr<SControlHierarchy> InHierarchyHandler);
public:
	FRigElementKey Key;
	TArray<TSharedPtr<FControlTreeElement>> Children;
	TSharedRef<ITableRow> MakeTreeRowWidget(const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FControlTreeElement> InRigTreeElement, TSharedPtr<SControlHierarchy> InHierarchy);
};

class SControlHierarchyItem : public STableRow<TSharedPtr<FControlTreeElement>>
{
	SLATE_BEGIN_ARGS(SControlHierarchyItem) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FControlTreeElement> InRigTreeElement, TSharedPtr<SControlHierarchy> InHierarchy);

private:
	TWeakPtr<FControlTreeElement> WeakRigTreeElement;
	FText GetName() const;
};

class SControlHierarchy : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SControlHierarchy) {}
	SLATE_END_ARGS()

	~SControlHierarchy();

	void Construct(const FArguments& InArgs, UControlRig* InControlRig);
	UControlRig* GetControlRig() const;
	void SetControlRig(UControlRig* InControlRig);
private:
	/** Rebuild the tree view */
	void RefreshTreeView();

	/** Make a row widget for the table */
	TSharedRef<ITableRow> MakeTableRowWidget(TSharedPtr<FControlTreeElement> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get children for the tree */
	void HandleGetChildrenForTree(TSharedPtr<FControlTreeElement> InItem, TArray<TSharedPtr<FControlTreeElement>>& OutChildren);

	/** Set Selection Changed */
	void OnSelectionChanged(TSharedPtr<FControlTreeElement> Selection, ESelectInfo::Type SelectInfo);

private:

	/** If selecting*/
	bool bSelecting;

	/** ControlRig */
	TWeakObjectPtr<UControlRig> ControlRig;

	/** Search box widget */
	TSharedPtr<SSearchBox> FilterBox;
	FText FilterText;

	void OnFilterTextChanged(const FText& SearchText);

	/** Tree view widget */
	TSharedPtr<STreeView<TSharedPtr<FControlTreeElement>>> TreeView;

	/** Backing array for tree view */
	TArray<TSharedPtr<FControlTreeElement>> RootElements;

	/** A map for looking up items based on their key */
	TMap<FRigElementKey, TSharedPtr<FControlTreeElement>> ElementMap;

	/** A map for looking up a parent based on their key */
	TMap<FRigElementKey, FRigElementKey> ParentMap;
private:
	static TSharedPtr<FControlTreeElement> FindElement(const FRigElementKey& InElementKey, TSharedPtr<FControlTreeElement> CurrentItem);

	void SetExpansionRecursive(TSharedPtr<FControlTreeElement> InElements, bool bTowardsParent);
	void OnRigElementSelected(UControlRig* Subject, const FRigControl& Control, bool bSelected);
	FRigHierarchyContainer* GetHierarchyContainer() const;
	void AddElement(FRigElementKey InKey, FRigElementKey InParentKey = FRigElementKey());
	void AddControlElement(FRigControl InControl);
	void AddSpaceElement(FRigSpace InSpace);
public:

	friend class SControlHierarchyItem;
};
