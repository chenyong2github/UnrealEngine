// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SAssetsPreviewWidget.h"

#include "EditorStyleSet.h"
#include "Widgets/Text/STextBlock.h"
#include "SAssetSearchBox.h"
#include "Misc/PackageName.h"

#include "UObject/UObjectBaseUtility.h"

#define LOCTEXT_NAMESPACE "AssetPreviewWidget"

namespace AssetPreviewWidget
{
	struct FAssetTreeItem
	{
		// This is used to accelerate the construction of the tree in the set assets function
		TMap< FString, FAssetTreeItemPtr> NameToFolder;

		// Childrens
		TArray< FAssetTreeItemPtr > Folders;
		TArray< FAssetTreeItemPtr > Assets;

		FString Name;
		TWeakObjectPtr< UObject > AssetPtr;

		TWeakPtr< SAssetsPreviewWidget > OwnerWeakPtr;

		// This value is cache for the last result for the filter function
		bool bPassedFilter;

		void AddFolder(FAssetTreeItemPtr Folder)
		{
			if (Folder)
			{
				NameToFolder.Add(Folder->Name, Folder);
				Folders.Add(Folder);
			}
		}

		bool IsFolder() const
		{
			if (Folders.Num() > 0 || Assets.Num() > 0)
			{
				return true;
			}
			return false;
		}

		bool Filter(const FText& FilterText)
		{
			bPassedFilter = false;
			if (FilterText.IsEmpty())
			{
				bPassedFilter = true;
			}

			if (IsFolder())
			{
				// A folder pass the filter if one of his child pass the filter
				for (FAssetTreeItemPtr& Folder : Folders)
				{
					bPassedFilter = Folder->Filter(FilterText) || bPassedFilter;
				}
				for (FAssetTreeItemPtr& Asset : Assets)
				{
					bPassedFilter = Asset->Filter(FilterText) || bPassedFilter;
				}
			}
			else if (!bPassedFilter && OwnerWeakPtr.IsValid())
			{
				TArray< FString > FilterStrings;
				FilterText.ToString().ParseIntoArray(FilterStrings, TEXT(" "));

				TSharedPtr< SAssetsPreviewWidget > OwnerWidget = OwnerWeakPtr.Pin();
				TArray< FString > ItemsName = OwnerWidget->GetItemsName(AssetPtr);

				// All the keywords must be pass for at least one of the items name in the hierarchy
				bool bPassKeyWord = false;
				for (const FString& KeyWord : FilterStrings)
				{
					bPassKeyWord = false;
					for (const FString& ItemName : ItemsName)
					{
						if (ItemName.Contains(KeyWord))
						{
							bPassKeyWord = true;
							break;
						}
					}
					if (!bPassKeyWord)
					{
						break;
					}
				}

				bPassedFilter = bPassKeyWord;

			}

			return bPassedFilter;
		}
	};

	void SAssetsPreviewWidget::Construct(const FArguments& InArgs)
	{
		ChildSlot
		[
			SNew(SVerticalBox)

			// Search and commands
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 0, 0, 2)
			[
				SNew(SHorizontalBox)
				// Search
				+ SHorizontalBox::Slot()
				.Padding(0, 1, 0, 0)
				.FillWidth(1.0f)
				[
					SNew(SAssetSearchBox)
					.OnTextChanged(this, &SAssetsPreviewWidget::OnSearchBoxChanged)
					.OnTextCommitted(this, &SAssetsPreviewWidget::OnSearchBoxCommitted)
					.DelayChangeNotificationsWhileTyping(true)
					.HintText(LOCTEXT("SearchHint", "Search..."))
				]
			]

			+ SVerticalBox::Slot()
			//.AutoHeight()
			.Padding(2.f)
			[
				SAssignNew(TreeView, STreeView< FAssetTreeItemPtr >)
				.SelectionMode( ESelectionMode::Single )
				.TreeItemsSource(&FilteredRootItems)
				.OnGenerateRow(this, &SAssetsPreviewWidget::MakeRowWidget)
				.OnSetExpansionRecursive(this, &SAssetsPreviewWidget::OnSetExpansionRecursive)
				.OnGetChildren(this, &SAssetsPreviewWidget::OnGetChildren)
				.OnSelectionChanged(this, &SAssetsPreviewWidget::OnSelectionChangedInternal)
			]
		];
	}

	void SAssetsPreviewWidget::SetAssetsList(const TArray< TWeakObjectPtr< UObject > >& InAssetsList, const FString& InPathToReplace, const FString& InSubstitutePath)
	{
		PathToReplace = InPathToReplace;
		SubstitutePath = InSubstitutePath;
		RootItems.Empty();
		TMap< FString, FAssetTreeItemPtr > NamesToRootItem;


		for (const TWeakObjectPtr< UObject >& Asset : InAssetsList)
		{
			if (Asset.Get())
			{
				TArray< FString > ItemsName = GetItemsName(Asset);

				if (ItemsName.Num() > 0)
				{
					FAssetTreeItemPtr LastParent;

					if (ItemsName.Num() > 1)
					{
						// Manage the root item
						{
							FString ItemName = MoveTemp(ItemsName[0]);
							FAssetTreeItemPtr RootItem;
							if (FAssetTreeItemPtr* PtrToTreeItemPtr = NamesToRootItem.Find(ItemName))
							{
								RootItem = *PtrToTreeItemPtr;
							}
							else
							{
								RootItem = MakeShared<FAssetTreeItem>();
								RootItem->Name = ItemName;
								RootItem->OwnerWeakPtr = SharedThis(this);
								RootItems.Add(RootItem);
								NamesToRootItem.Add(MoveTemp(ItemName), RootItem);
							}
							LastParent = MoveTemp(RootItem);
						}

						//Manage the folders to the asset
						{
							const int32 ItemsNameLenghtMinusOne = ItemsName.Num() - 1;
							for (int32 i = 1; i < ItemsNameLenghtMinusOne; i++)
							{
								FString ItemName = MoveTemp(ItemsName[i]);
								FAssetTreeItemPtr FolderItem;
								if (FAssetTreeItemPtr* PtrToTreeItemPtr = LastParent->NameToFolder.Find(ItemName))
								{
									FolderItem = *PtrToTreeItemPtr;
								}
								else
								{
									FolderItem = MakeShared<FAssetTreeItem>();
									FolderItem->Name = MoveTemp(ItemName);
									FolderItem->OwnerWeakPtr = SharedThis(this);
									LastParent->AddFolder(FolderItem);
								}

								LastParent = FolderItem;
							}
						}
					}

					// Create the asset item
					{
						FAssetTreeItemPtr AssetItem = MakeShared<FAssetTreeItem>();
						AssetItem->Name = MoveTemp(ItemsName.Last());
						AssetItem->AssetPtr = Asset;
						AssetItem->OwnerWeakPtr = SharedThis(this);
						LastParent->Assets.Add(AssetItem);
					}
				}
			}
		}

		FilterAssetsNames();
	}

	void SAssetsPreviewWidget::ClearAssetList()
	{
		RootItems.Empty();
		FilterAssetsNames();
	}

	void SAssetsPreviewWidget::FilterAssetsNames()
	{
		FilteredRootItems.Empty();

		for (FAssetTreeItemPtr& Item : RootItems)
		{
			if (Item->Filter(FilterText))
			{
				FilteredRootItems.Add(Item);
			}
		}

		TreeView->RequestListRefresh();

		ExpandAllFolders();
	}

	void SAssetsPreviewWidget::ExpandAllFolders()
	{
		for (FAssetTreeItemPtr& Item : FilteredRootItems)
		{
			ExpandFolderRecursive(Item);
		}
	}

	void SAssetsPreviewWidget::ExpandFolderRecursive(FAssetTreeItemPtr InItem)
	{
		TreeView->SetItemExpansion(InItem, true);
		for (FAssetTreeItemPtr& Item : InItem->Folders)
		{
			ExpandFolderRecursive(Item);
		}
	}

	TArray< FString > SAssetsPreviewWidget::GetItemsName(TWeakObjectPtr< UObject > Asset) const
	{
		TArray< FString > ItemsName;
		FString AssetSubPath = Asset->GetPathName(nullptr);
		if(AssetSubPath.RemoveFromStart(PathToReplace) && !SubstitutePath.IsEmpty())
		{
			AssetSubPath = SubstitutePath / AssetSubPath;
		}
		AssetSubPath.ReplaceCharInline(TEXT('/'), TEXT('.'));
		AssetSubPath.ParseIntoArray(ItemsName, TEXT("."), true);
		return ItemsName;
	}

	TSharedRef< ITableRow > SAssetsPreviewWidget::MakeRowWidget(FAssetTreeItemPtr InItem, const TSharedRef< STableViewBase >& OwnerTable) const
	{
		TSharedPtr< STableRow< FAssetTreeItemPtr > > TableRowWidget;

		SAssignNew(TableRowWidget, STableRow< FAssetTreeItemPtr >, OwnerTable)
			.Style(FEditorStyle::Get(), "ContentBrowser.AssetListView.TableRow")
			.Cursor(EMouseCursor::Default);

		TSharedRef< STextBlock > Item = SNew(STextBlock)
			//.TextStyle( FEditorStyle::Get(), "LargeText" )
			.Text(FText::FromString(*(InItem->Name)))
			.Font(FEditorStyle::GetFontStyle("ContentBrowser.SourceTreeItemFont"))
			.HighlightText(this, &SAssetsPreviewWidget::OnGetHighlightText);

		TableRowWidget->SetContent(Item);

		return TableRowWidget.ToSharedRef();
	}

	void SAssetsPreviewWidget::OnGetChildren(FAssetTreeItemPtr InParent, TArray< FAssetTreeItemPtr >& OutChildren) const
	{
		for (FAssetTreeItemPtr& Folder : InParent->Folders)
		{
			if (Folder->bPassedFilter)
			{
				OutChildren.Add(Folder);
			}
		}

		for (FAssetTreeItemPtr& Asset : InParent->Assets)
		{
			if (Asset->bPassedFilter)
			{
				OutChildren.Add(Asset);
			}
		}

	}

	void SAssetsPreviewWidget::OnSearchBoxChanged(const FText& InSearchText)
	{
		FilterText = InSearchText;
		FilterAssetsNames();
	}

	void SAssetsPreviewWidget::OnSearchBoxCommitted(const FText& InSearchText, ETextCommit::Type CommitInfo)
	{
		FilterText = InSearchText;
		FilterAssetsNames();
	}

	FText SAssetsPreviewWidget::OnGetHighlightText() const
	{
		return FilterText;
	}


	void SAssetsPreviewWidget::OnSetExpansionRecursive(FAssetTreeItemPtr InTreeNode, bool bInIsItemExpanded)
	{
		if (InTreeNode.IsValid())
		{
			TreeView->SetItemExpansion(InTreeNode, bInIsItemExpanded);

			for (FAssetTreeItemPtr SubFolder : InTreeNode->Folders)
			{
				OnSetExpansionRecursive(SubFolder, bInIsItemExpanded);
			}
		}
	}

	void SAssetsPreviewWidget::OnSelectionChangedInternal(FAssetTreeItemPtr ItemSelected, ESelectInfo::Type SelectionType)
	{
		if ( ItemSelected )
		{	
			TSet< UObject* > Selection;
			Selection.Add( ItemSelected->AssetPtr.Get() );
			OnSelectionChanged().Broadcast( Selection );
		}
	}
}

#undef LOCTEXT_NAMESPACE
