// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SAssetsPreviewWidget.h"

#include "AssetToolsModule.h"
#include "IAssetTypeActions.h"
#include "EditorStyleSet.h"
#include "Widgets/Images/SImage.h"
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

	/** Represents a row in the AssetPreview's tree view */
	class SAssetPreviewTableRow : public STableRow<FAssetTreeItemPtr>
	{
	public:
		SLATE_BEGIN_ARGS(SAssetPreviewTableRow) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTableView, FAssetTreeItemPtr InItem, TSharedRef< const SAssetsPreviewWidget > InPreviewWidget)
		{
			FolderOpenBrush = FEditorStyle::GetBrush("ContentBrowser.AssetTreeFolderOpen");
			FolderClosedBrush = FEditorStyle::GetBrush("ContentBrowser.AssetTreeFolderClosed");
			AssetIconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewAssetIcon");

			PreviewWidgetWeakPtr = InPreviewWidget;
			ItemWeakPtr = InItem;

			FSlateColor IconColor(FLinearColor::White);
			TWeakObjectPtr< UObject > AssetPtr = InItem->AssetPtr;
			if (AssetPtr.IsValid())
			{
				static FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
				TSharedPtr<IAssetTypeActions> AssetTypeActions = AssetToolsModule.Get().GetAssetTypeActionsForClass(AssetPtr->GetClass()).Pin();

				if (AssetTypeActions.IsValid())
				{
					IconColor = FSlateColor(AssetTypeActions->GetTypeColor());
				}
			}

			STableRow::Construct(
				STableRow::FArguments()
				.Style(FEditorStyle::Get(), "ContentBrowser.AssetListView.TableRow")
				.Cursor(EMouseCursor::Default),
				OwnerTableView);


			TSharedRef< SHorizontalBox > Widget = SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0, 0, 2, 0)
				.VAlign(VAlign_Center)
				[
					// Item icon
					SNew(SImage)
					.Image(this, &SAssetPreviewTableRow::GetIconBrush)
					.ColorAndOpacity(IconColor)
				]

				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(*(InItem->Name)))
					.Font(FEditorStyle::GetFontStyle("ContentBrowser.SourceTreeItemFont"))
					.HighlightText(PreviewWidgetWeakPtr.Pin().Get(), &SAssetsPreviewWidget::OnGetHighlightText)
				];

			SetContent(Widget);
		}

	private:
		const FSlateBrush* GetIconBrush() const
		{
			const FSlateBrush* IconBrush = AssetIconBrush;
			if (ItemWeakPtr.Pin()->IsFolder())
			{
				const bool bExpanded = PreviewWidgetWeakPtr.Pin()->GetTreeView()->IsItemExpanded(ItemWeakPtr.Pin());
				IconBrush = bExpanded ? FolderOpenBrush : FolderClosedBrush;
			}
			return IconBrush;
		}

	private:
		/** Brushes for the different folder states */
		const FSlateBrush* FolderOpenBrush;
		const FSlateBrush* FolderClosedBrush;
		const FSlateBrush* AssetIconBrush;

		FAssetTreeItemWeakPtr ItemWeakPtr;

		/** Weak reference back to the preview widget that owns us */
		TWeakPtr< const SAssetsPreviewWidget > PreviewWidgetWeakPtr;
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

		// Make sure the root dir is displayed as "Content".
		// This is more descriptive for the end user.
		TArray<FString> Tokens;
		const FString StrContent = TEXT("Content");
		SubstitutePath.ParseIntoArray(Tokens, TEXT("/"));
		if (Tokens.Num() > 0 && !Tokens[0].Equals(StrContent))
		{
			const int32 StartChar = SubstitutePath.Find(Tokens[0]);
			SubstitutePath.RemoveAt(StartChar, Tokens[0].Len(), false);
			SubstitutePath.InsertAt(StartChar, StrContent);
		}

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

		// Sort items in alphabetical order
		SortRecursive(RootItems);

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

	void SAssetsPreviewWidget::SortRecursive(TArray< FAssetTreeItemPtr >& InItems)
	{
		InItems.Sort([](const FAssetTreeItemPtr A, const FAssetTreeItemPtr B) { return A->Name.Compare(B->Name) < 0; });

		for (int ItemIndex = 0; ItemIndex < InItems.Num(); ++ItemIndex)
		{
			SortRecursive(InItems[ItemIndex]->Folders);
			SortRecursive(InItems[ItemIndex]->Assets);
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
		TSharedPtr< SAssetPreviewTableRow > TableRowWidget;

		SAssignNew(TableRowWidget, SAssetPreviewTableRow, OwnerTable, InItem, SharedThis(this));

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
