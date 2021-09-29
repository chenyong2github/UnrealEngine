// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Input/Reply.h"
#include "InterchangePipelineBase.h"
#include "Styling/SlateColor.h"
#include "UObject/GCObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STreeView.h"

class IDetailsView;

class FInterchangePipelineStacksTreeNodeItem : protected FGCObject
{
public:
	FInterchangePipelineStacksTreeNodeItem()
	{
		StackName = NAME_None;
		Pipeline = nullptr;
	}

	/* FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("FInterchangePipelineStacksTreeNodeItem");
	}

	//This name is use only when this item represent a stack name
	FName StackName;

	//Pipeline is nullptr when the node represent a stack name
	UInterchangePipelineBase* Pipeline;

	TArray<TSharedPtr<FInterchangePipelineStacksTreeNodeItem>> Childrens;
};

DECLARE_DELEGATE_TwoParams(FOnPipelineConfigurationSelectionChanged, TSharedPtr<FInterchangePipelineStacksTreeNodeItem>, ESelectInfo::Type)

class SInterchangePipelineStacksTreeView : public STreeView< TSharedPtr<FInterchangePipelineStacksTreeNodeItem>>
{
public:
	~SInterchangePipelineStacksTreeView();

	SLATE_BEGIN_ARGS(SInterchangePipelineStacksTreeView)
		: _OnSelectionChangedDelegate()
	{}
		SLATE_EVENT(FOnPipelineConfigurationSelectionChanged, OnSelectionChangedDelegate)
	SLATE_END_ARGS()

	/** Construct this widget */
	void Construct(const FArguments& InArgs);
	TSharedRef< ITableRow > OnGenerateRowPipelineConfigurationTreeView(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Item, const TSharedRef< STableViewBase >& OwnerTable);
	void OnGetChildrenPipelineConfigurationTreeView(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> InParent, TArray< TSharedPtr<FInterchangePipelineStacksTreeNodeItem> >& OutChildren);

	FReply OnExpandAll();
	FReply OnCollapseAll();

	const TArray<TSharedPtr<FInterchangePipelineStacksTreeNodeItem>>& GetRootNodeArray() const { return RootNodeArray; }
protected:
	/** Delegate to invoke when selection changes. */
	FOnPipelineConfigurationSelectionChanged OnSelectionChangedDelegate;

	/** the elements we show in the tree view */
	TArray<TSharedPtr<FInterchangePipelineStacksTreeNodeItem>> RootNodeArray;

	/** Open a context menu for the current selection */
	TSharedPtr<SWidget> OnOpenContextMenu();
	void SetHasDefaultStack(FName NewDefaultStackValue);
	void RecursiveSetExpand(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Node, bool ExpandState);
	void OnTreeViewSelectionChanged(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Item, ESelectInfo::Type SelectionType);
};


enum class ECloseEventType : uint8
{
	Cancel,
	ImportAll,
	Import
};

class SInterchangePipelineConfigurationDialog : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SInterchangePipelineConfigurationDialog)
		: _OwnerWindow()
	{}

		SLATE_ARGUMENT(TWeakPtr<SWindow>, OwnerWindow)
	SLATE_END_ARGS()

public:

	SInterchangePipelineConfigurationDialog();
	~SInterchangePipelineConfigurationDialog();

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }

	void ClosePipelineConfiguration(const ECloseEventType CloseEventType);

	FReply OnCloseDialog(const ECloseEventType CloseEventType)
	{
		ClosePipelineConfiguration(CloseEventType);
		return FReply::Handled();
	}

	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	bool IsCanceled() { return bCanceled; }
	bool IsImportAll() { return bImportAll; }
private:
	TWeakPtr< SWindow > OwnerWindow;

	TSharedRef<SBox> SpawnPipelineConfiguration();
	void OnSelectionChanged(TSharedPtr<FInterchangePipelineStacksTreeNodeItem> Item, ESelectInfo::Type SelectionType);

	void RecursiveSavePipelineSettings(const TSharedPtr<FInterchangePipelineStacksTreeNodeItem>& ParentNode, const int32 PipelineIndex) const;

	//Graph Inspector UI elements
	TSharedPtr<SInterchangePipelineStacksTreeView> PipelineConfigurationTreeView;
	TSharedPtr<IDetailsView> PipelineConfigurationDetailsView;

	bool bCanceled = false;
	bool bImportAll = false;
};

