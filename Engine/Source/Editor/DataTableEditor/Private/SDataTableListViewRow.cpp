// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SDataTableListViewRow.h"

#include "AssetData.h"
#include "DataTableEditor.h"
#include "DataTableRowUtlis.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "SDataTableListViewRowName"

void SDataTableListViewRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	RowDataPtr = InArgs._RowDataPtr;
	CurrentName = MakeShareable(new FName(RowDataPtr->RowId));
	DataTableEditor = InArgs._DataTableEditor;
	SMultiColumnTableRow<FDataTableEditorRowListViewDataPtr>::Construct(
		FSuperRowType::FArguments()
		.Style(FEditorStyle::Get(), "DataTableEditor.CellListViewRow"),
		InOwnerTableView
	);

}

FReply SDataTableListViewRow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && RowDataPtr.IsValid() && FEditorDelegates::OnOpenReferenceViewer.IsBound() && DataTableEditor.IsValid())
	{
		FDataTableEditorUtils::SelectRow(DataTableEditor.Pin()->GetDataTable(), RowDataPtr->RowId);
		TSharedRef<SWidget> MenuWidget = FDataTableRowUtils::MakeRowActionsMenu(DataTableEditor.Pin(), FExecuteAction::CreateSP(this, &SDataTableListViewRow::OnSearchForReferences));

		FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuWidget, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);
		return FReply::Handled();
	}

	return STableRow::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SDataTableListViewRow::OnSearchForReferences()
{
	if (DataTableEditor.IsValid() && RowDataPtr.IsValid())
	{
		if (FDataTableEditor* DataTableEditorPtr = DataTableEditor.Pin().Get())
		{
			UDataTable* SourceDataTable = const_cast<UDataTable*>(DataTableEditorPtr->GetDataTable());

			TArray<FAssetIdentifier> AssetIdentifiers;
			AssetIdentifiers.Add(FAssetIdentifier(SourceDataTable, RowDataPtr->RowId));

			FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers);
		}
	}
}

FReply SDataTableListViewRow::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape && InlineEditableText->HasKeyboardFocus())
	{
		// Clear focus
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Cleared);
	}

	return FReply::Unhandled();
}

void SDataTableListViewRow::OnRowRenamed(const FText& Text, ETextCommit::Type CommitType)
{
	UDataTable* DataTable = Cast<UDataTable>(DataTableEditor.Pin()->GetEditingObject());

	if (!GetCurrentNameAsText().EqualTo(Text) && DataTable)
	{
		const TArray<FName> RowNames = DataTable->GetRowNames();

		if (Text.IsEmptyOrWhitespace() || !FName::IsValidXName(Text.ToString(), INVALID_NAME_CHARACTERS))
		{
			// Only pop up the error dialog if the rename was caused by the user's action
			if ((CommitType == ETextCommit::OnEnter) || (CommitType == ETextCommit::OnUserMovedFocus))
			{
				// popup an error dialog here
				const FText Message = FText::Format(LOCTEXT("InvalidRowName", "'{0}' is not a valid row name"), Text);
				FMessageDialog::Open(EAppMsgType::Ok, Message);
			}
			return;
		}
		const FName NewName = DataTableUtils::MakeValidName(Text.ToString());
		if (NewName == NAME_None)
		{
			// popup an error dialog here
			const FText Message = FText::Format(LOCTEXT("InvalidRowName", "'{0}' is not a valid row name"), Text);
			FMessageDialog::Open(EAppMsgType::Ok, Message);

			return;
		}
		for (const FName& Name : RowNames)
		{
			if (Name.IsValid() && (Name == NewName))
			{
				//the name already exists
				// popup an error dialog here
				const FText Message = FText::Format(LOCTEXT("DuplicateRowName", "'{0}' is already used as a row name in this table"), Text);
				FMessageDialog::Open(EAppMsgType::Ok, Message);
				return;

			}
		}

		const FName OldName = GetCurrentName();
		FDataTableEditorUtils::RenameRow(DataTable, OldName, NewName);
		FDataTableEditorUtils::SelectRow(DataTable, NewName);

		*CurrentName = NewName;
	}
}

TSharedRef<SWidget> SDataTableListViewRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	TSharedPtr<FDataTableEditor> DataTableEditorPtr = DataTableEditor.Pin();
	return (DataTableEditorPtr.IsValid())
		? MakeCellWidget(IndexInList, ColumnName)
		: SNullWidget::NullWidget;
}

TSharedRef<SWidget> SDataTableListViewRow::MakeCellWidget(const int32 InRowIndex, const FName& InColumnId)
{
	int32 ColumnIndex = 0;

	FDataTableEditor* DataTableEdit = DataTableEditor.Pin().Get();
	TArray<FDataTableEditorColumnHeaderDataPtr>& AvailableColumns = DataTableEdit->AvailableColumns;

	if (InColumnId.IsEqual(FName(TEXT("RowName"))))
	{
		return SNew(SBox)
			.Padding(FMargin(4, 2, 4, 2))
			[
				SAssignNew(InlineEditableText, SInlineEditableTextBlock)
				.Text(RowDataPtr->DisplayName)
				.OnTextCommitted(this, &SDataTableListViewRow::OnRowRenamed)
				.ColorAndOpacity(DataTableEdit, &FDataTableEditor::GetRowTextColor, RowDataPtr->RowId)
			];
	}

	for (; ColumnIndex < AvailableColumns.Num(); ++ColumnIndex)
	{
		const FDataTableEditorColumnHeaderDataPtr& ColumnData = AvailableColumns[ColumnIndex];
		if (ColumnData->ColumnId == InColumnId)
		{
			break;
		}
	}
	 
	// Valid column ID?
	if (AvailableColumns.IsValidIndex(ColumnIndex) && RowDataPtr->CellData.IsValidIndex(ColumnIndex))
	{
		return SNew(SBox)
			.Padding(FMargin(4, 2, 4, 2))
			[
				SNew(STextBlock)
				.TextStyle(FEditorStyle::Get(), "DataTableEditor.CellText")
				.ColorAndOpacity(DataTableEdit, &FDataTableEditor::GetRowTextColor, RowDataPtr->RowId)
				.Text(DataTableEdit, &FDataTableEditor::GetCellText, RowDataPtr, ColumnIndex)
				.HighlightText(DataTableEdit, &FDataTableEditor::GetFilterText)
				.ToolTipText(DataTableEdit, &FDataTableEditor::GetCellToolTipText, RowDataPtr, ColumnIndex)
			];
	}

	return SNullWidget::NullWidget;
}

FName SDataTableListViewRow::GetCurrentName() const
{
	return CurrentName.IsValid() ? *CurrentName : NAME_None;

}

FReply SDataTableListViewRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	if (InlineEditableText->IsHovered())
	{
		InlineEditableText->EnterEditingMode();
	}

	return FReply::Handled();
}

FText SDataTableListViewRow::GetCurrentNameAsText() const
{
	return FText::FromName(GetCurrentName());
}

#undef LOCTEXT_NAMESPACE
