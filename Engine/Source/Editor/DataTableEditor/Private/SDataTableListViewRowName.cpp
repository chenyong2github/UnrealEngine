// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SDataTableListViewRowName.h"

#include "AssetData.h"
#include "DataTableEditor.h"
#include "DataTableRowUtlis.h"
#include "EditorStyleSet.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "SDataTableListViewRowName"

void SDataTableListViewRowName::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	RowDataPtr = InArgs._RowDataPtr;
	CurrentName = MakeShareable(new FName(RowDataPtr->RowId));
	DataTableEditor = InArgs._DataTableEditor;
	STableRow<FDataTableEditorRowListViewDataPtr>::Construct(
		typename STableRow<FDataTableEditorRowListViewDataPtr>::FArguments()
		.Style(FEditorStyle::Get(), "DataTableEditor.CellListViewRow")
		.Content()
		[
			SNew(SBox)
			.Padding(FMargin(4, 2, 4, 2))
			[
				SNew(SBox)
				.HeightOverride(RowDataPtr->DesiredRowHeight)
				[
					SAssignNew(EditableText, SEditableText)
					.Text(RowDataPtr->DisplayName)
					.OnTextCommitted(this, &SDataTableListViewRowName::OnRowRenamed)
					.ColorAndOpacity(DataTableEditor.Pin().Get(), &FDataTableEditor::GetRowTextColor, RowDataPtr->RowId)					
				]
			]
		],
		InOwnerTableView
	);
}

FReply SDataTableListViewRowName::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && RowDataPtr.IsValid() && FEditorDelegates::OnOpenReferenceViewer.IsBound() && DataTableEditor.IsValid())
	{
		FDataTableEditorUtils::SelectRow(DataTableEditor.Pin()->GetDataTable(), RowDataPtr->RowId);
		TSharedRef<SWidget> MenuWidget = FDataTableRowUtils::MakeRowActionsMenu(DataTableEditor.Pin(), FExecuteAction::CreateSP(this, &SDataTableListViewRowName::OnSearchForReferences));
		
		FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuWidget, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);
		return FReply::Handled();
	}

	return STableRow::OnMouseButtonUp(MyGeometry, MouseEvent);
}

void SDataTableListViewRowName::OnSearchForReferences()
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

void SDataTableListViewRowName::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	
	if (EditableText->HasAnyUserFocus())
	{
		UDataTable* DataTable = Cast<UDataTable>(DataTableEditor.Pin()->GetEditingObject());
		if (RowDataPtr.IsValid() && DataTable)
		{
	
			DataTableEditor.Pin()->SelectionChange(DataTable, RowDataPtr->RowId);
		}
	}
}

FReply SDataTableListViewRowName::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FKey Key = InKeyEvent.GetKey();

	if (Key == EKeys::Escape && EditableText->HasKeyboardFocus())
	{
		// Clear focus
		return FReply::Handled().SetUserFocus(SharedThis(this), EFocusCause::Cleared);
	}

	return FReply::Unhandled();
}

void SDataTableListViewRowName::OnRowRenamed(const FText& Text, ETextCommit::Type CommitType)
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
		DataTableEditor.Pin()->SelectionChange(DataTable, NewName);
		*CurrentName = NewName;
	}
}

FName SDataTableListViewRowName::GetCurrentName() const
{
	return CurrentName.IsValid() ? *CurrentName : NAME_None;

}

FText SDataTableListViewRowName::GetCurrentNameAsText() const
{
	return FText::FromName(GetCurrentName());
}

#undef LOCTEXT_NAMESPACE
