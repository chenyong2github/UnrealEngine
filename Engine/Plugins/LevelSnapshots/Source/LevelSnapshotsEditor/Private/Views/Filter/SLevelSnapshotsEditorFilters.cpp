// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelSnapshotsEditorFilters.h"


#include "DisjunctiveNormalFormFilter.h"
#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsEditorFilters.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotFilters.h"
#include "SFavoriteFilterList.h"
#include "SLevelSnapshotsEditorFilterRow.h"

#include "EditorStyleSet.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STreeView.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

class SLevelSnapshotsEditorFilterRowGroup : public STableRow<UConjunctionFilter*>
{
public:
	SLATE_BEGIN_ARGS(SLevelSnapshotsEditorFilterRowGroup)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedRef<SLevelSnapshotsEditorFilters>& InOwnerPanel, UConjunctionFilter* InManagedFilter)
	{
		ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			[
				SNew(SLevelSnapshotsEditorFilterRow, InOwnerPanel, InManagedFilter)
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

void SLevelSnapshotsEditorFilters::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	FiltersModelPtr = InFilters;

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

	FilterInputDetailsView = EditModule.CreateDetailView(DetailsViewArgs);

	const TArray<UConjunctionFilter*>* AndFilters = &GetEditorData()->GetUserDefinedFilters()->GetChildren();
	ChildSlot
	[
		SNew(SVerticalBox)

		// Favorite filters
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(FavoriteList, SFavoriteFilterList, InFilters->GetBuilder()->EditorDataPtr->GetFavoriteFilters())
		]

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
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Center)
					.AutoWidth()
					.Padding(FMargin(0, 1))
					[
						SNew(SImage)
						.Image(FEditorStyle::GetBrush("Plus"))
					]
				]
		]

		// Filter details panel
		+ SVerticalBox::Slot()
		.Padding(0.f, 10.f)
		.AutoHeight()
		[
			FilterInputDetailsView.ToSharedRef()
		]
	];

	RefreshGroups();

	// Set Delegates
	InFilters->GetOnSetActiveFilter().AddLambda([this](ULevelSnapshotFilter* ActiveFilter)
	{
		FilterInputDetailsView->SetObject(ActiveFilter);
	});
}

ULevelSnapshotsEditorData* SLevelSnapshotsEditorFilters::GetEditorData() const
{
	TSharedPtr<FLevelSnapshotsEditorFilters> FiltersModel = FiltersModelPtr.Pin();
	check(FiltersModel.IsValid());

	const TSharedRef<FLevelSnapshotsEditorViewBuilder>& Builder = FiltersModel->GetBuilder();
	
	return Builder->EditorDataPtr.Get();
}

TSharedRef<FLevelSnapshotsEditorFilters> SLevelSnapshotsEditorFilters::GetFiltersModel() const
{
	return FiltersModelPtr.Pin().ToSharedRef();
}

void SLevelSnapshotsEditorFilters::RemoveFilter(UConjunctionFilter* FilterToRemove)
{
	GetEditorData()->GetUserDefinedFilters()->RemoveConjunction(FilterToRemove);
	RefreshGroups();
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
	// Create Row Object
	if (ULevelSnapshotsEditorData* EditorData = GetEditorData())
	{
		GetEditorData()->GetUserDefinedFilters()->CreateChild();
		RefreshGroups();
	}

	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
