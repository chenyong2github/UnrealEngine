// Copyright Epic Games, Inc. All Rights Reserved.

#include "Views/TreeViews/DisplayClusterConfiguratorTreeBuilder.h"

#include "Interfaces/Views/TreeViews/IDisplayClusterConfiguratorTreeItem.h"

void FDisplayClusterConfiguratorTreeBuilderOutput::Add(const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem, const FName& InParentName, const FName& InParentType, bool bAddToHead)
{
	Add(InItem, InParentName, TArray<FName, TInlineAllocator<1>>({ InParentType }), bAddToHead);
}

void FDisplayClusterConfiguratorTreeBuilderOutput::Add(const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem, const FName& InParentName, TArrayView<const FName> InParentTypes, bool bAddToHead)
{
	TSharedPtr<IDisplayClusterConfiguratorTreeItem> ParentItem = Find(InParentName, InParentTypes);
	if (ParentItem.IsValid())
	{
		InItem->SetParent(ParentItem);

		if (bAddToHead)
		{
			ParentItem->GetChildren().Insert(InItem, 0);
		}
		else
		{
			ParentItem->GetChildren().Add(InItem);
		}
	}
	else
	{
		if (bAddToHead)
		{
			Items.Insert(InItem, 0);
		}
		else
		{
			Items.Add(InItem);
		}
	}

	LinearItems.Add(InItem);
}

TSharedPtr<IDisplayClusterConfiguratorTreeItem> FDisplayClusterConfiguratorTreeBuilderOutput::Find(const FName& InName, const FName& InType) const
{
	return Find(InName, TArray<FName, TInlineAllocator<1>>({ InType }));
}

TSharedPtr<IDisplayClusterConfiguratorTreeItem> FDisplayClusterConfiguratorTreeBuilderOutput::Find(const FName& InName, TArrayView<const FName> InTypes) const
{
	for (const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& Item : LinearItems)
	{
		bool bPassesType = (InTypes.Num() == 0);
		for (const FName& TypeName : InTypes)
		{
			if (Item->IsOfTypeByName(TypeName))
			{
				bPassesType = true;
				break;
			}
		}

		if (bPassesType && Item->GetAttachName() == InName)
		{
			return Item;
		}
	}

	return nullptr;
}

FDisplayClusterConfiguratorTreeBuilder::FDisplayClusterConfiguratorTreeBuilder(const TSharedRef<FDisplayClusterConfiguratorToolkit>& InToolkit)
	: ToolkitPtr(InToolkit)
{
}

void FDisplayClusterConfiguratorTreeBuilder::Initialize(const TSharedRef<class IDisplayClusterConfiguratorViewTree>& InConfiguratorTree, FOnFilterConfiguratorTreeItem InOnFilterTreeItem)
{
	ConfiguratorTreePtr = InConfiguratorTree;
	OnFilterTreeItem = InOnFilterTreeItem;
}

void FDisplayClusterConfiguratorTreeBuilder::Filter(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>& InItems, TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>& OutFilteredItems)
{
	OutFilteredItems.Empty();

	for (const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& Item : InItems)
	{
		if (InArgs.TextFilter.IsValid() && InArgs.bFlattenHierarchyOnFilter)
		{
			FilterRecursive(InArgs, Item, OutFilteredItems);
		}
		else
		{
			EDisplayClusterConfiguratorTreeFilterResult FilterResult = FilterRecursive(InArgs, Item, OutFilteredItems);
			if (FilterResult != EDisplayClusterConfiguratorTreeFilterResult::Hidden)
			{
				OutFilteredItems.Add(Item);
			}

			Item->SetFilterResult(FilterResult);
		}
	}
}

EDisplayClusterConfiguratorTreeFilterResult FDisplayClusterConfiguratorTreeBuilder::FilterRecursive(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem, TArray<TSharedPtr<IDisplayClusterConfiguratorTreeItem>>& OutFilteredItems)
{
	EDisplayClusterConfiguratorTreeFilterResult FilterResult = EDisplayClusterConfiguratorTreeFilterResult::Shown;

	InItem->GetFilteredChildren().Empty();

	if (InArgs.TextFilter.IsValid() && InArgs.bFlattenHierarchyOnFilter)
	{
		FilterResult = FilterItem(InArgs, InItem);
		InItem->SetFilterResult(FilterResult);

		if (FilterResult != EDisplayClusterConfiguratorTreeFilterResult::Hidden)
		{
			OutFilteredItems.Add(InItem);
		}

		for (const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& Item : InItem->GetChildren())
		{
			FilterRecursive(InArgs, Item, OutFilteredItems);
		}
	}
	else
	{
		// check to see if we have any children that pass the filter
		EDisplayClusterConfiguratorTreeFilterResult DescendantsFilterResult = EDisplayClusterConfiguratorTreeFilterResult::Hidden;
		for (const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& Item : InItem->GetChildren())
		{
			EDisplayClusterConfiguratorTreeFilterResult ChildResult = FilterRecursive(InArgs, Item, OutFilteredItems);
			if (ChildResult != EDisplayClusterConfiguratorTreeFilterResult::Hidden)
			{
				InItem->GetFilteredChildren().Add(Item);
			}
			if (ChildResult > DescendantsFilterResult)
			{
				DescendantsFilterResult = ChildResult;
			}
		}

		FilterResult = FilterItem(InArgs, InItem);
		if (DescendantsFilterResult > FilterResult)
		{
			FilterResult = EDisplayClusterConfiguratorTreeFilterResult::ShownDescendant;
		}

		InItem->SetFilterResult(FilterResult);
	}

	return FilterResult;
}

EDisplayClusterConfiguratorTreeFilterResult FDisplayClusterConfiguratorTreeBuilder::FilterItem(const FDisplayClusterConfiguratorTreeFilterArgs& InArgs, const TSharedPtr<IDisplayClusterConfiguratorTreeItem>& InItem)
{
	return OnFilterTreeItem.Execute(InArgs, InItem);
}
