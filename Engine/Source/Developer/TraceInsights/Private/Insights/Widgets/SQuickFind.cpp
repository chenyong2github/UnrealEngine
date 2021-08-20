// Copyright Epic Games, Inc. All Rights Reserved.

#include "SQuickFind.h"

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
#include "Insights/Widgets/SFilterConfigurator.h"
#include "Insights/ViewModels/FilterConfigurator.h"
#include "Insights/ViewModels/QuickFind.h"

#define LOCTEXT_NAMESPACE "SQuickFind"

namespace Insights
{

////////////////////////////////////////////////////////////////////////////////////////////////////
// SQuickFind
////////////////////////////////////////////////////////////////////////////////////////////////////

FName const SQuickFind::QuickFindTabId(TEXT("QuickFilter"));
TSharedPtr<SQuickFind> SQuickFind::PendingWidget;
bool SQuickFind::bIsTabRegistered = false;


SQuickFind::SQuickFind()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

SQuickFind::~SQuickFind()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SQuickFind::InitCommandList()
{
}

////////////////////////////////////////////////////////////////////////////////////////////////////

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void SQuickFind::Construct(const FArguments& InArgs, TSharedPtr<FQuickFind> InQuickFindViewModel)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		// Tree view
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SAssignNew(FilterConfigurator, SFilterConfigurator, InQuickFindViewModel->GetFilterConfigurator())
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 5.0f, 0.0f, 5.0f)
		.HAlign(EHorizontalAlignment::HAlign_Left)
		.VAlign(EVerticalAlignment::VAlign_Bottom)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("FindPrev", "Find Previous"))
				.ToolTipText(LOCTEXT("FindNextDesc", "Find the previous occurence that matches the search criteria."))
				.OnClicked(this, &SQuickFind::FindPrevious_OnClicked)
			]

			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.AutoWidth()
			.Padding(1.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("FindNext", "Find Next"))
				.ToolTipText(LOCTEXT("FindNextDesc", "Find the next occurence that matches the search criteria."))
				.OnClicked(this, &SQuickFind::FindNext_OnClicked)
			]

			+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("FilterAll", "Filter All"))
					.ToolTipText(LOCTEXT("FilterAllDesc", "Filter all the tracks using the the search criteria."))
					.OnClicked(this, &SQuickFind::FilterAll_OnClicked)
				]

			+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.AutoWidth()
				.Padding(1.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearFilters", "Clear Filters"))
					.ToolTipText(LOCTEXT("ClearFiltersDesc", "Clear all filters applied to the tracks."))
					.OnClicked(this, &SQuickFind::ClearFilters_OnClicked)
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

	QuickFindViewModel = InQuickFindViewModel;
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SQuickFind::FindNext_OnClicked()
{
	QuickFindViewModel->GetFilterConfigurator()->GetRootNode()->ProcessFilter();
	QuickFindViewModel->GetOnFindNextEvent().Broadcast();

	return FReply::Handled();
}////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SQuickFind::FindPrevious_OnClicked()
{
	QuickFindViewModel->GetFilterConfigurator()->GetRootNode()->ProcessFilter();
	QuickFindViewModel->GetOnFindPreviousEvent().Broadcast();

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SQuickFind::FilterAll_OnClicked()
{
	QuickFindViewModel->GetFilterConfigurator()->GetRootNode()->ProcessFilter();
	QuickFindViewModel->GetOnFilterAllEvent().Broadcast();

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

FReply SQuickFind::ClearFilters_OnClicked()
{
	QuickFindViewModel->GetOnClearFiltersEvent().Broadcast();

	return FReply::Handled();
}

////////////////////////////////////////////////////////////////////////////////////////////////////

void SQuickFind::RegisterQuickFindTab()
{
	if (bIsTabRegistered)
	{
		return;
	}

	FTabSpawnerEntry& TabSpawnerEntry = FGlobalTabmanager::Get()->RegisterNomadTabSpawner(QuickFindTabId,
		FOnSpawnTab::CreateStatic(&SQuickFind::SpawnTab))
		.SetDisplayName(LOCTEXT("FilterConfiguratorTabTitle", "Filter Configurator"))
		.SetMenuType(ETabSpawnerMenuType::Hidden);

	bIsTabRegistered = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedRef<SDockTab> SQuickFind::SpawnTab(const FSpawnTabArgs& Args)
{
	const TSharedRef<SDockTab> DockTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	if (!PendingWidget.IsValid())
	{
		return DockTab;
	}

	DockTab->SetContent(PendingWidget.ToSharedRef());
	PendingWidget->SetParentTab(DockTab);

	PendingWidget = nullptr;
	return DockTab;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

TSharedPtr<SQuickFind> SQuickFind::CreateAndOpenQuickFilterWidget(TSharedPtr<FQuickFind> InQuickFindViewModel)
{
	SAssignNew(PendingWidget, SQuickFind, InQuickFindViewModel);

	if (FGlobalTabmanager::Get()->HasTabSpawner(QuickFindTabId))
	{
		FGlobalTabmanager::Get()->TryInvokeTab(QuickFindTabId);
	}

	return PendingWidget;
}

////////////////////////////////////////////////////////////////////////////////////////////////////

} // namespace Insights

#undef LOCTEXT_NAMESPACE
