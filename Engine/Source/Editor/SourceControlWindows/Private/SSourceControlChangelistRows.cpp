// Copyright Epic Games, Inc. All Rights Reserved.

#include "SSourceControlChangelistRows.h"

#include "ISourceControlModule.h"
#include "UncontrolledChangelistsModule.h"
#include "SourceControlOperations.h"

#define LOCTEXT_NAMESPACE "SourceControlChangelistRow"


const FName SourceControlFileViewColumnId::Icon = TEXT("Icon");
const FName SourceControlFileViewColumnId::Name = TEXT("Name");
const FName SourceControlFileViewColumnId::Path = TEXT("Path");
const FName SourceControlFileViewColumnId::Type = TEXT("Type");


void SChangelistTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
{
	TreeItem = static_cast<FChangelistTreeItem*>(InArgs._TreeItemToVisualize.Get());
	OnPostDrop = InArgs._OnPostDrop;

	const FSlateBrush* IconBrush = (TreeItem != nullptr) ?
		FAppStyle::GetBrush(TreeItem->ChangelistState->GetSmallIconName()) :
		FAppStyle::GetBrush("SourceControl.Changelist");

	SetToolTipText(GetChangelistDescriptionText());

	STableRow<FChangelistTreeItemPtr>::Construct(
		STableRow<FChangelistTreeItemPtr>::FArguments()
		.Style(FAppStyle::Get(), "TableView.Row")
		.Content()
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot() // Icon
			.AutoWidth()
			[
				SNew(SImage)
				.Image(IconBrush)
			]
			+SHorizontalBox::Slot() // Changelist number.
			.Padding(2, 0, 0, 0)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SChangelistTableRow::GetChangelistText)
			]
			+SHorizontalBox::Slot() // Files count.
			.Padding(4, 0, 4, 0)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::Format(INVTEXT("({0})"), TreeItem->GetFileCount()))
			]
			+SHorizontalBox::Slot()
			.Padding(2, 0, 0, 0)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(this, &SChangelistTableRow::GetChangelistDescriptionText)
			]
		], InOwner);
}

FText SChangelistTableRow::GetChangelistText() const
{
	return TreeItem->GetDisplayText();
}

FText SChangelistTableRow::GetChangelistDescriptionText() const
{
	FString DescriptionString = TreeItem->GetDescriptionText().ToString();
	// Here we'll both remove \r\n (when edited from the dialog) and \n (when we get it from the SCC)
	DescriptionString.ReplaceInline(TEXT("\r"), TEXT(""));
	DescriptionString.ReplaceInline(TEXT("\n"), TEXT(" "));
	DescriptionString.TrimEndInline();
	return FText::FromString(DescriptionString);
}

FReply SChangelistTableRow::OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent)
{
	TSharedPtr<FSCCFileDragDropOp> DropOperation = InDragDropEvent.GetOperationAs<FSCCFileDragDropOp>();
	if (DropOperation.IsValid())
	{
		FSourceControlChangelistPtr DestChangelist = TreeItem->ChangelistState->GetChangelist();
		check(DestChangelist.IsValid());

		// NOTE: The UI don't show 'source controlled files' and 'uncontrolled files' at the same time. User cannot select and drag/drop both file types at the same time.
		if (!DropOperation->Files.IsEmpty())
		{
			TArray<FString> Files;
			Algo::Transform(DropOperation->Files, Files, [](const FSourceControlStateRef& State) { return State->GetFilename(); });

			SSourceControlCommon::ExecuteChangelistOperationWithSlowTaskWrapper(LOCTEXT("Dropping_Files_On_Changelist", "Moving file(s) to the selected changelist..."), [&]()
			{
				ISourceControlProvider& SourceControlProvider = ISourceControlModule::Get().GetProvider();
				SourceControlProvider.Execute(ISourceControlOperation::Create<FMoveToChangelist>(), DestChangelist, Files, EConcurrency::Synchronous, FSourceControlOperationComplete::CreateLambda(
					[](const TSharedRef<ISourceControlOperation>& Operation, ECommandResult::Type InResult)
					{
						if (InResult == ECommandResult::Succeeded)
						{
							SSourceControlCommon::DisplaySourceControlOperationNotification(LOCTEXT("Drop_Files_On_Changelist_Succeeded", "File(s) successfully moved to the selected changelist."), SNotificationItem::CS_Success);
						}
						else if (InResult == ECommandResult::Failed)
						{
							SSourceControlCommon::DisplaySourceControlOperationNotification(LOCTEXT("Drop_Files_On_Changelist_Failed", "Failed to move the file(s) to the selected changelist."), SNotificationItem::CS_Fail);
						}
					}));
			});
		}
		else if (!DropOperation->UncontrolledFiles.IsEmpty())
		{
			// NOTE: This function does several operations that can fails but we don't get feedback.
			SSourceControlCommon::ExecuteUncontrolledChangelistOperationWithSlowTaskWrapper(LOCTEXT("Dropping_Uncontrolled_Files_On_Changelist", "Moving uncontrolled file(s) to the selected changelist..."), 
				[&DropOperation, &DestChangelist]()
			{
				FUncontrolledChangelistsModule::Get().MoveFilesToControlledChangelist(DropOperation->UncontrolledFiles, DestChangelist, SSourceControlCommon::OpenConflictDialog);
					
				// TODO: Fix MoveFilesToControlledChangelist() to report the possible errors and display a notification.
			});

			OnPostDrop.ExecuteIfBound();
		}
	}

	return FReply::Handled();
}


void SUncontrolledChangelistTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
{
	TreeItem = static_cast<FUncontrolledChangelistTreeItem*>(InArgs._TreeItemToVisualize.Get());
	OnPostDrop = InArgs._OnPostDrop;

		const FSlateBrush* IconBrush = (TreeItem != nullptr) ?
			FAppStyle::GetBrush(TreeItem->UncontrolledChangelistState->GetSmallIconName()) :
			FAppStyle::GetBrush("SourceControl.Changelist");

	SetToolTipText(GetChangelistDescriptionText());

	STableRow<FChangelistTreeItemPtr>::Construct(
		STableRow<FChangelistTreeItemPtr>::FArguments()
		.Style(FAppStyle::Get(), "TableView.Row")
		.Content()
		[
			SNew(SHorizontalBox)
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
				.Text(this, &SUncontrolledChangelistTableRow::GetChangelistText)
			]
			+SHorizontalBox::Slot() // Files/Offline file count.
			.Padding(4, 0, 4, 0)
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::Format(INVTEXT("({0})"), TreeItem->GetFileCount() + TreeItem->GetOfflineFileCount()))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 0.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(this, &SUncontrolledChangelistTableRow::GetChangelistDescriptionText)
			]
		], InOwner);
}

FText SUncontrolledChangelistTableRow::GetChangelistText() const
{
	return TreeItem->GetDisplayText();
}

FText SUncontrolledChangelistTableRow::GetChangelistDescriptionText() const
{
	FString DescriptionString = TreeItem->GetDescriptionText().ToString();
	// Here we'll both remove \r\n (when edited from the dialog) and \n (when we get it from the SCC)
	DescriptionString.ReplaceInline(TEXT("\r"), TEXT(""));
	DescriptionString.ReplaceInline(TEXT("\n"), TEXT(" "));
	DescriptionString.TrimEndInline();
	return FText::FromString(DescriptionString);
}

FReply SUncontrolledChangelistTableRow::OnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent)
{
	TSharedPtr<FSCCFileDragDropOp> Operation = InDragDropEvent.GetOperationAs<FSCCFileDragDropOp>();

	if (Operation.IsValid())
	{
		SSourceControlCommon::ExecuteUncontrolledChangelistOperationWithSlowTaskWrapper(LOCTEXT("Drag_File_To_Uncontrolled_Changelist", "Moving file(s) to the selected uncontrolled changelists..."),
			[this, &Operation]()
		{
			FUncontrolledChangelistsModule::Get().MoveFilesToUncontrolledChangelist(Operation->Files, Operation->UncontrolledFiles, TreeItem->UncontrolledChangelistState->Changelist);
		});

		OnPostDrop.ExecuteIfBound();
	}

	return FReply::Handled();
}


void SFileTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
{
	TreeItem = static_cast<FFileTreeItem*>(InArgs._TreeItemToVisualize.Get());

	FSuperRowType::FArguments Args = FSuperRowType::FArguments()
		.OnDragDetected(InArgs._OnDragDetected)
		.ShowSelection(true);
	FSuperRowType::Construct(Args, InOwner);
}

TSharedRef<SWidget> SFileTableRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	if (ColumnId == SourceControlFileViewColumnId::Icon)
	{
		return SNew(SBox)
			.WidthOverride(16) // Small Icons are usually 16x16
			.HAlign(HAlign_Center)
			[
				SSourceControlCommon::GetSCCFileWidget(TreeItem->FileState, TreeItem->IsShelved())
			];
	}
	else if (ColumnId == SourceControlFileViewColumnId::Name)
	{
		return SNew(STextBlock)
			.Text(this, &SFileTableRow::GetDisplayName);
	}
	else if (ColumnId == SourceControlFileViewColumnId::Path)
	{
		return SNew(STextBlock)
			.Text(this, &SFileTableRow::GetDisplayPath)
			.ToolTipText(this, &SFileTableRow::GetFilename);
	}
	else if (ColumnId == SourceControlFileViewColumnId::Type)
	{
		return SNew(STextBlock)
			.Text(this, &SFileTableRow::GetDisplayType)
			.ColorAndOpacity(this, &SFileTableRow::GetDisplayColor);
	}
	else
	{
		return SNullWidget::NullWidget;
	}
}

FText SFileTableRow::GetDisplayName() const
{
	return TreeItem->GetAssetName();
}

FText SFileTableRow::GetFilename() const
{
	return TreeItem->GetFileName();
}

FText SFileTableRow::GetDisplayPath() const
{
	return TreeItem->GetAssetPath();
}

FText SFileTableRow::GetDisplayType() const
{
	return TreeItem->GetAssetType();
}

FSlateColor SFileTableRow::GetDisplayColor() const
{
	return TreeItem->GetAssetTypeColor();
}

void SFileTableRow::OnDragEnter(FGeometry const& InGeometry, FDragDropEvent const& InDragDropEvent)
{
	TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
	DragOperation->SetCursorOverride(EMouseCursor::SlashedCircle);
}

void SFileTableRow::OnDragLeave(FDragDropEvent const& InDragDropEvent)
{
	TSharedPtr<FDragDropOperation> DragOperation = InDragDropEvent.GetOperation();
	DragOperation->SetCursorOverride(EMouseCursor::None);
}


void SShelvedFilesTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	STableRow<FChangelistTreeItemPtr>::Construct(
		STableRow<FChangelistTreeItemPtr>::FArguments()
		.Content()
		[
			SNew(SHorizontalBox)
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(5, 0, 0, 0)
				[
					SNew(SImage)
					.Image(InArgs._Icon)
				]
				+SHorizontalBox::Slot()
				.Padding(2.0f, 1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(InArgs._Text)
				]
		],
		InOwnerTableView);
}


void SOfflineFileTableRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwner)
{
	TreeItem = static_cast<FOfflineFileTreeItem*>(InArgs._TreeItemToVisualize.Get());

	FSuperRowType::FArguments Args = FSuperRowType::FArguments().ShowSelection(true);
	FSuperRowType::Construct(Args, InOwner);
}

TSharedRef<SWidget> SOfflineFileTableRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	if (ColumnId == SourceControlFileViewColumnId::Icon)
	{
		return SNew(SBox)
			.WidthOverride(16) // Small Icons are usually 16x16
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(FName("SourceControl.OfflineFile_Small")))
			];
	}
	else if (ColumnId == SourceControlFileViewColumnId::Name)
	{
		return SNew(STextBlock)
			.Text(this, &SOfflineFileTableRow::GetDisplayName);
	}
	else if (ColumnId == SourceControlFileViewColumnId::Path)
	{
		return SNew(STextBlock)
			.Text(this, &SOfflineFileTableRow::GetDisplayPath)
			.ToolTipText(this, &SOfflineFileTableRow::GetFilename);
	}
	else if (ColumnId == SourceControlFileViewColumnId::Type)
	{
		return SNew(STextBlock)
			.Text(this, &SOfflineFileTableRow::GetDisplayType)
			.ColorAndOpacity(this, &SOfflineFileTableRow::GetDisplayColor);
	}
	else
	{
		return SNullWidget::NullWidget;
	}
}

FText SOfflineFileTableRow::GetDisplayName() const
{
	return TreeItem->GetDisplayName();
}

FText SOfflineFileTableRow::GetFilename() const
{
	return TreeItem->GetPackageName();
}

FText SOfflineFileTableRow::GetDisplayPath() const
{
	return TreeItem->GetDisplayPath();
}

FText SOfflineFileTableRow::GetDisplayType() const
{
	return TreeItem->GetDisplayType();
}

FSlateColor SOfflineFileTableRow::GetDisplayColor() const
{
	return TreeItem->GetDisplayColor();
}

#undef LOCTEXT_NAMESPACE
