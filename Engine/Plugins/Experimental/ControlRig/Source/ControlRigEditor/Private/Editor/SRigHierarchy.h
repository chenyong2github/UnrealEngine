// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/STreeView.h"
#include "Hierarchy.h"
#include "EditorUndoClient.h"
#include "DragAndDrop/GraphNodeDragDropOp.h"

class SRigHierarchy;
class FControlRigEditor;
class SSearchBox;
class FUICommandList;
class UControlRigBlueprint;
struct FAssetData;
class FMenuBuilder;
class SRigHierarchyItem;

DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnRenameBone, const FName& /*OldName*/, const FName& /*NewName*/);
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FOnVerifyBoneNameChanged, const FName& /*OldName*/, const FName& /*NewName*/, FText& /*OutErrorMessage*/);

/** An item in the tree */
class FRigTreeBone : public TSharedFromThis<FRigTreeBone>
{
public:
	FRigTreeBone(const FName& InBone, TWeakPtr<SRigHierarchy> InHierarchyHandler);
public:
	/** Bone Data to display */
	FName CachedBone;
	TArray<TSharedPtr<FRigTreeBone>> Children;

	TSharedRef<ITableRow> MakeTreeRowWidget(TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigTreeBone> InRigTreeBone, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigHierarchy> InHierarchy);

	void RequestRename();

	/** Delegate for when the context menu requests a rename */
	DECLARE_DELEGATE(FOnRenameRequested);
	FOnRenameRequested OnRenameRequested;
};

class FRigHierarchyDragDropOp : public FGraphNodeDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FRigHierarchyDragDropOp, FGraphNodeDragDropOp)

	static TSharedRef<FRigHierarchyDragDropOp> New(TArray<FName> InBoneNames);

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

	/** @return true if this drag operation contains property paths */
	bool HasBoneNames() const
	{
		return BoneNames.Num() > 0;
	}

	/** @return The property paths from this drag operation */
	const TArray<FName>& GetBoneNames() const
	{
		return BoneNames;
	}

	FString GetJoinedBoneNames() const;

private:

	/** Data for the property paths this item represents */
	TArray<FName> BoneNames;
};

 class SRigHierarchyItem : public STableRow<TSharedPtr<FRigTreeBone>>
{
	SLATE_BEGIN_ARGS(SRigHierarchyItem) {}
		/** Callback when the text is committed. */
		SLATE_EVENT(FOnRenameBone, OnRenameBone)
		/** Called whenever the text is changed interactively by the user */
		SLATE_EVENT(FOnVerifyBoneNameChanged, OnVerifyBoneNameChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigTreeBone> InControlRigTreeBone, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigHierarchy> InHierarchy);
	void OnNameCommitted(const FText& InText, ETextCommit::Type InCommitType) const;
	bool OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage);

private:
	TWeakPtr<FRigTreeBone> WeakRigTreeBone;
	TWeakPtr<FUICommandList> WeakCommandList;
	TWeakPtr<FControlRigEditor> ControlRigEditor;

	FOnRenameBone OnRenameBone;
	FOnVerifyBoneNameChanged OnVerifyBoneNameChanged;

	FText GetName() const;
};

/** Widget allowing editing of a control rig's structure */
class SRigHierarchy : public SCompoundWidget, public FEditorUndoClient
{
public:
	SLATE_BEGIN_ARGS(SRigHierarchy) {}
	SLATE_END_ARGS()

	~SRigHierarchy();

	void Construct(const FArguments& InArgs, TSharedRef<FControlRigEditor> InControlRigEditor);

private:
	/** Bind commands that this widget handles */
	void BindCommands();

	/** Rebuild the tree view */
	void RefreshTreeView();

	/** Make a row widget for the table */
	TSharedRef<ITableRow> MakeTableRowWidget(TSharedPtr<FRigTreeBone> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get children for the tree */
	void HandleGetChildrenForTree(TSharedPtr<FRigTreeBone> InItem, TArray<TSharedPtr<FRigTreeBone>>& OutChildren);

	/** Check whether we can deleting the selected item(s) */
	bool CanDeleteItem() const;

	/** Delete Item */
	void HandleDeleteItem();

	/** Delete Item */
	void HandleNewItem();

	/** Check whether we can deleting the selected item(s) */
	bool CanDuplicateItem() const;

	/** Delete Item */
	void HandleDuplicateItem();

	/** Check whether we can deleting the selected item(s) */
	bool CanRenameItem() const;

	/** Delete Item */
	void HandleRenameItem();

	/** Set Selection Changed */
	void OnSelectionChanged(TSharedPtr<FRigTreeBone> Selection, ESelectInfo::Type SelectInfo);

	TSharedPtr< SWidget > CreateContextMenu();

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// reply to a drag operation
	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

private:
	/** Our owning control rig editor */
	TWeakPtr<FControlRigEditor> ControlRigEditor;

	/** Search box widget */
	TSharedPtr<SSearchBox> FilterBox;
	FText FilterText;

	void OnFilterTextChanged(const FText& SearchText);

	/** Tree view widget */
	TSharedPtr<STreeView<TSharedPtr<FRigTreeBone>>> TreeView;

	/** Backing array for tree view */
	TArray<TSharedPtr<FRigTreeBone>> RootBones;

	/** Backing array for tree view (filtered, displayed) */
	TArray<TSharedPtr<FRigTreeBone>> FilteredRootBones;

	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprint;
	
	/** Command list we bind to */
	TSharedPtr<FUICommandList> CommandList;

	bool IsMultiSelected() const;
	bool IsSingleSelected() const;

	FRigHierarchy* GetHierarchy() const;
	FRigHierarchy* GetInstanceHierarchy() const;

	void ImportHierarchy(const FAssetData& InAssetData);
	void CreateImportMenu(FMenuBuilder& MenuBuilder);
	bool ShouldFilterOnImport(const FAssetData& AssetData) const;

	FName CreateUniqueName(const FName& InBaseName) const;

	void SetExpansionRecursive(TSharedPtr<FRigTreeBone> InBones);

	void ClearDetailPanel() const;
	void SelectBone(const FName& BoneName) const;
public:
	bool RenameBone(const FName& OldName, const FName& NewName);
	bool OnVerifyNameChanged(const FName& OldName, const FName& NewName, FText& OutErrorMessage);

	friend class SRigHierarchyItem;
};