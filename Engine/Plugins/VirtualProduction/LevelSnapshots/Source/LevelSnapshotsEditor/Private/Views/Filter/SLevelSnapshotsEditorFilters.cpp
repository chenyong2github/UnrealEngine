// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelSnapshotsEditorFilters.h"

#include "DisjunctiveNormalFormFilter.h"
#include "EditorFilter.h"
#include "EditorFontGlyphs.h"
#include "FilteredResults.h"
#include "ILevelSnapshotsEditorView.h"
#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsEditorFilters.h"
#include "LevelSnapshotsEditorStyle.h"
#include "SFavoriteFilterList.h"
#include "SMasterFilterIndicatorButton.h"
#include "SLevelSnapshotsEditorFilterRow.h"
#include "SSaveAndLoadFilters.h"

#include "EditorStyleSet.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IDetailsView.h"
#include "NegatableFilterDetailsCustomization.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

namespace
{
	constexpr float ErrorMessageDisplayTimeInSeconds = 5.f;
}

class SCustomSplitter : public SSplitter
{
public:

	bool IsResizing() const
	{
		return bIsResizing;
	}
};

class SLevelSnapshotsEditorFilterRowGroup : public STableRow<UConjunctionFilter*>
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterRowGroup)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedRef<SLevelSnapshotsEditorFilters>& InOwnerPanel, UConjunctionFilter* InManagedFilter)
	{
		const bool bIsFirstAndRow = InOwnerPanel->GetEditorData()->GetUserDefinedFilters()->GetChildren().Find(InManagedFilter) == 0;
		const bool bShouldShowOrTextInFrontOfRow = !bIsFirstAndRow;
		
		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			[
				SNew(SLevelSnapshotsEditorFilterRow, InOwnerPanel, InManagedFilter, bShouldShowOrTextInFrontOfRow)
					.OnClickRemoveRow_Lambda([InOwnerPanel, InManagedFilter](auto)
					{
						InOwnerPanel->RemoveFilter(InManagedFilter);
					})
			]
		];

		STableRow<UConjunctionFilter*>::ConstructInternal(
			STableRow::FArguments()
			.ShowSelection(false),
			InOwnerTableView
		);
	}
};

SLevelSnapshotsEditorFilters::~SLevelSnapshotsEditorFilters()
{
	if (ULevelSnapshotsEditorData* Data = GetEditorData())
	{
		Data->OnUserDefinedFiltersChanged.Remove(OnUserDefinedFiltersChangedHandle);
		Data->OnEditedFiterChanged.Remove(OnEditedFilterChangedHandle);
	}
}

void SLevelSnapshotsEditorFilters::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	struct Local
	{
		static TSharedRef<SWidget> CreatePlusText(const FText& Text)
		{
			return SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .HAlign(HAlign_Center)
                    .AutoWidth()
                    .Padding(FMargin(0.f, 1.f))
                    [
	                    SNew(STextBlock)
						.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
	                    .TextStyle(FEditorStyle::Get(), "NormalText.Important")
	                    .Text(FEditorFontGlyphs::Plus)
                    ]

                    + SHorizontalBox::Slot()
                    .HAlign(HAlign_Left)
                    .AutoWidth()
                    .Padding(2.f, 1.f)
                    [
                        SNew(STextBlock)
                        .Justification(ETextJustify::Center)
                        .TextStyle(FEditorStyle::Get(), "NormalText.Important")
                        .Text(Text)
                    ];
		}
	};
	
	
	FiltersModelPtr = InFilters;
	EditorDataPtr = InFilters->GetBuilder()->EditorDataPtr;

	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ true,
		FDetailsViewArgs::HideNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ nullptr,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ NAME_None);
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Automatic;
	
	FilterDetailsView = EditModule.CreateDetailView(DetailsViewArgs);
	FilterDetailsView->RegisterInstancedCustomPropertyLayout(UNegatableFilter::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateLambda([]() { return MakeShared<FNegatableFilterDetailsCustomization>(); })
		);


	
	const TArray<UConjunctionFilter*>* AndFilters = &GetEditorData()->GetUserDefinedFilters()->GetChildren();
	ChildSlot
	[
		SAssignNew(DetailsSplitter, SCustomSplitter)
		.Style(FEditorStyle::Get(), "DetailsView.Splitter")
		.PhysicalSplitterHandleSize(1.0f)
		.HitDetectionSplitterHandleSize(5.0f)
		.Orientation(Orient_Vertical)

		// Filter config
		+ SSplitter::Slot()
		[
			SNew(SVerticalBox)

			// Refresh results & Save and load buttons
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 5.f)
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Left)
				.Padding(FMargin(0.f, 2.f))
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						// Checkbox 
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(2.f, 0.f)
						.AutoWidth()
						[
							SAssignNew(MasterFilterIndicatorButton, SMasterFilterIndicatorButton, GetEditorData()->GetUserDefinedFilters())
						]
					]
				]

				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)

					// Refresh button
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 2.f, 0.f)
					[
						SNew(SButton)
						.ButtonStyle(FEditorStyle::Get(), "FlatButton.Success")
						.ForegroundColor(FSlateColor::UseForeground())
						.OnClicked(this, &SLevelSnapshotsEditorFilters::OnClickUpdateResultsView)
						[
							SNew(SHorizontalBox)
							+SHorizontalBox::Slot()
								.AutoWidth()
								.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Justification(ETextJustify::Center)
								.TextStyle(FEditorStyle::Get(), "NormalText.Important")
								.Text(FEditorFontGlyphs::Plus)
								.Text(LOCTEXT("UpdateResults", "Refresh Results"))
							]
						]
					]

					// Save and load
					+SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
					[
						SNew(SSaveAndLoadFilters, GetEditorData())
					]
				]
			]
			
            // Favorite filters
            + SVerticalBox::Slot()
            .AutoHeight()
            .HAlign(HAlign_Fill)
            [
				SAssignNew(FavoriteList, SFavoriteFilterList, InFilters->GetBuilder()->EditorDataPtr->GetFavoriteFilters(), GetEditorData())
            ]

            // Rows 
            +SVerticalBox::Slot()
				.Padding(FMargin(0.f, 10.f, 0.f, 0.f))
            [
				SNew(SScrollBox)
                .Orientation(Orient_Vertical)

                + SScrollBox::Slot()
                [
                	SNew(SVerticalBox)
                	
                    // Rows
                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        SAssignNew(FilterRowsList, STreeView<UConjunctionFilter*>)
                        .TreeItemsSource(AndFilters)
                        .ItemHeight(24.0f)
                        .OnGenerateRow(this, &SLevelSnapshotsEditorFilters::OnGenerateRow)
                        .OnGetChildren_Lambda([](auto, auto){})
                        .ClearSelectionOnClick(false)
                    ]

                    // Add button
                    + SVerticalBox::Slot()
                    .Padding(5.f, 10.f)
                    .AutoHeight()
                    [
                        SNew(SButton)
                            .ButtonStyle(FEditorStyle::Get(), "RoundButton")
                            .ContentPadding(FMargin(4.0, 10.0))
                            .OnClicked(this, &SLevelSnapshotsEditorFilters::AddFilterClick)
                            .HAlign(HAlign_Center)
                            [
								Local::CreatePlusText(LOCTEXT("AddFilterGroup", "Filter Group"))
                            ]
                    ]
                ]
            ]
			
			
		]
		
		// Filter details panel
		+ SSplitter::Slot()
		[
			SNew(SScrollBox)
			.Orientation(Orient_Vertical)
			
			+SScrollBox::Slot()
			[
				FilterDetailsView.ToSharedRef()
			]
		]
	];

	OnUserDefinedFiltersChangedHandle = GetEditorData()->OnUserDefinedFiltersChanged.AddLambda([this]()
	{
		UDisjunctiveNormalFormFilter* UserDefinedFilters = GetEditorData()->GetUserDefinedFilters();

		const TArray<UConjunctionFilter*>* AndFilters = &UserDefinedFilters->GetChildren();
		FilterRowsList->SetTreeItemsSource(AndFilters);

		// Update master filter
		MasterFilterIndicatorButton->SetUserDefinedFilters(UserDefinedFilters);
		UserDefinedFilters->SetEditorFilterBehaviorFromChild();
		
		GetEditorData()->SetEditedFilter(TOptional<UNegatableFilter*>());
	});
	RefreshGroups();

	// Set Delegates
	OnEditedFilterChangedHandle = GetEditorData()->OnEditedFiterChanged.AddLambda([this](const TOptional<UNegatableFilter*>& ActiveFilter)
	{
		FilterDetailsView->SetObject(ActiveFilter.IsSet() ? ActiveFilter.GetValue() : nullptr);
	});
}

ULevelSnapshotsEditorData* SLevelSnapshotsEditorFilters::GetEditorData() const
{
	return EditorDataPtr.IsValid() ? EditorDataPtr.Get() : nullptr;
}

TSharedPtr<FLevelSnapshotsEditorFilters> SLevelSnapshotsEditorFilters::GetFiltersModel() const
{
	return FiltersModelPtr.Pin();
}

const TSharedPtr<IDetailsView>& SLevelSnapshotsEditorFilters::GetFilterDetailsView() const
{
	return FilterDetailsView;
}

bool SLevelSnapshotsEditorFilters::IsResizingDetailsView() const
{
	return DetailsSplitter->IsResizing();
}

void SLevelSnapshotsEditorFilters::RemoveFilter(UConjunctionFilter* FilterToRemove)
{
	UDisjunctiveNormalFormFilter* UserDefinedFilters = GetEditorData()->GetUserDefinedFilters();
	UserDefinedFilters->RemoveConjunction(FilterToRemove);
	UserDefinedFilters->SetEditorFilterBehaviorFromChild();

	RefreshGroups();
}

void SLevelSnapshotsEditorFilters::SetMasterFilterBehaviorFromChildRow(const EEditorFilterBehavior InFilterBehavior)
{
	ULevelSnapshotsEditorData* EditorData = GetEditorData();
	if (!ensure(EditorData))
	{
		return;
	}

	if (UDisjunctiveNormalFormFilter* UserDefinedFilters = EditorData->GetUserDefinedFilters())
	{
		UserDefinedFilters->SetEditorFilterBehaviorFromChild(InFilterBehavior);
	}
}

FReply SLevelSnapshotsEditorFilters::OnClickUpdateResultsView()
{
	ULevelSnapshotsEditorData* EditorData = GetEditorData();
	if (!ensure(EditorData))
	{
		return FReply::Handled();
	}
	
	if (EditorData->GetActiveSnapshot())
	{
		EditorData->OnRefreshResults.Broadcast();
	}
	else
	{
		FNotificationInfo Info(LOCTEXT("SelectSnapshotFirst", "Select a snapshot first."));
		Info.ExpireDuration = ErrorMessageDisplayTimeInSeconds;
		FSlateNotificationManager::Get().AddNotification(Info);
	}

	return FReply::Handled();
}

TSharedRef<ITableRow> SLevelSnapshotsEditorFilters::OnGenerateRow(UConjunctionFilter* InManagedFilter, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SLevelSnapshotsEditorFilterRowGroup, OwnerTable, SharedThis<SLevelSnapshotsEditorFilters>(this), InManagedFilter);
}

void SLevelSnapshotsEditorFilters::RefreshGroups()
{
	FilterRowsList->RequestTreeRefresh();
}

FReply SLevelSnapshotsEditorFilters::AddFilterClick()
{
	if (ULevelSnapshotsEditorData* EditorData = GetEditorData())
	{
		GetEditorData()->GetUserDefinedFilters()->CreateChild();
		RefreshGroups();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
