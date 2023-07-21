// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXPixelMappingHierarchyItem.h"

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


#define LOCTEXT_NAMESPACE "SDMXPixelMappingHierarchyItem"

void SDMXPixelMappingHierarchyItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TWeakPtr<FDMXPixelMappingToolkit> InWeakToolkit, const TSharedRef<FDMXPixelMappingHierarchyItem>& InItem)
{
	WeakToolkit = InWeakToolkit;
	Item = InItem;

	SMultiColumnTableRow<FDMXPixelMappingHierarchyItemWidgetModelPtr>::Construct(
		FSuperRowType::FArguments()
		.Padding(0.0f)
		.Style(&FAppStyle::Get().GetWidgetStyle<FTableRowStyle>("SimpleTableView.Row"))
		.OnDragDetected(this, &SDMXPixelMappingHierarchyItem::OnDraggingWidget)
		.OnDrop(this, &SDMXPixelMappingHierarchyItem::OnDropWidget),
		InOwnerTableView);
}

void SDMXPixelMappingHierarchyItem::EnterRenameMode()
{
	EditableNameTextBox->EnterEditingMode();
}

TSharedRef<SWidget> SDMXPixelMappingHierarchyItem::GenerateWidgetForColumn(const FName& ColumnName)
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

TSharedRef<SWidget> SDMXPixelMappingHierarchyItem::GenerateEditorColorWidget()
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

TSharedRef<SWidget> SDMXPixelMappingHierarchyItem::GenerateComponentNameWidget()
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

TSharedRef<SWidget> SDMXPixelMappingHierarchyItem::GenerateFixtureIDWidget()
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

TSharedRef<SWidget> SDMXPixelMappingHierarchyItem::GeneratePatchWidget()
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

void SDMXPixelMappingHierarchyItem::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
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

FReply SDMXPixelMappingHierarchyItem::OnDraggingWidget(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	UDMXPixelMappingBaseComponent* Component = Item.IsValid() ? Item->GetComponent() : nullptr;
	if (Component)
	{
		TArray<TWeakObjectPtr<UDMXPixelMappingBaseComponent>> DraggedComponents;
		DraggedComponents.Add(Component);

		return FReply::Handled().BeginDragDrop(FDMXPixelMappingDragDropOp::New(FVector2D::ZeroVector, DraggedComponents));
	};

	return FReply::Unhandled();
}

FReply SDMXPixelMappingHierarchyItem::OnDropWidget(const FDragDropEvent& InDragDropEvent)
{
	const FScopedTransaction Transaction(FText::FromString("MoveComponent"));
	TSharedPtr<FDMXPixelMappingDragDropOp> ComponentDragDropOp = InDragDropEvent.GetOperationAs<FDMXPixelMappingDragDropOp>();
	if (ComponentDragDropOp.IsValid())
	{
		for (const TWeakObjectPtr<UDMXPixelMappingBaseComponent>& WeakSource : ComponentDragDropOp->GetDraggedComponents())
		{
			if (UDMXPixelMappingBaseComponent* Source = WeakSource.Get())
			{
				UDMXPixelMappingBaseComponent* Destination = Item->GetComponent();

				UDMXPixelMappingBaseComponent* NewParent = nullptr;
				if (Source->CanBeMovedTo(Destination))
				{
					NewParent = Destination;
				}
				else if (Source->CanBeMovedTo(Destination->GetParent()))
				{
					NewParent = Destination->GetParent();
				}

				const bool bOldParentIsNewParent = NewParent == Source->GetParent();
				if (NewParent && !bOldParentIsNewParent)
				{
					// Add to the new parent
					NewParent->Modify();
					Source->Modify();
					NewParent->AddChild(Source);

					// Remove from the old parent
					if (Source->GetParent())
					{
						Source->GetParent()->Modify();
						Source->GetParent()->RemoveChild(Source);
					}

					// Adopt location and size of new parent if required
					if (UDMXPixelMappingOutputComponent* ParentOutputComponent = Cast<UDMXPixelMappingOutputComponent>(NewParent))
					{
						if (UDMXPixelMappingOutputComponent* ChildOutputComponent = Cast<UDMXPixelMappingOutputComponent>(Source))
						{
							if (!ChildOutputComponent->IsOverParent())
							{
								const FVector2D ParentSize = ParentOutputComponent->GetSize();
								const FVector2D ChildSize = ChildOutputComponent->GetSize();
								const FVector2D NewChildSize = FVector2D(FMath::Min(ParentSize.X, ChildSize.X), FMath::Min(ParentSize.Y, ChildSize.Y));
								ChildOutputComponent->SetSize(NewChildSize);

								ChildOutputComponent->SetPosition(ParentOutputComponent->GetPosition());
							}
						}
					}
				}
			}
		}
	}
	return FReply::Unhandled();
}

#undef LOCTEXT_NAMESPACE
