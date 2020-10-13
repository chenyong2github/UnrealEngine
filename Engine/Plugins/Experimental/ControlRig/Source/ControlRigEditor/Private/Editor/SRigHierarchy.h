// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/Views/STreeView.h"
#include "Rigs/RigHierarchyContainer.h"
#include "EditorUndoClient.h"
#include "DragAndDrop/GraphNodeDragDropOp.h"
#include "Engine/SkeletalMesh.h"
#include "SRigHierarchy.generated.h"

class SRigHierarchy;
class FControlRigEditor;
class SSearchBox;
class FUICommandList;
class UControlRigBlueprint;
struct FAssetData;
class FMenuBuilder;
class SRigHierarchyItem;

DECLARE_DELEGATE_RetVal_TwoParams(bool, FOnRenameElement, const FRigElementKey& /*OldKey*/, const FName& /*NewName*/);
DECLARE_DELEGATE_RetVal_ThreeParams(bool, FOnVerifyElementNameChanged, const FRigElementKey& /*OldKey*/, const FName& /*NewName*/, FText& /*OutErrorMessage*/);

/** An item in the tree */
class FRigTreeElement : public TSharedFromThis<FRigTreeElement>
{
public:
	FRigTreeElement(const FRigElementKey& InKey, TWeakPtr<SRigHierarchy> InHierarchyHandler);
public:
	/** Element Data to display */
	FRigElementKey Key;
	TArray<TSharedPtr<FRigTreeElement>> Children;

	TSharedRef<ITableRow> MakeTreeRowWidget(TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& InOwnerTable, TSharedRef<FRigTreeElement> InRigTreeElement, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigHierarchy> InHierarchy);

	void RequestRename();

	/** Delegate for when the context menu requests a rename */
	DECLARE_DELEGATE(FOnRenameRequested);
	FOnRenameRequested OnRenameRequested;
};

class FRigElementHierarchyDragDropOp : public FGraphNodeDragDropOp
{
public:
	DRAG_DROP_OPERATOR_TYPE(FRigElementHierarchyDragDropOp, FGraphNodeDragDropOp)

	static TSharedRef<FRigElementHierarchyDragDropOp> New(const TArray<FRigElementKey>& InElements);

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override;

	/** @return true if this drag operation contains property paths */
	bool HasElements() const
	{
		return Elements.Num() > 0;
	}

	/** @return The property paths from this drag operation */
	const TArray<FRigElementKey>& GetElements() const
	{
		return Elements;
	}

	FString GetJoinedElementNames() const;

private:

	/** Data for the property paths this item represents */
	TArray<FRigElementKey> Elements;
};

 class SRigHierarchyItem : public STableRow<TSharedPtr<FRigTreeElement>>
{
	SLATE_BEGIN_ARGS(SRigHierarchyItem) {}
		/** Callback when the text is committed. */
		SLATE_EVENT(FOnRenameElement, OnRenameElement)
		/** Called whenever the text is changed interactively by the user */
		SLATE_EVENT(FOnVerifyElementNameChanged, OnVerifyElementNameChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TSharedPtr<FControlRigEditor> InControlRigEditor, const TSharedRef<STableViewBase>& OwnerTable, TSharedRef<FRigTreeElement> InRigTreeElement, TSharedRef<FUICommandList> InCommandList, TSharedPtr<SRigHierarchy> InHierarchy);
	void OnNameCommitted(const FText& InText, ETextCommit::Type InCommitType) const;
	bool OnVerifyNameChanged(const FText& InText, FText& OutErrorMessage);

private:
	TWeakPtr<FRigTreeElement> WeakRigTreeElement;
	TWeakPtr<FUICommandList> WeakCommandList;
	TWeakPtr<FControlRigEditor> ControlRigEditor;

	FOnRenameElement OnRenameElement;
	FOnVerifyElementNameChanged OnVerifyElementNameChanged;

	FText GetName() const;
};

USTRUCT()
struct FRigHierarchyImportSettings
{
	GENERATED_BODY()

	FRigHierarchyImportSettings()
	: Mesh(nullptr)
	{}

	UPROPERTY(EditAnywhere, Category = "Hierachy Import")
	USkeletalMesh* Mesh;
};

class SRigHierarchyTreeView : public STreeView<TSharedPtr<FRigTreeElement>>
{
public:

	virtual ~SRigHierarchyTreeView() {}

	virtual FReply OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent) override
	{
		FReply Reply = STreeView<TSharedPtr<FRigTreeElement>>::OnFocusReceived(MyGeometry, InFocusEvent);

		LastClickCycles = FPlatformTime::Cycles();

		return Reply;
	}

	uint32 LastClickCycles = 0;

	/** Save a snapshot of the internal map that tracks item expansion before tree reconstruction */
	void SaveAndClearSparseItemInfos()
	{
		OldSparseItemInfos = SparseItemInfos;
		ClearExpandedItems();
	}

	/** Restore the expansion infos map from the saved snapshot after tree reconstruction */
	void RestoreSparseItemInfos(TSharedPtr<FRigTreeElement> ItemPtr)
	{
		for (const auto& Pair : OldSparseItemInfos)
		{
			if (Pair.Key->Key == ItemPtr->Key)
			{
				// the SparseItemInfos now reference the new element, but keep the same expansion state
				SparseItemInfos.Add(ItemPtr, Pair.Value);
				break;
			}
		}
	}

private:
	/** A temporary snapshot of the SparseItemInfos in STreeView, used during SRigHierarchy::RefreshTreeView() */
	TSparseItemMap OldSparseItemInfos;
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

	/** SWidget interface */
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent);

	/** Rebuild the tree view */
	void RefreshTreeView();

	/** Make a row widget for the table */
	TSharedRef<ITableRow> MakeTableRowWidget(TSharedPtr<FRigTreeElement> InItem, const TSharedRef<STableViewBase>& OwnerTable);

	/** Get children for the tree */
	void HandleGetChildrenForTree(TSharedPtr<FRigTreeElement> InItem, TArray<TSharedPtr<FRigTreeElement>>& OutChildren);

	/** Check whether we can deleting the selected item(s) */
	bool CanDeleteItem() const;

	/** Delete Item */
	void HandleDeleteItem();

	/** Create a new item */
	void HandleNewItem(ERigElementType InElementType);

	/** Check whether we can deleting the selected item(s) */
	bool CanDuplicateItem() const;

	/** Duplicate Item */
	void HandleDuplicateItem();

	/** Mirror Item */
	void HandleMirrorItem();

	/** Check whether we can deleting the selected item(s) */
	bool CanRenameItem() const;

	/** Delete Item */
	void HandleRenameItem();

	bool CanPasteItems() const;
	bool CanCopyOrPasteItems() const;
	void HandleCopyItems();
	void HandlePasteItems();
	void HandlePasteLocalTransforms();
	void HandlePasteGlobalTransforms();

	/** Set Selection Changed */
	void OnSelectionChanged(TSharedPtr<FRigTreeElement> Selection, ESelectInfo::Type SelectInfo);

	TSharedPtr< SWidget > CreateContextMenu();
	void OnItemClicked(TSharedPtr<FRigTreeElement> InItem);
	void OnItemDoubleClicked(TSharedPtr<FRigTreeElement> InItem);
	void OnSetExpansionRecursive(TSharedPtr<FRigTreeElement> InItem, bool bShouldBeExpanded);

	// FEditorUndoClient
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;

	// reply to a drag operation
	FReply OnDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);
	TOptional<EItemDropZone> OnCanAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FRigTreeElement> TargetItem);
	FReply OnAcceptDrop(const FDragDropEvent& DragDropEvent, EItemDropZone DropZone, TSharedPtr<FRigTreeElement> TargetItem);

private:

	void FillContextMenu(class FMenuBuilder& MenuBuilder);
	TSharedPtr<FUICommandList> GetContextMenuCommands();

	/** Our owning control rig editor */
	TWeakPtr<FControlRigEditor> ControlRigEditor;

	/** Search box widget */
	TSharedPtr<SSearchBox> FilterBox;
	FText FilterText;

	EVisibility IsToolbarVisible() const;
	EVisibility IsSearchbarVisible() const;
	FReply OnImportSkeletonClicked();
	void OnFilterTextChanged(const FText& SearchText);

	/** Tree view widget */
	TSharedPtr<SRigHierarchyTreeView> TreeView;

	/** Backing array for tree view */
	TArray<TSharedPtr<FRigTreeElement>> RootElements;

	/** A map for looking up items based on their key */
	TMap<FRigElementKey, TSharedPtr<FRigTreeElement>> ElementMap;

	/** A map for looking up a parent based on their key */
	TMap<FRigElementKey, FRigElementKey> ParentMap;

	TWeakObjectPtr<UControlRigBlueprint> ControlRigBlueprint;
	
	/** Command list we bind to */
	TSharedPtr<FUICommandList> CommandList;

	bool IsMultiSelected() const;
	bool IsSingleSelected() const;
	bool IsSingleBoneSelected() const;
	bool IsSingleSpaceSelected() const;
	bool IsControlSelected() const;
	bool IsControlOrSpaceSelected() const;

	FRigHierarchyContainer* GetHierarchyContainer() const;
	FRigHierarchyContainer* GetDebuggedHierarchyContainer() const;

	void ImportHierarchy(const FAssetData& InAssetData);
	void CreateImportMenu(FMenuBuilder& MenuBuilder);
	void CreateRefreshMenu(FMenuBuilder& MenuBuilder);
	bool ShouldFilterOnImport(const FAssetData& AssetData) const;
	void RefreshHierarchy(const FAssetData& InAssetData);

	void HandleResetTransform(bool bSelectionOnly);
	void HandleResetInitialTransform();
	void HandleResetSpace();
	void HandleSetInitialTransformFromCurrentTransform();
	void HandleSetInitialTransformFromClosestBone();
	void HandleFrameSelection();
	void HandleControlBoneOrSpaceTransform();
	void HandleUnparent();
	bool FindClosestBone(const FVector& Point, FName& OutRigElementName, FTransform& OutGlobalTransform) const;

	FName CreateUniqueName(const FName& InBaseName, ERigElementType InElementType) const;

	void SetExpansionRecursive(TSharedPtr<FRigTreeElement> InElement, bool bTowardsParent, bool bShouldBeExpanded);

	void ClearDetailPanel() const;

	bool bIsChangingRigHierarchy;
	void OnRigElementAdded(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnRigElementRemoved(FRigHierarchyContainer* Container, const FRigElementKey& InKey);
	void OnRigElementRenamed(FRigHierarchyContainer* Container, ERigElementType ElementType, const FName& InOldName, const FName& InNewName);
	void OnRigElementReparented(FRigHierarchyContainer* Container, const FRigElementKey& InKey, const FName& InOldParentName, const FName& InNewParentName);
	void OnRigElementSelected(FRigHierarchyContainer* Container, const FRigElementKey& InKey, bool bSelected);
	void HandleRefreshEditorFromBlueprint(UControlRigBlueprint* InBlueprint);

	static TSharedPtr<FRigTreeElement> FindElement(const FRigElementKey& InElementKey, TSharedPtr<FRigTreeElement> CurrentItem);
	void AddElement(FRigElementKey InKey, FRigElementKey InParentKey = FRigElementKey());
	void AddSpacerElement();
	void AddBoneElement(FRigBone InBone);
	void AddControlElement(FRigControl InControl);
	void AddSpaceElement(FRigSpace InSpace);
	void ReparentElement(FRigElementKey InKey, FRigElementKey InParentKey);

public:
	bool RenameElement(const FRigElementKey& OldKey, const FName& NewName);
	bool OnVerifyNameChanged(const FRigElementKey& OldKey, const FName& NewName, FText& OutErrorMessage);

	friend class SRigHierarchyItem;
};