// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXPixelMappingHierarchyItem.h"
#include "ViewModels/DMXPixelMappingHierarchyViewModel.h"

#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "EditorStyleSet.h"
#include "Widgets/Views/STableRow.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"
#include "Views/SDMXPixelMappingHierarchyView.h"
#include "ScopedTransaction.h"

void SDMXPixelMappingHierarchyItem::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, TSharedPtr<FDMXPixelMappingHierarchyItemWidgetModel> InModel, TSharedPtr<SDMXPixelMappingHierarchyView> InHierarchyView)
{
	Model = InModel;
	HierarchyView = InHierarchyView;

	InModel->RenameEvent.BindSP(this, &SDMXPixelMappingHierarchyItem::OnRequestBeginRename);

	STableRow<FDMXPixelMappingHierarchyItemWidgetModelPtr>::Construct(
		STableRow<FDMXPixelMappingHierarchyItemWidgetModelPtr>::FArguments()
		.Padding(0.0f)
		.Style(FEditorStyle::Get(), "UMGEditor.PaletteHeader")
		.OnDragDetected(this, &SDMXPixelMappingHierarchyItem::OnDraggingWidget)
		.OnDrop(this, &SDMXPixelMappingHierarchyItem::OnDropWidget)
		.Content()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(2, 0, 0, 0)
			.VAlign(VAlign_Center)
			[
				SAssignNew(EditBox, SInlineEditableTextBlock)
				.Text(this, &SDMXPixelMappingHierarchyItem::GetItemText)
				.OnVerifyTextChanged(this, &SDMXPixelMappingHierarchyItem::OnVerifyNameTextChanged)
				.OnTextCommitted(this, &SDMXPixelMappingHierarchyItem::OnNameTextCommited)
				.IsEnabled(true)
			]
		],
		InOwnerTableView);
}

FText SDMXPixelMappingHierarchyItem::GetItemText() const
{
	if (FDMXPixelMappingHierarchyItemWidgetModelPtr ModelPtr = Model.Pin())
	{
		return ModelPtr->GetText();
	}

	return FText();
}

bool SDMXPixelMappingHierarchyItem::OnVerifyNameTextChanged(const FText& InText, FText& OutErrorMessage)
{
	return Model.Pin()->OnVerifyNameTextChanged(InText, OutErrorMessage);
}

void SDMXPixelMappingHierarchyItem::OnNameTextCommited(const FText& InText, ETextCommit::Type CommitInfo)
{
	Model.Pin()->OnNameTextCommited(InText, CommitInfo);
}

void SDMXPixelMappingHierarchyItem::OnRequestBeginRename()
{
	if (EditBox.IsValid())
	{
		EditBox->EnterEditingMode();
	}
}

FReply SDMXPixelMappingHierarchyItem::OnDraggingWidget(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	check(Model.IsValid());

	UDMXPixelMappingBaseComponent* Component = Model.Pin()->GetReference().GetComponent();

	TArray<TWeakObjectPtr<UDMXPixelMappingBaseComponent>> DraggedComponents;
	DraggedComponents.Add(Component);
	return FReply::Handled().BeginDragDrop(FDMXPixelMappingDragDropOp::New(FVector2D::ZeroVector, DraggedComponents));
}

FReply SDMXPixelMappingHierarchyItem::OnDropWidget(const FDragDropEvent& InDragDropEvent)
{
	const FScopedTransaction Transaction(FText::FromString("MoveComponent"));
	TSharedPtr<FDMXPixelMappingDragDropOp> ComponentDragDropOp = InDragDropEvent.GetOperationAs<FDMXPixelMappingDragDropOp>();
	if (ComponentDragDropOp.IsValid()
		&& HierarchyView.IsValid())
	{
		for (const TWeakObjectPtr<UDMXPixelMappingBaseComponent>& WeakSource : ComponentDragDropOp->GetDraggedComponents())
		{
			if (UDMXPixelMappingBaseComponent* Source = WeakSource.Get())
			{
				UDMXPixelMappingBaseComponent* Destination = Model.Pin()->GetReference().GetComponent();

				UDMXPixelMappingBaseComponent* NewParent = nullptr;
				if (Source->CanBeMovedTo(Destination))
				{
					NewParent = Destination;
				}
				else if (Source->CanBeMovedTo(Destination->GetParent()))
				{
					NewParent = Destination->GetParent();
				}

				if (NewParent)
				{
					NewParent->Modify();
					Source->Modify();
					NewParent->AddChild(Source);

					if (Source->GetParent())
					{
						Source->GetParent()->Modify();
						Source->GetParent()->RemoveChild(Source);
					}

					HierarchyView->RequestComponentRedraw(NewParent);
					HierarchyView->RequestRebuildTree();
				}
			}
		}		
	}
	return FReply::Unhandled();
}

