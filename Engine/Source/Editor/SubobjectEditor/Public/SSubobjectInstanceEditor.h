// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "SSubobjectEditor.h"

/**
* This is the editor for subobjects within the level editor that
* works mainly with component and actor instances. 
*/
class SUBOBJECTEDITOR_API SSubobjectInstanceEditor final : public SSubobjectEditor
{
private:	
	SLATE_BEGIN_ARGS(SSubobjectInstanceEditor)
        : _ObjectContext(nullptr)
        , _AllowEditing(true)
        , _OnSelectionUpdated()
		{}

		SLATE_ATTRIBUTE(UObject*, ObjectContext)
	    SLATE_ATTRIBUTE(bool, AllowEditing)
	    SLATE_EVENT(FOnSelectionUpdated, OnSelectionUpdated)
	    SLATE_EVENT(FOnItemDoubleClicked, OnItemDoubleClicked)
		SLATE_EVENT(FSimpleDelegate, OnObjectReplaced)
	
	SLATE_END_ARGS()
	
	void Construct(const FArguments& InArgs);

protected:
	
	// SSubobjectEditor interface
	virtual void OnDeleteNodes() override;
	virtual void CopySelectedNodes() override;
	virtual void OnDuplicateComponent() override;
	virtual void PasteNodes() override;
	
	virtual void OnAttachToDropAction(FSubobjectEditorTreeNodePtrType DroppedOn, const TArray<FSubobjectEditorTreeNodePtrType>& DroppedNodePtrs) override;
    virtual void OnDetachFromDropAction(const TArray<FSubobjectEditorTreeNodePtrType>& DroppedNodePtrs) override;
    virtual void OnMakeNewRootDropAction(FSubobjectEditorTreeNodePtrType DroppedNodePtr) override;
    virtual void PostDragDropAction(bool bRegenerateTreeNodes) override;

    /** Builds a context menu popup for dropping a child node onto the scene root node */
    virtual TSharedPtr<SWidget> BuildSceneRootDropActionMenu(FSubobjectEditorTreeNodePtrType DroppedOntoNodePtr, FSubobjectEditorTreeNodePtrType DroppedNodePtr) override;
	virtual FSubobjectDataHandle AddNewSubobject(const FSubobjectDataHandle& ParentHandle, UClass* NewClass, UObject* AssetOverride, FText& OutFailReason, TUniquePtr<FScopedTransaction> InOngoingTransaction) override;
	virtual void PopulateContextMenuImpl(UToolMenu* InMenu, TArray<FSubobjectEditorTreeNodePtrType>& InSelectedItems, bool bIsChildActorSubtreeNodeSelected) override;

public:
	virtual FSlateColor GetColorTintForIcon(FSubobjectEditorTreeNodePtrType Node) const override;
	// End of SSubobjectEditor

	/** Delegate to invoke when objects within the Subobject tree are replaced (eg, via re-instancing from a BP compile) */
	FSimpleDelegate OnObjectReplaced;

};