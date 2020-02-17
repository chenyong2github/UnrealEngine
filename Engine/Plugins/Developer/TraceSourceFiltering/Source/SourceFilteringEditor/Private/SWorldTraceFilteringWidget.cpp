// Copyright Epic Games, Inc. All Rights Reserved.

#include "SWorldTraceFilteringWidget.h"

#include "Widgets/Input/SComboButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "EditorStyleSet.h"
#include "Internationalization/Internationalization.h"
#include "Algo/Transform.h"
#include "Widgets/Views/SListView.h"

#include "SWorldObjectWidget.h"
#include "SourceFilterStyle.h"
#include "ISessionSourceFilterService.h"
#include "WorldObject.h"

#define LOCTEXT_NAMESPACE "SWorldFilterWidget"

void SWorldTraceFilteringWidget::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SBorder)
		.Padding(4.0f)
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FMargin(0.0f, 0.0f, 0.0f, 2.0f))
			[			
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SAssignNew(OptionsComboBox, SComboButton)
					.Visibility(EVisibility::Visible)
					.ComboButtonStyle(FSourceFilterStyle::Get(), "SourceFilter.ComboButton")
					.ForegroundColor(FLinearColor::White)
					.ContentPadding(0.0f)
					.OnGetMenuContent(this, &SWorldTraceFilteringWidget::OnGetMenuContextMenu)					
					.ButtonContent()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.TextStyle(FSourceFilterStyle::Get(), "SourceFilter.TextStyle")
							.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
							.Text(FText::FromString(FString(TEXT("\xf0fe"))) /*fa-filter*/)
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0, 0, 0)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.TextStyle(FSourceFilterStyle::Get(), "SourceFilter.TextStyle")
							.Text(LOCTEXT("OptionsMenuLabel", "Options"))
						]
					]
				]
			]

			+ SVerticalBox::Slot()
			[	
				SAssignNew(WorldListView, SListView<TSharedPtr<FWorldObject>>)
				.ItemHeight(20.f)
				.ListItemsSource(&WorldObjects)
				.OnGenerateRow_Lambda([this](TSharedPtr<FWorldObject> InItem, const TSharedRef<STableViewBase>& OwnerTable) -> TSharedRef<ITableRow>
				{
					return SNew(SWorldObjectRowWidget, OwnerTable, InItem, SessionFilterService);
				})
			]	
		]
	];

	TAttribute<bool> EnabledAttribute = TAttribute<bool>::Create([this]() -> bool
	{
		return SessionFilterService.IsValid() && !SessionFilterService->IsActionPending();
	});

	OptionsComboBox->SetEnabled(EnabledAttribute);
	WorldListView->SetEnabled(EnabledAttribute);
}

void SWorldTraceFilteringWidget::SetSessionFilterService(TSharedPtr<ISessionSourceFilterService> InSessionFilterService)
{
	SessionFilterService = InSessionFilterService;
	RefreshWorldData();
}

void SWorldTraceFilteringWidget::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (SessionFilterService.IsValid())
	{
		if (SessionFilterService->GetTimestamp() > TimeStamp)
		{
			RefreshWorldData();
			TimeStamp = SessionFilterService->GetTimestamp();
		}
	}
}

void SWorldTraceFilteringWidget::RefreshWorldData()
{
	WorldObjects.Empty();
	SessionFilterService->GetWorldObjects(WorldObjects);
	WorldListView->RequestListRefresh();
}

TSharedRef<SWidget> SWorldTraceFilteringWidget::OnGetMenuContextMenu()
{
	FMenuBuilder MenuBuilder(true, TSharedPtr<FUICommandList>(), SessionFilterService->GetExtender());
	const TArray<TSharedPtr<IWorldTraceFilter>>& WorldFilters = SessionFilterService->GetWorldFilters();

	if (WorldFilters.Num())
	{
		MenuBuilder.BeginSection(NAME_None, LOCTEXT("WorldFilteringLabel", "World Filtering"));
		{
			for (TSharedPtr<IWorldTraceFilter> WorldFilter : WorldFilters)
			{
				MenuBuilder.AddSubMenu(WorldFilter->GetDisplayText(),
					WorldFilter->GetToolTipText(),
					FNewMenuDelegate::CreateLambda([this, WorldFilter](FMenuBuilder& SubMenuBuilder)
					{
						WorldFilter->PopulateMenuBuilder(SubMenuBuilder);
					}),
					false
				);
			}
		}
		MenuBuilder.EndSection();
	}
		
	return MenuBuilder.MakeWidget();
}

#undef LOCTEXT_NAMESPACE // "SWorldFilterWidget"