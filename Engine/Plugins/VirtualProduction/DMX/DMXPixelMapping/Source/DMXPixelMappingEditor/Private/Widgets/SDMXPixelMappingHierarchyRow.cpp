// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXPixelMappingHierarchyRow.h"

#include "Components/DMXPixelMappingOutputComponent.h"
#include "DMXEditorStyle.h"
#include "DMXPixelMappingEditorUtils.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "ScopedTransaction.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Views/SListView.h"
#include "ViewModels/DMXPixelMappingHierarchyItem.h"
#include "Views/SDMXPixelMappingHierarchyView.h"


#define LOCTEXT_NAMESPACE "SDMXPixelMappingHierarchyRow"

void SDMXPixelMappingHierarchyRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakPtr<FDMXPixelMappingToolkit> InWeakToolkit, const TSharedRef<FDMXPixelMappingHierarchyItem>& InItem)
{
	WeakToolkit = InWeakToolkit;
	Item = InItem;

	SMultiColumnTableRow<FDMXPixelMappingHierarchyItemWidgetModelPtr>::Construct(
		FSuperRowType::FArguments()
		.Padding(0.0f)
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SimpleTableView.Row"))
		.OnDragDetected(this, &SDMXPixelMappingHierarchyRow::OnRowDragDetected),
		InOwnerTableView);
}

void SDMXPixelMappingHierarchyRow::EnterRenameMode()
{
	EditableNameTextBox->EnterEditingMode();
}

TSharedRef<SWidget> SDMXPixelMappingHierarchyRow::GenerateWidgetForColumn(const FName& ColumnName)
{
	if (ColumnName == SDMXPixelMappingHierarchyView::FColumnIds::EditorColor)
	{
		return GenerateEditorColorWidget();
	}
	else if (ColumnName == SDMXPixelMappingHierarchyView::FColumnIds::ComponentName)
	{			
		// The name column gets the tree expansion arrow
		return SNew(SBox)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SExpanderArrow, SharedThis(this))
				]

				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					GenerateComponentNameWidget()
				]
			];
	}
	else if (ColumnName == SDMXPixelMappingHierarchyView::FColumnIds::FixtureID)
	{
		return GenerateFixtureIDWidget();
	}
	else if (ColumnName == SDMXPixelMappingHierarchyView::FColumnIds::Patch)
	{
		return GeneratePatchWidget();
	}

	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SDMXPixelMappingHierarchyRow::GenerateEditorColorWidget()
{
	if (Item.IsValid() && Item->HasEditorColor())
	{
		return
			SNew(SBorder)
			.HAlign(HAlign_Fill)
			.Padding(5.f, 2.f)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			[
				SAssignNew(EditorColorImageWidget, SImage)
				.Image(FDMXEditorStyle::Get().GetBrush("DMXEditor.WhiteRoundedPropertyBorder"))
			.ColorAndOpacity_Lambda([this]() -> FSlateColor
				{
					return Item.IsValid() ? Item->GetEditorColor() : FLinearColor::Red;
				})
			];
	}

	return SNullWidget::NullWidget;
}

TSharedRef<SWidget> SDMXPixelMappingHierarchyRow::GenerateComponentNameWidget()
{
	return
		SNew(SBorder)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.Padding(4.f)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		[
			SAssignNew(EditableNameTextBox, SInlineEditableTextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text(Item.Get(), &FDMXPixelMappingHierarchyItem::GetComponentNameText)
			.ColorAndOpacity(FLinearColor::White)
		];
}

TSharedRef<SWidget> SDMXPixelMappingHierarchyRow::GenerateFixtureIDWidget()
{
	return
		SNew(SBorder)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.Padding(4.f)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text(Item.Get(), &FDMXPixelMappingHierarchyItem::GetFixtureIDText)
			.ColorAndOpacity(FLinearColor::White)
		];
}

TSharedRef<SWidget> SDMXPixelMappingHierarchyRow::GeneratePatchWidget()
{
	return
		SNew(SBorder)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Center)
		.Padding(4.f)
		.BorderImage(FAppStyle::GetBrush("NoBorder"))
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.Text(Item.Get(), &FDMXPixelMappingHierarchyItem::GetPatchText)
			.ColorAndOpacity(FLinearColor::White)
		];
}

void SDMXPixelMappingHierarchyRow::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
{
	const TSharedPtr<FDMXPixelMappingToolkit> Toolkit = WeakToolkit.Pin();
	const UDMXPixelMappingBaseComponent* Component = Item.IsValid() ? Item->GetComponent() : nullptr;
	if (Toolkit.IsValid() && Component && !InText.IsEmpty())
	{
		const FScopedTransaction RenameComponentTransaction(LOCTEXT("RenameComponentTransaction", "Rename Pixel Mapping Component"));

		const FName& ComponentName = Component->GetFName();
		Toolkit->RenameComponent(ComponentName, InText.ToString());
	}
}

FReply SDMXPixelMappingHierarchyRow::OnRowDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	// SMultiColumnTableRow for some reason does not provide the mouse key, so testing for LeftMouseButton is not required.
	if (!WeakToolkit.IsValid())
	{
		return FReply::Unhandled();
	}

	const TSet<FDMXPixelMappingComponentReference>& SelectedComponents = WeakToolkit.Pin()->GetSelectedComponents();
	TArray<TWeakObjectPtr<UDMXPixelMappingBaseComponent>> DraggedComponents;
	Algo::TransformIf(SelectedComponents, DraggedComponents,
		[](const FDMXPixelMappingComponentReference& ComponentReference)
		{
			return ComponentReference.IsValid();
		},
		[](const FDMXPixelMappingComponentReference& ComponentReference)
		{
			return ComponentReference.GetComponent();
		}
	);

	return FReply::Handled().BeginDragDrop(FDMXPixelMappingDragDropOp::New(FVector2D::ZeroVector, DraggedComponents));
}

#undef LOCTEXT_NAMESPACE
