// Copyright Epic Games, Inc. All Rights Reserved.

#include "SFilterConfiguratorRow.h"

#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"
#include "SlateOptMacros.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"
#include "Widgets/Views/STreeView.h"

// Insights
#include "Insights/InsightsStyle.h"
#include "Insights/ViewModels/Filters.h"

#define LOCTEXT_NAMESPACE "SFilterConfiguratorRow"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SFilterConfiguratorRow::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	FilterConfiguratorNodePtr = InArgs._FilterConfiguratorNodePtr;
	SMultiColumnTableRow<FFilterConfiguratorNodePtr>::Construct(SMultiColumnTableRow<FFilterConfiguratorNodePtr>::FArguments(), InOwnerTableView);
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SFilterConfiguratorRow::GenerateWidgetForColumn(const FName& InColumnName)
{
	TSharedRef<SHorizontalBox> GeneratedWidget = SNew(SHorizontalBox);
	if (!FilterConfiguratorNodePtr->IsGroup())
	{
		GeneratedWidget->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			[
				SNew(SExpanderArrow, SharedThis(this))
				.ShouldDrawWires(true)
			];

		// Filter combo box
		GeneratedWidget->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
				[
					SAssignNew(FilterTypeComboBox, SComboBox<TSharedPtr<FFilter>>)
					.OptionsSource(GetAvailableFilters())
					.OnSelectionChanged(this, &SFilterConfiguratorRow::AvailableFilters_OnSelectionChanged)
					.OnGenerateWidget(this, &SFilterConfiguratorRow::AvailableFilters_OnGenerateWidget)
					[
						SNew(STextBlock)
						.Text(this, &SFilterConfiguratorRow::AvailableFilters_GetSelectionText)
					]
				]
			];

		// Operator combo box
		GeneratedWidget->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.FillWidth(1.0)
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
				[
					SAssignNew(FilterOperatorComboBox, SComboBox<TSharedPtr<IFilterOperator>>)
					.OptionsSource(GetAvailableFilterOperators())
					.OnSelectionChanged(this, &SFilterConfiguratorRow::AvailableFilterOperators_OnSelectionChanged)
					.OnGenerateWidget(this, &SFilterConfiguratorRow::AvailableFilterOperators_OnGenerateWidget)
						[
						SNew(STextBlock)
						.Text(this, &SFilterConfiguratorRow::AvailableFilterOperators_GetSelectionText)
						]
				]
			];

		GeneratedWidget->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.FillWidth(1.0)
			.Padding(2.0f)
			[
				SNew(SEditableTextBox)
				.MinDesiredWidth(50.0f)
				.OnTextCommitted(this, &SFilterConfiguratorRow::OnTextBoxValueCommitted)
				.Text(this, &SFilterConfiguratorRow::GetTextBoxValue)
			];
			
		GeneratedWidget->AddSlot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			.Padding(2.0f)
			.AutoWidth()
			[
				SNew(SButton)
				.ToolTipText(LOCTEXT("DeleteFilterTooptip", "Delete Filter"))
				.ContentPadding(FMargin(2.0f, 0.0f, 2.0f, -1.0f))
				.OnClicked(this, &SFilterConfiguratorRow::DeleteFilter_OnClicked)
				.Content()
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.0f, 0.0f, 1.0f)))
					.Image(FInsightsStyle::Get().GetBrush("Mem.Remove.Small"))
				]
			];
	}
	else
	{
		OwnerTablePtr.Pin()->Private_SetItemExpansion(FilterConfiguratorNodePtr, true);

		GeneratedWidget->AddSlot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(2.0f)
			[
				SNew(SExpanderArrow, SharedThis(this))
				.ShouldDrawWires(true)
			];

		GeneratedWidget->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SBorder)
				.Padding(0)
				.BorderImage(FEditorStyle::GetBrush("NoBorder"))
				.IsEnabled(FSlateApplication::Get().GetNormalExecutionAttribute())
				[
					SAssignNew(FilterGroupOperatorComboBox, SComboBox<TSharedPtr<FFilterGroupOperator>>)
					.OptionsSource(GetFilterGroupOperators())
					.OnSelectionChanged(this, &SFilterConfiguratorRow::FilterGroupOperators_OnSelectionChanged)
					.OnGenerateWidget(this, &SFilterConfiguratorRow::FilterGroupOperators_OnGenerateWidget)
					[
						SNew(STextBlock)
						.Text(this, &SFilterConfiguratorRow::FilterGroupOperators_GetSelectionText)
					]
				]
			];
		
		GeneratedWidget->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddFilter", "Add Filter"))
				.ToolTipText(LOCTEXT("AddFilterDesc", "Add a filter node as a child to this group node."))
				.OnClicked(this, &SFilterConfiguratorRow::AddFilter_OnClicked)
			];

		GeneratedWidget->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(2.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("AddGroup", "Add Group"))
				.ToolTipText(LOCTEXT("AddGroupDesc", "Add a group node as a child to this group node."))
				.OnClicked(this, &SFilterConfiguratorRow::AddGroup_OnClicked)
			];

		// Do not show Delete button for the Root node
		if (FilterConfiguratorNodePtr->GetGroupPtr().IsValid())
		{
			GeneratedWidget->AddSlot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Left)
				.Padding(2.0f)
				.AutoWidth()
				[
					SNew(SButton)
					.ToolTipText(LOCTEXT("DeleteGroupTooptip", "Delete Group"))
					.ContentPadding(FMargin(2.0f, 0.0f, 2.0f, -1.0f))
					.OnClicked(this, &SFilterConfiguratorRow::DeleteGroup_OnClicked)
					.Content()
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor(FLinearColor(0.5f, 0.0f, 0.0f, 1.0f)))
						.Image(FInsightsStyle::Get().GetBrush("Mem.Remove.Small"))
					]
				];
		}
	}

	return GeneratedWidget;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SFilterConfiguratorRow::AvailableFilters_OnGenerateWidget(TSharedPtr<FFilter> InFilter)
{
	TSharedRef<SHorizontalBox> Widget = SNew(SHorizontalBox);
	Widget->AddSlot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(InFilter->Name)
			.Margin(2.0f)
		];

	return Widget;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SFilterConfiguratorRow::AvailableFilterOperators_OnGenerateWidget(TSharedPtr<IFilterOperator> InFilterOperator)
{
	TSharedRef<SHorizontalBox> Widget = SNew(SHorizontalBox);
	Widget->AddSlot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(FText::FromString(InFilterOperator->GetName()))
			.Margin(2.0f)
		];

	return Widget;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SWidget> SFilterConfiguratorRow::FilterGroupOperators_OnGenerateWidget(TSharedPtr<FFilterGroupOperator> InFilterGroupOperator)
{
	TSharedRef<SHorizontalBox> Widget = SNew(SHorizontalBox);
	Widget->SetToolTipText(InFilterGroupOperator->Desc);
	Widget->AddSlot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.Text(InFilterGroupOperator->Name)
			.Margin(2.0f)
		];

	return Widget;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

const TArray<TSharedPtr<struct FFilter>>* SFilterConfiguratorRow::GetAvailableFilters()
{
	return &*FilterConfiguratorNodePtr->GetAvailableFilters();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SFilterConfiguratorRow::AvailableFilters_OnSelectionChanged(TSharedPtr<FFilter> InFilter, ESelectInfo::Type SelectInfo)
{
	FilterConfiguratorNodePtr->SetSelectedFilter(InFilter);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FText SFilterConfiguratorRow::AvailableFilters_GetSelectionText() const
{
	return FilterConfiguratorNodePtr->GetSelectedFilter()->Name;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

const TArray<TSharedPtr<IFilterOperator>>* SFilterConfiguratorRow::GetAvailableFilterOperators()
{
	return FilterConfiguratorNodePtr->GetAvailableFilterOperators().Get();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SFilterConfiguratorRow::AvailableFilterOperators_OnSelectionChanged(TSharedPtr<IFilterOperator> InFilter, ESelectInfo::Type SelectInfo)
{
	FilterConfiguratorNodePtr->SetSelectedFilterOperator(InFilter);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FText SFilterConfiguratorRow::AvailableFilterOperators_GetSelectionText() const
{
	return FText::FromString(FilterConfiguratorNodePtr->GetSelectedFilterOperator()->GetName());
}

///////////////////////////////////////////////////////////////////////////////////////////////////

const TArray<TSharedPtr<FFilterGroupOperator>>* SFilterConfiguratorRow::GetFilterGroupOperators()
{
	return &FilterConfiguratorNodePtr->GetFilterGroupOperators();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SFilterConfiguratorRow::FilterGroupOperators_OnSelectionChanged(TSharedPtr<FFilterGroupOperator> InFilterGroupOperator, ESelectInfo::Type SelectInfo)
{
	FilterConfiguratorNodePtr->SetSelectedFilterGroupOperator(InFilterGroupOperator);
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FText SFilterConfiguratorRow::FilterGroupOperators_GetSelectionText() const
{
	return FilterConfiguratorNodePtr->GetSelectedFilterGroupOperator()->Name;
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFilterConfiguratorRow::AddFilter_OnClicked()
{
	FFilterConfiguratorNodePtr ChildNode = MakeShared<FFilterConfiguratorNode>(TEXT(""), false);
	ChildNode->SetAvailableFilters(FilterConfiguratorNodePtr->GetAvailableFilters());

	FilterConfiguratorNodePtr->AddChildAndSetGroupPtr(ChildNode);
	FilterConfiguratorNodePtr->SetExpansion(true);
	OwnerTablePtr.Pin()->Private_SetItemExpansion(FilterConfiguratorNodePtr, true);

	StaticCastSharedPtr<STreeView<FFilterConfiguratorNodePtr>>(OwnerTablePtr.Pin())->RequestTreeRefresh();
	return FReply::Handled();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFilterConfiguratorRow::DeleteFilter_OnClicked()
{
	Insights::FBaseTreeNodeWeak GroupWeakPtr = FilterConfiguratorNodePtr->GetGroupPtr();
	if (GroupWeakPtr.IsValid())
	{
		
		FFilterConfiguratorNodePtr GroupPtr = StaticCastSharedPtr<FFilterConfiguratorNode>(GroupWeakPtr.Pin());
		GroupPtr->DeleteChildNode(FilterConfiguratorNodePtr);
	}

	StaticCastSharedPtr<STreeView<FFilterConfiguratorNodePtr>>(OwnerTablePtr.Pin())->RequestTreeRefresh();
	return FReply::Handled();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFilterConfiguratorRow::AddGroup_OnClicked()
{
	FFilterConfiguratorNodePtr ChildNode = MakeShared<FFilterConfiguratorNode>(TEXT(""), true);
	ChildNode->SetAvailableFilters(FilterConfiguratorNodePtr->GetAvailableFilters());
	ChildNode->SetExpansion(true);

	FilterConfiguratorNodePtr->AddChildAndSetGroupPtr(ChildNode);
	FilterConfiguratorNodePtr->SetExpansion(true);
	OwnerTablePtr.Pin()->Private_SetItemExpansion(FilterConfiguratorNodePtr, true);

	StaticCastSharedPtr<STreeView<FFilterConfiguratorNodePtr>>(OwnerTablePtr.Pin())->RequestTreeRefresh();
	return FReply::Handled();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FReply SFilterConfiguratorRow::DeleteGroup_OnClicked()
{
	Insights::FBaseTreeNodeWeak GroupWeakPtr = FilterConfiguratorNodePtr->GetGroupPtr();
	if (GroupWeakPtr.IsValid())
	{

		FFilterConfiguratorNodePtr GroupPtr = StaticCastSharedPtr<FFilterConfiguratorNode>(GroupWeakPtr.Pin());
		GroupPtr->DeleteChildNode(FilterConfiguratorNodePtr);
	}

	StaticCastSharedPtr<STreeView<FFilterConfiguratorNodePtr>>(OwnerTablePtr.Pin())->RequestTreeRefresh();
	return FReply::Handled();
}

///////////////////////////////////////////////////////////////////////////////////////////////////

FText SFilterConfiguratorRow::GetTextBoxValue() const
{
	return FText::FromString(FilterConfiguratorNodePtr->GetTextBoxValue());
}

///////////////////////////////////////////////////////////////////////////////////////////////////

void SFilterConfiguratorRow::OnTextBoxValueCommitted(const FText& InNewText, ETextCommit::Type InTextCommit)
{
	FilterConfiguratorNodePtr->SetTextBoxValue(InNewText.ToString());
}

///////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
