// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DMXPixelMappingEditorCommon.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/DeclarativeSyntaxSupport.h"

class STableViewBase;
class FDMXPixelMappingPalatteWidgetViewModel;
class FDMXPixelMappingComponentTemplate;
class FReply;

class SDMXPixelMappingHierarchyItemHeader
	: public STableRow<TSharedPtr<FDMXPixelMappingPalatteWidgetViewModel>>
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingHierarchyItemHeader) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef< STableViewBase >& InOwnerTableView, const TSharedPtr<FDMXPixelMappingPalatteWidgetViewModel>& InViewModel);
};

class SDMXPixelMappingHierarchyItemTemplate
	: public STableRow<TSharedPtr<FDMXPixelMappingPalatteWidgetViewModel>>
{
public:
	SLATE_BEGIN_ARGS(SDMXPixelMappingHierarchyItemTemplate) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef< STableViewBase >& InOwnerTableView, const TSharedPtr<FDMXPixelMappingPalatteWidgetViewModel>& InViewModel);

private:
	FReply OnDraggingWidget(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent);

private:
	TWeakPtr<FDMXPixelMappingPalatteWidgetViewModel> ViewModel;
};