// Copyright Epic Games, Inc. All Rights Reserved.

#include "SPCGEditorGraphDeterminism.h"

#include "PCGEditor.h"

#define LOCTEXT_NAMESPACE "PCGDeterminism"

namespace
{
	const FName NAME_Index(TEXT("Index_ColumnID"));
	const FName NAME_NodeTitle(TEXT("NodeTitle_ColumnID"));
	const FName NAME_NodeName(TEXT("NodeName_ColumnID"));
	const FName NAME_DataTypesTested(TEXT("DataTypesTested_ColumnID"));
	const FName NAME_AdditionalDetails(TEXT("AdditionalDetails_ColumnID"));

	const FText TEXT_Index(LOCTEXT("Index_Label", ""));
	const FText TEXT_NodeTitle(LOCTEXT("NodeTitle_Label", "Title"));
	const FText TEXT_NodeName(LOCTEXT("NodeName_Label", "Name"));
	const FText TEXT_DataTypesTested(LOCTEXT("DataTypesTested_Label", "Input Data"));
	const FText TEXT_AdditionalDetails(LOCTEXT("AdditionalDetails_Label", "Additional Details"));

	const FText TEXT_NotDeterministic(LOCTEXT("NotDeterministic", "Fail"));
	const FText TEXT_Consistent(LOCTEXT("OrderConsistent", "Order Consistent"));
	const FText TEXT_Independent(LOCTEXT("OrderIndependent", "Order Independent"));
	const FText TEXT_Orthogonal(LOCTEXT("OrderOrthogonal", "Order Orthogonal"));
	const FText TEXT_Basic(LOCTEXT("BasicDeterminism", "Pass"));

	constexpr float SmallManualWidth = 25.f;
	constexpr float MediumManualWidth = 70.f;
	constexpr float LargeManualWidth = 160.f;
	constexpr float ListViewRowHeight = 36.f;
}

void SPCGEditorGraphDeterminismRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const FPCGNodeTestResultPtr& Item)
{
	CurrentItem = Item;
	SMultiColumnTableRow<FPCGNodeTestResultPtr>::Construct(SMultiColumnTableRow::FArguments(), InOwnerTableView);
}

TSharedRef<SWidget> SPCGEditorGraphDeterminismRow::GenerateWidgetForColumn(const FName& ColumnId)
{
	check(CurrentItem);

	FText CellText = LOCTEXT("UnknownColumn", "Unknown");

	auto ReturnColorCodedResultBlock = [this](const FText& OutCellText)
	{
		return SNew(STextBlock)
			.Text(OutCellText)
			.ColorAndOpacity(CurrentItem->bFlagRaised ? FColor::Red : FColor::Green);
	};

	// Permanent columns
	if (ColumnId == NAME_Index)
	{
		return ReturnColorCodedResultBlock(FText::FromString(FString::FromInt(CurrentItem->Index)));
	}
	else if (ColumnId == NAME_NodeTitle)
	{
		return ReturnColorCodedResultBlock(FText::FromName(CurrentItem->NodeTitle));
	}
	else if (ColumnId == NAME_NodeName)
	{
		CellText = FText::FromString(CurrentItem->NodeNameString);
	}
	else if (ColumnId == NAME_DataTypesTested)
	{
		FString DataTypesTestedString = *UEnum::GetValueAsString(CurrentItem->DataTypesTested);
		DataTypesTestedString.RemoveFromStart("EPCGDataType::");
		CellText = FText::FromString(DataTypesTestedString);
	}
	else if (ColumnId == NAME_AdditionalDetails)
	{
		FString FullDetails = FString::Join(CurrentItem->AdditionalDetails, TEXT(", "));
		CellText = FText::FromString(FullDetails);
	}

	// Test columns
	if (PCGDeterminismTests::EDeterminismLevel* DeterminismLevel = CurrentItem->TestResults.Find(ColumnId))
	{
		switch (*DeterminismLevel)
		{
		case PCGDeterminismTests::EDeterminismLevel::OrderOrthogonal:
			return SNew(STextBlock)
				.Text(TEXT_Orthogonal)
				.ColorAndOpacity(FColor::Orange);
		case PCGDeterminismTests::EDeterminismLevel::OrderConsistent:
			return SNew(STextBlock)
				.Text(TEXT_Consistent)
				.ColorAndOpacity(FColor::Yellow);
		case PCGDeterminismTests::EDeterminismLevel::OrderIndependent:
			return SNew(STextBlock)
				.Text(TEXT_Independent)
				.ColorAndOpacity(FColor::Green);
		case PCGDeterminismTests::EDeterminismLevel::Basic:
			return SNew(STextBlock)
				.Text(TEXT_Basic)
				.ColorAndOpacity(FColor::Turquoise);
		case PCGDeterminismTests::EDeterminismLevel::NoDeterminism:
		default:
			return SNew(STextBlock)
				.Text(TEXT_NotDeterministic)
				.ColorAndOpacity(FColor::Red);
		}
	}

	return SNew(STextBlock)
		.Text(CellText);
}

void SPCGEditorGraphDeterminismListView::Construct(const FArguments& InArgs, TWeakPtr<FPCGEditor> InPCGEditor, const TArray<FTestColumnInfo>& InTestColumns)
{
	check(InPCGEditor.IsValid() && !bIsConstructed);
	PCGEditorPtr = InPCGEditor;

	TSharedRef<SHeaderRow> GeneratedHeaderRow = SNew(SHeaderRow);

	TArray<FTestColumnInfo> TestColumnInfo;
	// Permanent columns
	TestColumnInfo.Emplace(NAME_Index, TEXT_Index, SmallManualWidth, HAlign_Center);
	TestColumnInfo.Emplace(NAME_NodeTitle, TEXT_NodeTitle, LargeManualWidth, HAlign_Left);
	TestColumnInfo.Emplace(NAME_NodeName, TEXT_NodeName, LargeManualWidth, HAlign_Left);
	TestColumnInfo.Emplace(NAME_DataTypesTested, TEXT_DataTypesTested, MediumManualWidth, HAlign_Center);

	// Test columns
	TestColumnInfo.Append(InTestColumns);

	// Final details column
	TestColumnInfo.Emplace(NAME_AdditionalDetails, TEXT_AdditionalDetails, 0.f, HAlign_Left);

	// Build column arguments for test columns dynamically
	for (const FTestColumnInfo& ColumnInfo : TestColumnInfo)
	{
		SHeaderRow::FColumn::FArguments Arguments;
		Arguments.ColumnId(ColumnInfo.ColumnID);
		Arguments.DefaultLabel(ColumnInfo.ColumnLabel);
		if (ColumnInfo.Width > 0.f)
		{
			Arguments.ManualWidth(ColumnInfo.Width);
		}
		Arguments.HAlignCell(ColumnInfo.HAlign);
		GeneratedHeaderRow->AddColumn(Arguments);
	}

	SAssignNew(ListView, SListView<FPCGNodeTestResultPtr>)
		.ListItemsSource(&ListViewItems)
		.ItemHeight(ListViewRowHeight)
		.OnGenerateRow(this, &SPCGEditorGraphDeterminismListView::OnGenerateRow)
		.HeaderRow(GeneratedHeaderRow);

	ChildSlot
	[
		ListView->AsShared()
	];

	bIsConstructed = true;
}

void SPCGEditorGraphDeterminismListView::AddItem(const FPCGNodeTestResultPtr& Item)
{
	check(Item.IsValid());
	ListViewItems.Emplace(Item);
	Refresh();
}

void SPCGEditorGraphDeterminismListView::Clear()
{
	ListViewItems.Empty();
	Refresh();
}

void SPCGEditorGraphDeterminismListView::Refresh()
{
	ListView->RequestListRefresh();
}

bool SPCGEditorGraphDeterminismListView::WidgetIsConstructed() const
{
	return bIsConstructed;
}

TSharedRef<ITableRow> SPCGEditorGraphDeterminismListView::OnGenerateRow(const FPCGNodeTestResultPtr Item, const TSharedRef<STableViewBase>& OwnerTable) const
{
	return SNew(SPCGEditorGraphDeterminismRow, OwnerTable, Item);
}

#undef LOCTEXT_NAMESPACE