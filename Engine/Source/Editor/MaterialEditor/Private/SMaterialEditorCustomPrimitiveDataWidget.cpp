// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMaterialEditorCustomPrimitiveDataWidget.h"
#include "Widgets/Layout/SScrollBox.h"
#include "MaterialEditor/MaterialEditorPreviewParameters.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"
#include "Widgets/SToolTip.h"

class SCustomPrimitiveDataRow : public SMultiColumnTableRow<TSharedPtr<FCustomPrimitiveDataRowData>>
{
public:
	SLATE_BEGIN_ARGS(SCustomPrimitiveDataRow) {}
		SLATE_ARGUMENT(TSharedPtr<FCustomPrimitiveDataRowData>, Entry)
	SLATE_END_ARGS()

	void Construct(const FArguments& Args, const TSharedRef<STableViewBase>& OwnerTableView)
	{
		RowData = Args._Entry;

		FSuperRowType::Construct(
			FSuperRowType::FArguments()
			.Padding(1.0f),
			OwnerTableView
		);
	}

	/** Overridden from SMultiColumnTableRow.  Generates a widget for this column of the list view. */
	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		TSharedRef<STextBlock> TextBlock = SNew(STextBlock);

		if (RowData->bIsDuplicate)
		{
			TextBlock->SetToolTip(SNew(SToolTip).Text(FText::FromString("This slot is potentially incorrectly overlapping")).BorderImage(FCoreStyle::Get().GetBrush("ToolTip.BrightBackground")));
			TextBlock->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}

		if (ColumnName == "Slot")
		{
			TextBlock->SetText(FText::FromString(FString::FormatAsNumber(RowData->Slot)));
			return TextBlock;
		}
		else if (ColumnName == "Name")
		{
			TextBlock->SetText(FText::FromString(RowData->Name));
			return TextBlock;
		}

		return SNullWidget::NullWidget;
	}

private:
	TSharedPtr<FCustomPrimitiveDataRowData> RowData;
};

void SMaterialCustomPrimitiveDataPanel::Refresh()
{
	Items.Empty();

	if (MaterialEditorInstance && MaterialEditorInstance->PreviewMaterial)
	{
		TArray<UMaterialExpressionScalarParameter*> Scalars;
		MaterialEditorInstance->PreviewMaterial->GetAllExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionScalarParameter>(Scalars);

		for (const UMaterialExpressionScalarParameter* Expr : Scalars)
		{
			if (Expr->bUseCustomPrimitiveData)
			{
				FString FunctionName;
				if (Expr->GraphNode)
				{
					Expr->GraphNode->GetGraph()->GetName(FunctionName);
				}

				Items.Add(MakeShareable(new FCustomPrimitiveDataRowData((int32)Expr->PrimitiveDataIndex, Expr->GetParameterName().ToString(), FunctionName)));
			}
		}

		TArray<UMaterialExpressionVectorParameter*> Vectors;
		MaterialEditorInstance->PreviewMaterial->GetAllExpressionsInMaterialAndFunctionsOfType<UMaterialExpressionVectorParameter>(Vectors);

		for (const UMaterialExpressionVectorParameter* Expr : Vectors)
		{
			if (Expr->bUseCustomPrimitiveData)
			{
				Items.Add(MakeShareable(new FCustomPrimitiveDataRowData((int32)Expr->PrimitiveDataIndex + 0, Expr->GetParameterName().ToString() + ".r")));
				Items.Add(MakeShareable(new FCustomPrimitiveDataRowData((int32)Expr->PrimitiveDataIndex + 1, Expr->GetParameterName().ToString() + ".g")));
				Items.Add(MakeShareable(new FCustomPrimitiveDataRowData((int32)Expr->PrimitiveDataIndex + 2, Expr->GetParameterName().ToString() + ".b")));
				Items.Add(MakeShareable(new FCustomPrimitiveDataRowData((int32)Expr->PrimitiveDataIndex + 3, Expr->GetParameterName().ToString() + ".a")));
			}
		}

		// Sort the items before construction the list
		Items.StableSort([](const TSharedPtr<FCustomPrimitiveDataRowData>& A, const TSharedPtr<FCustomPrimitiveDataRowData>& B) {
			int32 SlotA = A->Slot;
			int32 SlotB = B->Slot;
			if (SlotA == SlotB)
			{
				return A->Name < B->Name;
			}
			return SlotA < SlotB;
		});

		TSet<int32> DuplicateIndices;

		// Once sorted, find suplicate slots
		for (int32 i = 1; i < Items.Num(); i++)
		{
			TSharedPtr<FCustomPrimitiveDataRowData>& PrevItem = Items[i - 1];
			TSharedPtr<FCustomPrimitiveDataRowData>& CurrItem = Items[i];
			const int32 PrevSlot = PrevItem->Slot;
			const int32 CurrSlot = CurrItem->Slot;
			const FString& PrevName = PrevItem->Name;
			const FString& CurrName = CurrItem->Name;

			// Duplicate found, store the slot for later processing
			if ((CurrSlot == PrevSlot) && (CurrName != PrevName))
			{
				DuplicateIndices.Add(CurrItem->Slot);
			}
		}

		// Go through and mark slots as duplicate
		for (int32 i = 0; i < Items.Num(); i++)
		{
			if (DuplicateIndices.Find(Items[i]->Slot))
			{
				Items[i]->bIsDuplicate = true;
			}
		}
	}

	ListViewWidget->RequestListRefresh();
}

void SMaterialCustomPrimitiveDataPanel::Construct(const FArguments& InArgs, UMaterialEditorPreviewParameters* InMaterialEditorInstance)
{	
	this->ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop_Hovered"))
				.Padding(FMargin(4.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(FMargin(3.0f, 4.0f))
						.HAlign(HAlign_Left)
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::FromString("Custom Primitive Data Parameters"))
							.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
							.ShadowOffset(FVector2D(1.0f, 1.0f))
						]
					]
					+ SVerticalBox::Slot()
					.Padding(FMargin(3.0f, 2.0f, 3.0f, 3.0f))
					[
						SNew(SBorder)
						.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
						[
							SNew(SScrollBox)
							+ SScrollBox::Slot()
							[
								SAssignNew(ListViewWidget, SListView<TSharedPtr<FCustomPrimitiveDataRowData>>)
								.ItemHeight(24)
								.ListItemsSource(&Items)
								.OnGenerateRow(this, &SMaterialCustomPrimitiveDataPanel::OnGenerateRowForList)
								.SelectionMode(ESelectionMode::None)
								.HeaderRow
								(
									SNew(SHeaderRow)
									+ SHeaderRow::Column("Slot").DefaultLabel(FText::FromString("Slot")).ManualWidth(48.0f)
									+ SHeaderRow::Column("Name").DefaultLabel(FText::FromString("Name"))
								)
							]
						]
					]
				]
			]
		];

	MaterialEditorInstance = InMaterialEditorInstance;
	Refresh();
}

TSharedRef<ITableRow> SMaterialCustomPrimitiveDataPanel::OnGenerateRowForList(TSharedPtr<FCustomPrimitiveDataRowData> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SCustomPrimitiveDataRow, OwnerTable).Entry(Item);
}
