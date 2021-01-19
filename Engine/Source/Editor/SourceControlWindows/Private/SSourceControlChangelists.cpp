// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlChangelists.h"

#include "EditorStyleSet.h"

#include "Algo/Transform.h"

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SScrollBorder.h"
#include "Widgets/SOverlay.h"

#include "ISourceControlProvider.h"
#include "ISourceControlModule.h"
#include "SourceControlOperations.h"
#include "ToolMenus.h"
#include "Widgets/Images/SLayeredImage.h"
#include "SSourceControlDescription.h"
#include "SourceControlWindows.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"


#define LOCTEXT_NAMESPACE "SourceControlChangelist"

//////////////////////////////

static TSharedRef<SWidget> GetSCCFileWidget(FSourceControlStateRef InFileState)
{
	const FSlateBrush* IconBrush = FEditorStyle::GetBrush("ContentBrowser.ColumnViewAssetIcon");

	// Make icon overlays (eg, SCC and dirty status) a reasonable size in relation to the icon size (note: it is assumed this icon is square)
	const float ICON_SCALING_FACTOR = 0.7f;
	const float IconOverlaySize = IconBrush->ImageSize.X * ICON_SCALING_FACTOR;

	return SNew(SOverlay)

		// The actual icon
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(IconBrush)
		]

	// Source control state
	+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.WidthOverride(IconOverlaySize)
			.HeightOverride(IconOverlaySize)
			[
				SNew(SLayeredImage, InFileState->GetIcon())
			]
		];
}

struct FSCCFileDragDropOp : public FDragDropOperation
{
	DRAG_DROP_OPERATOR_TYPE(FSCCFileDragDropOp, FDragDropOperation);

	using FDragDropOperation::Construct;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		return GetSCCFileWidget(Files[0]);
	}

	TArray<FSourceControlStateRef> Files;
};

//////////////////////////////

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
	// Register delegates
	ISourceControlModule& SCCModule = ISourceControlModule::Get();
	SCCModule.RegisterProviderChanged(FSourceControlProviderChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlProviderChanged));
	SourceControlStateChangedDelegateHandle = SCCModule.GetProvider().RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlStateChanged));

	TreeView = CreateTreeviewWidget();

	ChildSlot
	[
		SNew(SScrollBorder, TreeView.ToSharedRef())
		.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([]()->EVisibility { return ISourceControlModule::Get().IsEnabled() ? EVisibility::Visible : EVisibility::Hidden; })))
		[
			TreeView.ToSharedRef()
		]
	];

	bShouldRefresh = true;
}

void SSourceControlChangelistsWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bShouldRefresh)
	{
		if (ISourceControlModule::Get().IsEnabled())
		{
			RequestRefresh();
			bShouldRefresh = false;
		}
		else
		{
			// No provider available, clear changelist tree
			ClearChangelistsTree();
		}
	}
}

void SSourceControlChangelistsWidget::RequestRefresh()
{
	if (ISourceControlModule::Get().IsEnabled())
	{
		TSharedRef<FUpdatePendingChangelistsStatus, ESPMode::ThreadSafe> UpdatePendingChangelistsOperation = ISourceControlOperation::Create<FUpdatePendingChangelistsStatus>();
		UpdatePendingChangelistsOperation->SetUpdateAllChangelists(true);
		UpdatePendingChangelistsOperation->SetUpdateFilesStates(true);

		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
		SourceControlProvider.Execute(UpdatePendingChangelistsOperation, EConcurrency::Asynchronous, FSourceControlOperationComplete::CreateSP(this, &SSourceControlChangelistsWidget::OnChangelistsStatusUpdated));
	}
	else
	{
		// No provider available, clear changelist tree
		ClearChangelistsTree();
	}
}

void SSourceControlChangelistsWidget::ClearChangelistsTree()
{
	if (!ChangelistsNodes.IsEmpty())
	{
		ChangelistsNodes.Empty();
		TreeView->RequestTreeRefresh();
	}
}

void SSourceControlChangelistsWidget::Refresh()
{
	if (ISourceControlModule::Get().IsEnabled())
	{
		ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

		TArray<FSourceControlChangelistRef> Changelists = SourceControlProvider.GetChangelists(EStateCacheUsage::Use);

		TArray<FSourceControlChangelistStateRef> ChangelistsStates;
		SourceControlProvider.GetState(Changelists, ChangelistsStates, EStateCacheUsage::Use);

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
	else
	{
		ClearChangelistsTree();
	}
}

void SSourceControlChangelistsWidget::OnSourceControlProviderChanged(ISourceControlProvider& OldProvider, ISourceControlProvider& NewProvider)
{
	OldProvider.UnregisterSourceControlStateChanged_Handle(SourceControlStateChangedDelegateHandle);
	SourceControlStateChangedDelegateHandle = NewProvider.RegisterSourceControlStateChanged_Handle(FSourceControlStateChanged::FDelegate::CreateSP(this, &SSourceControlChangelistsWidget::OnSourceControlStateChanged));

	bShouldRefresh = true;
}

void SSourceControlChangelistsWidget::OnSourceControlStateChanged()
{
	Refresh();
}

void SSourceControlChangelistsWidget::OnChangelistsStatusUpdated(const FSourceControlOperationRef& InOperation, ECommandResult::Type InType)
{
	Refresh();
}

void SChangelistTree::Private_SetItemSelection(FChangelistTreeItemPtr TheItem, bool bShouldBeSelected, bool bWasUserDirected)
{
	bool bAllowSelectionChange = true;

	if (bShouldBeSelected && !SelectedItems.IsEmpty())
	{
		// Prevent selecting changelists and files at the same time.
		FChangelistTreeItemPtr CurrentlySelectedItem = (*SelectedItems.begin());
		if (TheItem->GetTreeItemType() != CurrentlySelectedItem->GetTreeItemType())
		{
			bAllowSelectionChange = false;
		}
		// Prevent selecting items that don't share the same root
		else if (TheItem->GetParent() != CurrentlySelectedItem->GetParent())
		{
			bAllowSelectionChange = false;
		}
	}

	if (bAllowSelectionChange)
	{
		STreeView::Private_SetItemSelection(TheItem, bShouldBeSelected, bWasUserDirected);
	}
}

FSourceControlChangelistStatePtr SSourceControlChangelistsWidget::GetCurrentChangelistState()
{
	if (!TreeView)
	{
		return nullptr;
	}

	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	if (SelectedItems.Num() != 1 || SelectedItems[0]->GetTreeItemType() != IChangelistTreeItem::Changelist)
	{
		return nullptr;
	}
	else
	{
		return StaticCastSharedPtr<FChangelistTreeItem>(SelectedItems[0])->ChangelistState;
	}
}

FSourceControlChangelistPtr SSourceControlChangelistsWidget::GetCurrentChangelist()
{
	FSourceControlChangelistStatePtr ChangelistState = GetCurrentChangelistState();
	return ChangelistState ? (FSourceControlChangelistPtr)(ChangelistState->GetChangelist()) : nullptr;
}

FSourceControlChangelistStatePtr SSourceControlChangelistsWidget::GetChangelistStateFromFileSelection()
{
	if (!TreeView)
	{
		return nullptr;
	}

	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	if (SelectedItems.Num() == 0 || SelectedItems[0]->GetTreeItemType() != IChangelistTreeItem::File)
	{
		return nullptr;
	}
	else
	{
		return StaticCastSharedPtr<FChangelistTreeItem>(SelectedItems[0]->GetParent())->ChangelistState;
	}
}

FSourceControlChangelistPtr SSourceControlChangelistsWidget::GetChangelistFromFileSelection()
{
	FSourceControlChangelistStatePtr ChangelistState = GetChangelistStateFromFileSelection();
	return ChangelistState ? (FSourceControlChangelistPtr)(ChangelistState->GetChangelist()) : nullptr;
}

TArray<FString> SSourceControlChangelistsWidget::GetSelectedFiles()
{
	TArray<FChangelistTreeItemPtr> SelectedItems = TreeView->GetSelectedItems();

	if (SelectedItems.Num() == 0 || SelectedItems[0]->GetTreeItemType() != IChangelistTreeItem::File)
	{
		return TArray<FString>();
	}
	else
	{
		TArray<FString> Files;
		for (FChangelistTreeItemPtr Item : SelectedItems)
		{
			Files.Add(StaticCastSharedPtr<FFileTreeItem>(Item)->FileState->GetFilename());
		}

		return Files;
	}
}

void SSourceControlChangelistsWidget::OnNewChangelist()
{
	FText ChangelistDescription;
	bool bOk = GetChangelistDescription(
		nullptr,
		LOCTEXT("SourceControl.Changelist.New.Title", "New Changelist..."),
		LOCTEXT("SourceControl.Changelist.New.Label", "Enter a description for the changelist:"),
		ChangelistDescription);

	if (!bOk)
	{
		return;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto NewChangelistOperation = ISourceControlOperation::Create<FNewChangelist>();
	NewChangelistOperation->SetDescription(ChangelistDescription);

	SourceControlProvider.Execute(NewChangelistOperation);
	Refresh();
}

void SSourceControlChangelistsWidget::OnDeleteChangelist()
{
	if (GetCurrentChangelist() == nullptr)
	{
		return;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	SourceControlProvider.Execute(ISourceControlOperation::Create<FDeleteChangelist>(), GetCurrentChangelist());
	Refresh();
}

bool SSourceControlChangelistsWidget::CanDeleteChangelist()
{
	FSourceControlChangelistStatePtr Changelist = GetCurrentChangelistState();
	return Changelist != nullptr && Changelist->GetFilesStates().Num() == 0 && Changelist->GetShelvedFilesStates().Num() == 0;
}

void SSourceControlChangelistsWidget::OnEditChangelist()
{
	FSourceControlChangelistStatePtr ChangelistState = GetCurrentChangelistState();

	if(ChangelistState == nullptr)
	{
		return;
	}

	FText NewChangelistDescription = ChangelistState->GetDescriptionText();

	bool bOk = GetChangelistDescription(
		nullptr,
		LOCTEXT("SourceControl.Changelist.New.Title", "Edit Changelist..."),
		LOCTEXT("SourceControl.Changelist.New.Label", "Enter a new description for the changelist:"),
		NewChangelistDescription);

	if (!bOk)
	{
		return;
	}

	auto EditChangelistOperation = ISourceControlOperation::Create<FEditChangelist>();
	EditChangelistOperation->SetDescription(NewChangelistDescription);

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	SourceControlProvider.Execute(EditChangelistOperation, ChangelistState->GetChangelist());

	Refresh();
}

void SSourceControlChangelistsWidget::OnRevertUnchanged()
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	auto RevertUnchangedOperation = ISourceControlOperation::Create<FRevertUnchanged>();

	if(GetCurrentChangelist() != nullptr)
	{
		// Operation on full changelist
		SourceControlProvider.Execute(RevertUnchangedOperation, GetCurrentChangelist());
	}
	else
	{
		// Operation on files in a changelist
		SourceControlProvider.Execute(RevertUnchangedOperation, GetChangelistFromFileSelection(), GetSelectedFiles());
	}

	Refresh();
}

void SSourceControlChangelistsWidget::OnRevert()
{
	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();

	auto RevertOperation = ISourceControlOperation::Create<FRevert>();

	if (GetCurrentChangelist() != nullptr)
	{
		// Operation on full changelist
		SourceControlProvider.Execute(RevertOperation, GetCurrentChangelist());
	}
	else
	{
		// Operation on files in a changelist
		SourceControlProvider.Execute(RevertOperation, GetChangelistFromFileSelection(), GetSelectedFiles());
	}

	Refresh();
}

void SSourceControlChangelistsWidget::OnSubmitChangelist()
{
	FSourceControlChangelistPtr Changelist = GetCurrentChangelist();
	
	if (!Changelist)
	{
		return;
	}

	ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
	auto SubmitChangelistOperation = ISourceControlOperation::Create<FCheckIn>();

	SourceControlProvider.Execute(SubmitChangelistOperation, Changelist);
	Refresh();	
}

bool SSourceControlChangelistsWidget::CanSubmitChangelist()
{
	FSourceControlChangelistStatePtr Changelist = GetCurrentChangelistState();
	return Changelist != nullptr && Changelist->GetFilesStates().Num() >= 0 && Changelist->GetShelvedFilesStates().Num() == 0;
}

void SSourceControlChangelistsWidget::OnLocateFile()
{ 
	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

	TArray<UObject*> AssetsToSync;

	TArray<FString> SelectedFiles = GetSelectedFiles();
	AssetsToSync.Reserve(SelectedFiles.Num());

	for (const FString& SelectedFile : SelectedFiles)
	{
		FString AssetPackageName;

		if (FPackageName::TryConvertFilenameToLongPackageName(SelectedFile, AssetPackageName))
		{
			UPackage* AssetPackage = LoadPackage(nullptr, *AssetPackageName, LOAD_ForDiff | LOAD_DisableCompileOnLoad);

			// grab the asset from the package - we assume asset name matches file name
			FString AssetName = FPaths::GetBaseFilename(SelectedFile);
			UObject* SelectedAsset = FindObject<UObject>(AssetPackage, *AssetName);

			if (SelectedAsset)
			{
				AssetsToSync.Add(SelectedAsset);
			}
		}
	}

	if (AssetsToSync.Num() > 0)
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowserModule.Get().SyncBrowserToAssets(AssetsToSync, true);
	}
}

bool SSourceControlChangelistsWidget::CanLocateFile()
{
	return GetSelectedFiles().Num() > 0;
}

void SSourceControlChangelistsWidget::OnShowHistory()
{
	TArray<FString> SelectedFiles = GetSelectedFiles();
	if (SelectedFiles.Num() > 0)
	{
		FSourceControlWindows::DisplayRevisionHistory(SelectedFiles);
	}
}

void SSourceControlChangelistsWidget::OnDiffAgainstDepot()
{
	TArray<FString> SelectedFiles = GetSelectedFiles();
	if (SelectedFiles.Num() > 0)
	{
		FSourceControlWindows::DiffAgainstWorkspace(SelectedFiles[0]);
	} 
}

bool SSourceControlChangelistsWidget::CanDiffAgainstDepot()
{
	return GetSelectedFiles().Num() == 1;
}

TSharedPtr<SWidget> SSourceControlChangelistsWidget::OnOpenContextMenu()
{
	UToolMenus* ToolMenus = UToolMenus::Get();
	static const FName MenuName = "SourceControl.ChangelistContextMenu";
	if (!ToolMenus->IsMenuRegistered(MenuName))
	{
		ToolMenus->RegisterMenu(MenuName);
	}

	// Build up the menu for a selection
	FToolMenuContext Context;
	UToolMenu* Menu = ToolMenus->GenerateMenu(MenuName, Context);
		
	bool bHasSelectedChangelist = (GetCurrentChangelist() != nullptr);
	bool bHasEmptySelection = (GetCurrentChangelist() == nullptr && GetSelectedFiles().Num() == 0);

	FToolMenuSection& Section = Menu->AddSection("Source Control");
	
	// This should appear only on change lists
	if (bHasSelectedChangelist)
	{
		Section.AddMenuEntry("SubmitChangelist", LOCTEXT("SourceControl_SubmitChangelist", "Submit Changelist"), LOCTEXT("SourceControl_SubmitChangeslit_Tooltip", "Submits a changelist"), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnSubmitChangelist),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanSubmitChangelist)));
	}

	// This can appear on both files & changelist
	if (!bHasEmptySelection)
	{
		Section.AddMenuEntry("RevertUnchanged", LOCTEXT("SourceControl_RevertUnchanged", "Revert Unchanged"), LOCTEXT("SourceControl_Revert_Unchanged_Tooltip", "Reverts unchanged files & changelists"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnRevertUnchanged)));

		Section.AddMenuEntry("Revert", LOCTEXT("SourceControl_Revert", "Revert Files"), LOCTEXT("SourceControl_Revert_Tooltip", "Reverts all files in the changelist or from the selection"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnRevert)));
	}

	if (bHasEmptySelection || bHasSelectedChangelist)
	{
		Section.AddSeparator("Changelists");
	}

	// This should appear only if we have no selection
	if (bHasEmptySelection)
	{
		Section.AddMenuEntry("NewChangelist", LOCTEXT("SourceControl_NewChangelist", "New Changelist"), LOCTEXT("SourceControl_NewChangelist_Tooltip", "Creates an empty changelist"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnNewChangelist)));
	}

	if (bHasSelectedChangelist)
	{
		Section.AddMenuEntry("EditChangelist", LOCTEXT("SourceControl_EditChangelist", "Edit Changelist"), LOCTEXT("SourceControl_Edit_Changelist_Tooltip", "Edit a changelist description"), FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnEditChangelist)));

		Section.AddMenuEntry("DeleteChangelist", LOCTEXT("SourceControl_DeleteChangelist", "Delete Empty Changelist"), LOCTEXT("SourceControl_Delete_Changelist_Tooltip", "Deletes an empty changelist"), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDeleteChangelist),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanDeleteChangelist)));
	}

	// Files-only operations
	if (!bHasEmptySelection && !bHasSelectedChangelist)
	{
		Section.AddSeparator("Files");

		Section.AddMenuEntry("Locate File", LOCTEXT("SourceControl_LocateFile", "Locate File"), LOCTEXT("SourceControl_LocateFile_Tooltip", "Locate File in Project..."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnLocateFile),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanLocateFile)));

		Section.AddMenuEntry("Show History", LOCTEXT("SourceControl_ShowHistory", "Show History"), LOCTEXT("SourceControl_ShowHistory_ToolTip", "Show File History From Selection..."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnShowHistory)));

		Section.AddMenuEntry("Diff Against Local Version", LOCTEXT("SourceControl_DiffAgainstDepot", "Diff Against Depot"), LOCTEXT("SourceControl_DiffAgainstLocal_Tooltip", "Diff local file against depot revision."), FSlateIcon(),
			FUIAction(
				FExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::OnDiffAgainstDepot),
				FCanExecuteAction::CreateSP(this, &SSourceControlChangelistsWidget::CanDiffAgainstDepot)));
	}

	return ToolMenus->GenerateWidget(Menu);
}

TSharedRef<SChangelistTree> SSourceControlChangelistsWidget::CreateTreeviewWidget()
{
	return SAssignNew(TreeView, SChangelistTree)
		.ItemHeight(24.0f)
		.TreeItemsSource(&ChangelistsNodes)
		.OnGenerateRow(this, &SSourceControlChangelistsWidget::OnGenerateRow)
		.OnGetChildren(this, &SSourceControlChangelistsWidget::OnGetChildren)
		.SelectionMode(ESelectionMode::Multi)
		.OnContextMenuOpening(this, &SSourceControlChangelistsWidget::OnOpenContextMenu)
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

protected:
	//~ Begin STableRow Interface.
	virtual FReply OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent) override
	{
		TSharedPtr<FSCCFileDragDropOp> Operation = InDragDropEvent.GetOperationAs<FSCCFileDragDropOp>();
		if (Operation.IsValid())
		{
			FSourceControlChangelistPtr Changelist = TreeItem->ChangelistState->GetChangelist();
			check(Changelist.IsValid());

			TArray<FString> Files;
			Algo::Transform(Operation->Files, Files, [](const FSourceControlStateRef& State) { return State->GetFilename(); });

			ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
			SourceControlProvider.Execute(ISourceControlOperation::Create<FMoveToChangelist>(), Changelist, Files);
		}

		return FReply::Handled();
	}
	//~ End STableRow Interface.

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
	SLATE_EVENT(FOnDragDetected, OnDragDetected)
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

		ChildSlot
		[
			SNew(SHorizontalBox)

			// Icon
			+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(40, 0, 4, 0)
				[
					GetSCCFileWidget(TreeItem->FileState)
				]

			+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(this, &SFileTableRow::GetDisplayText)
				]
		];

		auto Args = STableRow::FArguments()
			.OnDragDetected(InArgs._OnDragDetected);

		STableRow<FChangelistTreeItemPtr>::ConstructInternal(
			Args
			.ShowSelection(true),
			InOwner
		);
	}

	FText GetDisplayText() const
	{
		return TreeItem->GetDisplayText();
	}

protected:
	//~ Begin STableRow Interface.
	virtual void OnDragEnter(FGeometry const& InGeometry, FDragDropEvent const& InDragDropEvent) override
	{
		TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
		DragOperation->SetCursorOverride(EMouseCursor::SlashedCircle);
	}

	virtual void OnDragLeave(FDragDropEvent const& InDragDropEvent) override
	{
		TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
		DragOperation->SetCursorOverride(EMouseCursor::None);
	}
	//~ End STableRow Interface.

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
			.TreeItemToVisualize(InTreeItem)
			.OnDragDetected(this, &SSourceControlChangelistsWidget::OnFilesDragged);

	default:
		check(false);
	};

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable);
}

FReply SSourceControlChangelistsWidget::OnFilesDragged(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && !TreeView->GetSelectedItems().IsEmpty())
	{
		TSharedRef<FSCCFileDragDropOp> Operation = MakeShareable(new FSCCFileDragDropOp());

		Algo::Transform(TreeView->GetSelectedItems(), Operation->Files, [](FChangelistTreeItemPtr InTreeItem) { check(InTreeItem->GetTreeItemType() == IChangelistTreeItem::File); return static_cast<FFileTreeItem*>(InTreeItem.Get())->FileState; });
		Operation->Construct();

		return FReply::Handled().BeginDragDrop(Operation);
	}

	return FReply::Unhandled();
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