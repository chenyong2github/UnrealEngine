// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Types/SlateEnums.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STreeView.h"


namespace AssetPreviewWidget
{
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnSelectionChanged, TSet< UObject* > /** Selected objects */)

	using FAssetTreeItemPtr = TSharedPtr< struct FAssetTreeItem >;
	using FAssetTreeItemWeakPtr = TWeakPtr< struct FAssetTreeItem >;

	class SAssetsPreviewWidget : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SAssetsPreviewWidget) {}
		SLATE_END_ARGS()

	public:
		void Construct(const FArguments& InArgs);

		void SetAssetsList(const TArray< TWeakObjectPtr< UObject > >& InAssetsList, const FString& InPathToReplace, const FString& InSubstitutePath);
		void ClearAssetList();

		FOnSelectionChanged& OnSelectionChanged() { return OnSelectionChangedDelegate; };
	
		FText OnGetHighlightText() const;

		const TSharedPtr< STreeView< FAssetTreeItemPtr > > GetTreeView() const
		{
			return TreeView;
		}

	private:

		friend struct FAssetTreeItem;

		void FilterAssetsNames();
		void ExpandAllFolders();
		void ExpandFolderRecursive(FAssetTreeItemPtr InItem);

		void SortRecursive(TArray< FAssetTreeItemPtr >& InItems);

		TArray< FString > GetItemsName(TWeakObjectPtr< UObject > Asset) const;

		TSharedRef< class ITableRow > MakeRowWidget(FAssetTreeItemPtr InItem, const TSharedRef< class STableViewBase >& OwnerTable) const;
		void OnGetChildren(FAssetTreeItemPtr InParent, TArray< FAssetTreeItemPtr >& OutChildren) const;

		void OnSearchBoxChanged(const FText& InSearchText);
		void OnSearchBoxCommitted(const FText& InSearchText, ETextCommit::Type CommitInfo);

		void OnSetExpansionRecursive(FAssetTreeItemPtr InTreeNode, bool bInIsItemExpanded);

		void OnSelectionChangedInternal(FAssetTreeItemPtr ItemSelected, ESelectInfo::Type SelectionType);

		TArray< FAssetTreeItemPtr > RootItems;
		TArray< FAssetTreeItemPtr > FilteredRootItems;

		TSharedPtr< STreeView< FAssetTreeItemPtr > > TreeView;

		FText FilterText;
		FString PathToReplace;
		FString SubstitutePath;

		FOnSelectionChanged OnSelectionChangedDelegate;
	};
}
