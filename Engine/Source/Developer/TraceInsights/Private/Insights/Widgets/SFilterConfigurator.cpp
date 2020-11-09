// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFilterConfigurator.h"

#include "EditorStyleSet.h"
#include "SlateOptMacros.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableViewBase.h"

// Insights
#include "Insights/Widgets/SFilterConfiguratorRow.h"
#include "Insights/ViewModels/FilterConfigurator.h"

#define LOCTEXT_NAMESPACE "SFilterConfigurator"

////////////////////////////////////////////////////////////////////////////////////////////////////
// SFilterConfigurator
////////////////////////////////////////////////////////////////////////////////////////////////////

SFilterConfigurator::SFilterConfigurator()
{

}

////////////////////////////////////////////////////////////////////////////////////////////////////

SFilterConfigurator::~SFilterConfigurator()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFilterConfigurator::InitCommandList()
{
	
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SFilterConfigurator::Construct(const FArguments& InArgs, TSharedPtr<FFilterConfigurator> InFilterConfiguratorViewModel)
{
	SAssignNew(ExternalScrollbar, SScrollBar)
		.AlwaysShowScrollbar(true);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Tree view
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f)
			[
				SNew(SScrollBox)
				.Orientation(Orient_Horizontal)

				+ SScrollBox::Slot()
				[
					SNew(SOverlay)

					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Fill)
					[
						SAssignNew(TreeView, STreeView<FFilterConfiguratorNodePtr>)
						.ExternalScrollbar(ExternalScrollbar)
						.SelectionMode(ESelectionMode::None)
						.TreeItemsSource(&GroupNodes)
						.OnGetChildren(this, &SFilterConfigurator::TreeView_OnGetChildren)
						.OnGenerateRow(this, &SFilterConfigurator::TreeView_OnGenerateRow)
						.ItemHeight(12.0f)
						.HeaderRow
						(
							SAssignNew(TreeViewHeaderRow, SHeaderRow)
							.Visibility(EVisibility::Visible)
						)
					]
				]
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f)
			[
					SNew(SBox)
					.WidthOverride(FOptionalSize(13.0f))
				[
					ExternalScrollbar.ToSharedRef()
				]
			]
		]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 5.0f, 0.0f, 5.0f)
			.HAlign(EHorizontalAlignment::HAlign_Right)
			.VAlign(EVerticalAlignment::VAlign_Bottom)
			[
				SNew(SHorizontalBox)

				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("OK", "OK"))
					.ToolTipText(LOCTEXT("OKDESC", "Apply the changes to the filters."))
					.OnClicked(this, &SFilterConfigurator::OK_OnClicked)
				]

				+SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("Cancel", "Cancel"))
					.ToolTipText(LOCTEXT("CancelDesc", "Cancel the changes to the filter."))
					.OnClicked(this, &SFilterConfigurator::Cancel_OnClicked)
				]
			]
	];

	SHeaderRow::FColumn::FArguments ColumnArgs;
	ColumnArgs
		.ColumnId(TEXT("Filter"))
		.DefaultLabel(LOCTEXT("FilterColumnHeader", "Filter"))
		.HAlignHeader(HAlign_Fill)
		.VAlignHeader(VAlign_Fill)
		.HeaderContentPadding(FMargin(2.0f))
		.HAlignCell(HAlign_Fill)
		.VAlignCell(VAlign_Fill)
		.HeaderContent()
		[
			SNew(SBox)
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("FilterColumnHeader", "Filter"))
			]
		];

	TreeViewHeaderRow->InsertColumn(ColumnArgs, 0);

	OnViewModelDestroyedHandle = InFilterConfiguratorViewModel->GetOnDestroyedEvent().AddSP(this, &SFilterConfigurator::RequestClose);
	OriginalFilterConfiguratorViewModel = InFilterConfiguratorViewModel;
	FilterConfiguratorViewModel = MakeShared<FFilterConfigurator>(*InFilterConfiguratorViewModel);

	GroupNodes.Add(FilterConfiguratorViewModel->GetRootNode());
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFilterConfigurator::TreeView_OnGetChildren(FFilterConfiguratorNodePtr InParent, TArray<FFilterConfiguratorNodePtr>& OutChildren)
{
	const TArray<Insights::FBaseTreeNodePtr>& Children = InParent->GetChildren();
	OutChildren.Reset(Children.Num());
	for (const Insights::FBaseTreeNodePtr& Child : Children)
	{
		OutChildren.Add(StaticCastSharedPtr<FFilterConfiguratorNode, Insights::FBaseTreeNode>(Child));
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<ITableRow> SFilterConfigurator::TreeView_OnGenerateRow(FFilterConfiguratorNodePtr TreeNode, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedRef<ITableRow> TableRow =
		SNew(SFilterConfiguratorRow, OwnerTable)
		.FilterConfiguratorNodePtr(TreeNode);

	return TableRow;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFilterConfigurator::OK_OnClicked()
{
	TSharedPtr<FFilterConfigurator> OriginalFilterVM = OriginalFilterConfiguratorViewModel.Pin();
	if (OriginalFilterVM.IsValid())
	{
		*OriginalFilterVM = *FilterConfiguratorViewModel;
	}

	RequestClose();

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFilterConfigurator::Cancel_OnClicked()
{
	RequestClose();

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SFilterConfigurator::RequestClose()
{
	TSharedPtr<SDockTab> ParentTabSharedPtr = ParentTab.Pin();
	if (ParentTabSharedPtr.IsValid())
	{
		ParentTabSharedPtr->RequestCloseTab();
		ParentTabSharedPtr.Reset();
	}

	TSharedPtr<FFilterConfigurator> OriginalFilterVM = OriginalFilterConfiguratorViewModel.Pin();
	if (OriginalFilterVM.IsValid())
	{
		OriginalFilterVM->GetOnDestroyedEvent().Remove(OnViewModelDestroyedHandle);
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE
