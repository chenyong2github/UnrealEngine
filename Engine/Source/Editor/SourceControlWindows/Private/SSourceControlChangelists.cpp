// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlChangelists.h"

#include "EditorStyleSet.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/SOverlay.h"

#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "SourceControlOperations.h"


#define LOCTEXT_NAMESPACE "SourceControlChangelist"

struct FChangelistTreeItem : public IChangelistTreeItem
{
	FChangelistTreeItem(FSourceControlChangelistStateRef InChangelistState)
		: ChangelistState(InChangelistState)
	{
		Type = IChangelistTreeItem::Changelist;
	}

	FText GetDisplayText() const
	{
		return ChangelistState->GetDisplayText();
	}

	FText GetDescriptionText() const
	{
		return ChangelistState->GetDescriptionText();
	}

	FSourceControlChangelistStateRef ChangelistState;
};

struct FFileTreeItem : public IChangelistTreeItem
{
	FFileTreeItem(FSourceControlStateRef InFileState)
		: FileState(InFileState)
	{
		Type = IChangelistTreeItem::File;
	}

	FText GetDisplayText() const
	{
		FString Filename = FileState->GetFilename();
		return FText::FromString(Filename);
	}

	FSourceControlStateRef FileState;
};

SSourceControlChangelistsWidget::SSourceControlChangelistsWidget()
{
}

void SSourceControlChangelistsWidget::Construct(const FArguments& InArgs)
{
	TreeView = CreateTreeviewWidget();

	Refresh();

	ChildSlot
		[
			SNew(SScrollBorder, TreeView.ToSharedRef())
			[
				TreeView.ToSharedRef()
			]
		];
}

void SSourceControlChangelistsWidget::Refresh()
{
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	ISourceControlProvider& SourceControlProvider = SourceControlModule.GetProvider();

	UpdatePendingChangelistsOperation = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
	UpdatePendingChangelistsOperation->SetUpdateAllChangelists(true);
	UpdatePendingChangelistsOperation->SetUpdateFilesStates(true);

	SourceControlProvider.Execute(UpdatePendingChangelistsOperation.ToSharedRef(), EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateSP(this, &SSourceControlChangelistsWidget::OnChangelistsStatusUpdated));
}

void SSourceControlChangelistsWidget::OnChangelistsStatusUpdated(const FSourceControlOperationRef& InOperation, ECommandResult::Type InType)
{
	ISourceControlModule& SourceControlModule = ISourceControlModule::Get();
	ISourceControlProvider& SourceControlProvider = SourceControlModule.GetProvider();

	TArray<FSourceControlChangelistStateRef> ChangelistsStates;
	SourceControlProvider.GetState(SourceControlProvider.GetChangelists(EStateCacheUsage::Use), ChangelistsStates, EStateCacheUsage::Use);

	ChangelistsNodes.Reset(ChangelistsStates.Num());

	for (FSourceControlChangelistStateRef ChangelistState : ChangelistsStates)
	{
		FChangelistTreeItemRef ChangelistTreeItem = MakeShareable(new FChangelistTreeItem(ChangelistState));

		for (FSourceControlStateRef FileRef : ChangelistState->GetFilesStates())
		{
			FChangelistTreeItemRef FileTreeItem = MakeShareable(new FFileTreeItem(FileRef));
			ChangelistTreeItem->AddChild(FileTreeItem);
		}

		ChangelistsNodes.Add(ChangelistTreeItem);
	}

	TreeView->RequestTreeRefresh();
}

bool SSourceControlChangelistsWidget::OnIsSelectableOrNavigable(FChangelistTreeItemPtr InItem) const
{
	if (!InItem)
	{
		return false;
	}

	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	// Prevent selecting changelists and files at the same time.
	if (!SelectedItems.IsEmpty() && SelectedItems[0]->GetTreeItemType() != InItem->GetTreeItemType())
	{
		return false;
	}

	return true;
}

TSharedRef<SChangelistTree> SSourceControlChangelistsWidget::CreateTreeviewWidget()
{
	return SAssignNew(TreeView, SChangelistTree)
		.ItemHeight(24.0f)
		.TreeItemsSource(&ChangelistsNodes)
		.OnGenerateRow(this, &SSourceControlChangelistsWidget::OnGenerateRow)
		.OnGetChildren(this, &SSourceControlChangelistsWidget::OnGetChildren)
		.SelectionMode(ESelectionMode::Multi)
		.OnIsSelectableOrNavigable(this, &SSourceControlChangelistsWidget::OnIsSelectableOrNavigable)
		.HeaderRow
		(
			SNew(SHeaderRow)
			+ SHeaderRow::Column("Change")
			.DefaultLabel(LOCTEXT("Change", "Change"))
			.FillWidth(0.2f)
			+ SHeaderRow::Column("Description")
			.DefaultLabel(LOCTEXT("Description", "Description"))
		);
}


class SChangelistTableRow : public SMultiColumnTableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SChangelistTableRow)
		: _TreeItemToVisualize()
	{}
	SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
	{
		TreeItem = static_cast<FChangelistTreeItem*>(InArgs._TreeItemToVisualize.Get());

		auto Args = FSuperRowType::FArguments();
		SMultiColumnTableRow<FChangelistTreeItemPtr>::Construct(Args, InOwner);
	}

	// SMultiColumnTableRow overrides
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		if (ColumnName == TEXT("Change"))
		{
			const FSlateBrush* IconBrush = FEditorStyle::GetBrush("SourceControl.Changelist");

			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SExpanderArrow, SharedThis(this))
					]

				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SImage)
						.Image(IconBrush)
					]

				+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(2.0f, 0.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SChangelistTableRow::GetChangelistText)
					];
		}
		else if (ColumnName == TEXT("Description"))
		{
			return SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SChangelistTableRow::GetChangelistDescriptionText)
				];
		}
		else
		{
			return SNullWidget::NullWidget;
		}
	}

	FText GetChangelistText() const
	{
		return TreeItem->GetDisplayText();
	}

	FText GetChangelistDescriptionText() const
	{
		FString DescriptionString = TreeItem->GetDescriptionText().ToString();
		DescriptionString.ReplaceInline(TEXT("\n"), TEXT(" "));
		DescriptionString.TrimEndInline();
		return FText::FromString(DescriptionString);
	}

private:
	/** The info about the widget that we are visualizing. */
	FChangelistTreeItem* TreeItem;
};

class SFileTableRow : public STableRow<FChangelistTreeItemPtr>
{
public:
	SLATE_BEGIN_ARGS(SFileTableRow)
		: _TreeItemToVisualize()
	{}
	SLATE_ARGUMENT(FChangelistTreeItemPtr, TreeItemToVisualize)
	SLATE_END_ARGS()

public:
	/**
	* Construct child widgets that comprise this widget.
	*
	* @param InArgs Declaration from which to construct this widget.
	*/
	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
	{
		TreeItem = static_cast<FFileTreeItem*>(InArgs._TreeItemToVisualize.Get());

		const FSlateBrush* IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewAssetIcon");

		// Make icon overlays (eg, SCC and dirty status) a reasonable size in relation to the icon size (note: it is assumed this icon is square)
		const float ICON_SCALING_FACTOR = 0.7f;
		const float IconOverlaySize = IconBrush->ImageSize.X * ICON_SCALING_FACTOR;

		ChildSlot
		[
			SNew(SHorizontalBox)

			// Icon
			+SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(40, 0, 4, 0)
				[
					SNew(SOverlay)
				
					// The actual icon
					+SOverlay::Slot()
					[
						SNew(SImage)
						.Image( IconBrush )
					]

					// Source control state
					+SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					[
						SNew(SBox)
						.WidthOverride(IconOverlaySize)
						.HeightOverride(IconOverlaySize)
						[
							SNew(SImage)
							.Image(this, &SFileTableRow::GetSCCStateImage)
						]
					]
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SFileTableRow::GetDisplayText)
				]
		];

		STableRow<FChangelistTreeItemPtr>::ConstructInternal(
			STableRow::FArguments()
			.ShowSelection(true),
			InOwner
		);
	}

	FText GetDisplayText() const
	{
		return TreeItem->GetDisplayText();
	}

	const FSlateBrush* GetSCCStateImage() const
	{
		return FEditorStyle::GetBrush(TreeItem->FileState->GetSmallIconName());
	}

private:
	/** The info about the widget that we are visualizing. */
	FFileTreeItem* TreeItem;
};

TSharedRef<ITableRow> SSourceControlChangelistsWidget::OnGenerateRow(FChangelistTreeItemPtr InTreeItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	switch (InTreeItem->GetTreeItemType())
	{
	case IChangelistTreeItem::Changelist:
		return SNew(SChangelistTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem);

	case IChangelistTreeItem::File:
		return SNew(SFileTableRow, OwnerTable)
			.TreeItemToVisualize(InTreeItem);

	default:
		check(false);
	};

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable);
}

void SSourceControlChangelistsWidget::OnGetChildren(FChangelistTreeItemPtr InParent, TArray<FChangelistTreeItemPtr>& OutChildren)
{
	for (auto& Child : InParent->GetChildren())
	{
		// Should never have bogus entries in this list
		check(Child.IsValid());
		OutChildren.Add(Child);
	}
}



#undef LOCTEXT_NAMESPACE