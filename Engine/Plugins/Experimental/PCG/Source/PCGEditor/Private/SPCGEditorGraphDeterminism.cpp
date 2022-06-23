// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphDeterminism.h"

#include "PCGEditor.h"

#define LOCTEXT_NAMESPACE "SPCGEditorGraphDeterminismListView"

namespace
{
	const FName NAME_Index(TEXT("IndexColumn"));
	const FName NAME_NodeTitle(TEXT("NodeTitleColumn"));
	const FName NAME_NodeName(TEXT("NodeNameColumn"));
	const FName NAME_Data(TEXT("DataColumn"));
	const FName NAME_Result(TEXT("ResultColumn"));
	const FName NAME_AdditionalDetails(TEXT("AdditionalDetailsColumn"));

	const FText TEXT_Index(LOCTEXT("IndexLabel", ""));
	const FText TEXT_NodeTitle(LOCTEXT("NodeTitleLabel", "Title"));
	const FText TEXT_NodeName(LOCTEXT("NodeNameLabel", "Name"));
	const FText TEXT_Data(LOCTEXT("DataLabel", "Input Data"));
	const FText TEXT_Result(LOCTEXT("ResultLabel", "Deterministic"));
	const FText TEXT_AdditionalDetails(LOCTEXT("AdditionalDetailsLabel", "Additional Details"));

	const FText TEXT_Yes(LOCTEXT("Yes", "Yes"));
	const FText TEXT_No(LOCTEXT("No", "No"));
}

void SPCGEditorGraphDeterminismRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const FPCGDeterminismResultPtr& Item)
{
	CurrentItem = Item;
	SMultiColumnTableRow<FPCGDeterminismResultPtr>::Construct(SMultiColumnTableRow::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SPCGEditorGraphDeterminismRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	check(CurrentItem);

	FText CellText = LOCTEXT("UnknownColumn", "Unknown");

	if (ColumnId == NAME_Index)
	{
		CellText = FText::FromString(FString::FromInt(CurrentItem->Index));
	}
	else if (ColumnId == NAME_NodeTitle)
	{
		CellText = FText::FromString(CurrentItem->NodeTitle.ToString());
	}
	else if (ColumnId == NAME_NodeName)
	{
		CellText = FText::FromString(CurrentItem->NodeNameString);
	}
	else if (ColumnId == NAME_Data)
	{
		CellText = FText::FromString(CurrentItem->DataTestedString);
	}
	else if (ColumnId == NAME_Result)
	{
		if (CurrentItem->bIsDeterministic)
		{
			return SNew(STextBlock)
					.Text(TEXT_Yes)
					.ColorAndOpacity(FColor::Green);
		}
		else
		{
			return SNew(STextBlock)
					.Text(TEXT_No)
					.ColorAndOpacity(FColor::Red);
		}
	}
	else if (ColumnId == NAME_AdditionalDetails)
	{
		CellText = FText::FromString(CurrentItem->AdditionalDetailString);
	}

	return SNew(STextBlock).Text(CellText);
}

void SPCGEditorGraphDeterminismListView::Construct(const FArguments& InArgs, TWeakPtr<FPCGEditor> InPCGEditor)
{
	check(InPCGEditor.IsValid() && !bIsConstructed);
	PCGEditorPtr = InPCGEditor;

	SAssignNew(ListView, SListView<FPCGDeterminismResultPtr>)
		.ListItemsSource(&ListViewItems)
		.ItemHeight(36)
		.OnGenerateRow(this, &SPCGEditorGraphDeterminismListView::OnGenerateRow)
		.HeaderRow
		(
			SNew(SHeaderRow)
			+ SHeaderRow::Column(NAME_Index)
			.DefaultLabel(TEXT_Index)
			.ManualWidth(25)
			.HAlignCell(HAlign_Center)
			+ SHeaderRow::Column(NAME_NodeTitle)
			.DefaultLabel(TEXT_NodeTitle)
			.ManualWidth(160)
			.HAlignCell(HAlign_Left)
			+ SHeaderRow::Column(NAME_NodeName)
			.DefaultLabel(TEXT_NodeName)
			.ManualWidth(160)
			.HAlignCell(HAlign_Left)
			+ SHeaderRow::Column(NAME_Data)
			.DefaultLabel(TEXT_Data)
			.ManualWidth(70)
			.HAlignCell(HAlign_Center)
			+ SHeaderRow::Column(NAME_Result)
			.DefaultLabel(TEXT_Result)
			.ManualWidth(90)
			.HAlignCell(HAlign_Center)
			+ SHeaderRow::Column(NAME_AdditionalDetails)
			.DefaultLabel(TEXT_AdditionalDetails)
			.HAlignCell(HAlign_Left)
		);

	ChildSlot
		[
			ListView->AsShared()
		];

	bIsConstructed = true;
}

void SPCGEditorGraphDeterminismListView::AddItem(const FPCGDeterminismResultPtr Item)
{
	check(Item.IsValid());
	ListViewItems.Add(Item);
	ListView->RequestListRefresh();
}

void SPCGEditorGraphDeterminismListView::Clear()
{
	ListViewItems.Empty();
	ListView->RequestListRefresh();
}

bool SPCGEditorGraphDeterminismListView::IsContructed() const
{
	return bIsConstructed;
}

TSharedRef<ITableRow> SPCGEditorGraphDeterminismListView::OnGenerateRow(const FPCGDeterminismResultPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SPCGEditorGraphDeterminismRow, OwnerTable, Item);
}

#undef LOCTEXT_NAMESPACE