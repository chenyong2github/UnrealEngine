// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXPixelMappingPaletteItem.h"
#include "ViewModels/DMXPixelMappingPaletteViewModel.h"
#include "Templates/DMXPixelMappingComponentTemplate.h"
#include "DragDrop/DMXPixelMappingDragDropOp.h"

#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"


void SDMXPixelMappingHierarchyItemHeader::Construct(const FArguments& InArgs, const TSharedRef< STableViewBase >& InOwnerTableView, const TSharedPtr<FDMXPixelMappingPaletteWidgetViewModel>& InViewModel)
{
	STableRow<FDMXPixelMappingPreviewWidgetViewModelPtr>::Construct(
		STableRow<FDMXPixelMappingPreviewWidgetViewModelPtr>::FArguments()
		.Padding(1.0f)
		.Style(FEditorStyle::Get(), "UMGEditor.PaletteHeader")
		.Content()
		[
			SNew(STextBlock)
			.Text(InViewModel->GetName())
		],
		InOwnerTableView);
}

void SDMXPixelMappingHierarchyItemTemplate::Construct(const FArguments& InArgs, const TSharedRef< STableViewBase >& InOwnerTableView, const TSharedPtr<FDMXPixelMappingPaletteWidgetViewModel>& InViewModel)
{
	ViewModel = InViewModel;

	STableRow<FDMXPixelMappingPreviewWidgetViewModelPtr>::Construct(
		STableRow<FDMXPixelMappingPreviewWidgetViewModelPtr>::FArguments()
		.Padding(1.0f)
		.Style(FEditorStyle::Get(), "UMGEditor.PaletteHeader")
		.ShowSelection(false)
		.OnDragDetected(this, &SDMXPixelMappingHierarchyItemTemplate::OnDraggingWidget)
		.Content()
		[
			SNew(STextBlock)
			.Text(InViewModel->GetName())
		],
		InOwnerTableView);

}

FReply SDMXPixelMappingHierarchyItemTemplate::OnDraggingWidget(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	check(ViewModel.IsValid());

	if (FDMXPixelMappingComponentTemplatePtr ComponentTemplatePtr = ViewModel.Pin()->GetTemplate())
	{
		const FVector2D DragOffset = FVector2D::ZeroVector;
		TArray<FDMXPixelMappingComponentTemplatePtr> TemplateArray = TArray<FDMXPixelMappingComponentTemplatePtr>({ ComponentTemplatePtr });

		return FReply::Handled().BeginDragDrop(FDMXPixelMappingDragDropOp::New(DragOffset, TemplateArray, nullptr));
	}

	return FReply::Unhandled();
}
