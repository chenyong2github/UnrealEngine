// Copyright Epic Games, Inc. All Rights Reserved.

#include "SLevelSnapshotsEditorFilters.h"

#include "LevelSnapshotsEditorData.h"
#include "LevelSnapshotsEditorFilters.h"
#include "LevelSnapshotsEditorFilterClass.h"
#include "LevelSnapshotsEditorStyle.h"
#include "LevelSnapshotFilters.h"
#include "LevelSnapshotFiltersBasic.h"
#include "SFavoriteFilterList.h"
#include "SLevelSnapshotsEditorFilterRow.h"

#include "EditorStyleSet.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"

#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Kismet2/KismetEditorUtilities.h"

#define LOCTEXT_NAMESPACE "LevelSnapshotsEditor"

SLevelSnapshotsEditorFilters::~SLevelSnapshotsEditorFilters()
{
}

#pragma optimize("", off)
void SLevelSnapshotsEditorFilters::Construct(const FArguments& InArgs, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters)
{
	FiltersModelPtr = InFilters;

	// TODO: class generation was extracted to UFavoriteFilterContainer. Dominik will refactor to use UFavoriteFilterContainer.
	// Get all classes
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* TestClass = *ClassIt;

		// Ignore deprecated and temporary trash classes.
		if (TestClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists) ||
			FKismetEditorUtilities::IsClassABlueprintSkeleton(TestClass))
		{
			continue;
		}

		TSharedPtr<FLevelSnapshotsEditorFilterClass>* FilterClass = FilterClasses.Find(TestClass);
		if (FilterClass != nullptr)
		{
			continue;
		}


		if (TestClass->IsChildOf(ULevelSnapshotBlueprintFilter::StaticClass()) && 
			TestClass->GetFName() != ULevelSnapshotBlueprintFilter::StaticClass()->GetFName())
		{
			// This is blueprint class
			UE_LOG(LogTemp, Warning, TEXT("Blueprint name %s"), *TestClass->GetFName().ToString());

			FilterClasses.Add(TestClass, MakeShared<FLevelSnapshotsEditorFilterClass>(TestClass));
		}
		else if (TestClass->IsChildOf(ULevelSnapshotFilter::StaticClass()) &&
			TestClass->GetFName() != ULevelSnapshotFilter::StaticClass()->GetFName() &&
			TestClass->GetFName() != ULevelSnapshotFiltersBasic::StaticClass()->GetFName() &&
			TestClass->GetFName() != ULevelSnapshotBlueprintFilter::StaticClass()->GetFName()
			)
		{
			if (TestClass->IsChildOf(ULevelSnapshotFiltersBasic::StaticClass()))
			{
				// This is native C++ class
				UE_LOG(LogTemp, Warning, TEXT("Basic C++ Class name %s"), *TestClass->GetFName().ToString())
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Custom C++ Class name %s"), *TestClass->GetFName().ToString())
			}

			FilterClasses.Add(TestClass, MakeShared<FLevelSnapshotsEditorFilterClass>(TestClass));
		}
	}

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
				SAssignNew(FilterRowsList, STreeView<TSharedPtr<FLevelSnapshotsEditorFilterRow>>)
				.TreeItemsSource(reinterpret_cast<TArray<TSharedPtr<FLevelSnapshotsEditorFilterRow>>*>(&FilterRowGroups))
				.ItemHeight(24.0f)
				.OnGenerateRow(this, &SLevelSnapshotsEditorFilters::OnGenerateRow)
				.OnGetChildren(this, &SLevelSnapshotsEditorFilters::OnGetGroupChildren)
				.OnSelectionChanged(this, &SLevelSnapshotsEditorFilters::OnSelectionChanged)
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
					//.ForegroundColor(FLevelSnapshotsEditorStyle::GetBrush("LevelSnapshotsEditor.BrightBorder"))
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

	// Restore all groups from Object
	{
		if (ULevelSnapshotsEditorData* EditorData = GetEditorData())
		{
			for (const TPair<FName, ULevelSnapshotEditorFilterGroup*>& FilterGroupsPair : EditorData->FilterGroups)
			{
				TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> NewGroup = MakeShared<FLevelSnapshotsEditorFilterRowGroup>(FilterGroupsPair.Value, SharedThis(this));
				FilterRowGroups.Add(NewGroup);
			}
		}
	}

	Refresh();

	// Set Delegates
	InFilters->GetOnSetActiveFilter().AddSP(this, &SLevelSnapshotsEditorFilters::OnSetActiveFilter);
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

void SLevelSnapshotsEditorFilters::RemoveFilterRow(TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> InRowGroup)
{
	if (ULevelSnapshotsEditorData* EditorData = GetEditorData())
	{
		ULevelSnapshotEditorFilterGroup* FilterGroupToRemove = InRowGroup->GetFilterGroupObject();

		// Remove the UObject
		EditorData->FilterGroups.Remove(FilterGroupToRemove->GetFName());

		int32 IndexToRemove = FilterRowGroups.Remove(InRowGroup);
		check(IndexToRemove > 0);

		FilterGroupToRemove->Modify();
		FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), ULevelSnapshotEditorFilterGroup::StaticClass(), "FilterGroupTempName");
		FilterGroupToRemove->Rename(*UniqueName.ToString());
		FilterGroupToRemove->MarkPendingKill();
		FilterGroupToRemove = nullptr;

		Refresh();
	}

}

#pragma optimize("", on)


TSharedRef<ITableRow> SLevelSnapshotsEditorFilters::OnGenerateRow(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InFilterRow->GetType() == FLevelSnapshotsEditorFilterRow::Group)
	{
		return SNew(SLevelSnapshotsEditorFilterRowGroup, OwnerTable, InFilterRow->AsGroup(), SharedThis<SLevelSnapshotsEditorFilters>(this));
	}
	
	return SNew(STableRow<TSharedPtr<SWidget>>, OwnerTable)
		[
			SNew(STextBlock).Text(LOCTEXT("ErrorRowGeneration", "ErrorRowGeneration"))
		];
}

void SLevelSnapshotsEditorFilters::OnGetGroupChildren(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, TArray<TSharedPtr<FLevelSnapshotsEditorFilterRow>>& OutRows)
{
	if (InFilterRow.IsValid())
	{
		InFilterRow->GetNodeChildren(OutRows);
	}
}

void SLevelSnapshotsEditorFilters::OnSelectionChanged(TSharedPtr<FLevelSnapshotsEditorFilterRow> InFilterRow, ESelectInfo::Type SelectInfo)
{
}

void SLevelSnapshotsEditorFilters::Refresh()
{
	RefreshGroups();
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
		const FName FilterGroupName = MakeUniqueObjectName(EditorData, ULevelSnapshotEditorFilterGroup::StaticClass(), TEXT("FilterGroup"));

		ULevelSnapshotEditorFilterGroup* FilterGroup = EditorData->AddOrFindGroup(FilterGroupName);

		TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> NewGroup = MakeShared<FLevelSnapshotsEditorFilterRowGroup>(FilterGroup, SharedThis(this));
		FilterRowGroups.Add(NewGroup);

		RefreshGroups();
	}

	return FReply::Handled();
}

void SLevelSnapshotsEditorFilters::OnSetActiveFilter(ULevelSnapshotFilter* InFilter)
{
	FilterInputDetailsView->SetObject(InFilter);
}

void SLevelSnapshotsEditorFilterRowGroup::Tick(const FGeometry&, const double, const float)
{
}

void SLevelSnapshotsEditorFilterRowGroup::Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTableView, const TSharedPtr<FLevelSnapshotsEditorFilterRowGroup>& FieldGroup, const TSharedPtr<SLevelSnapshotsEditorFilters>& OwnerPanel)
{
	ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(3.0f, 2.0f))
			[
				SNew(SLevelSnapshotsEditorFilterRow, OwnerPanel.ToSharedRef(), FieldGroup.ToSharedRef())
			]
		];

	STableRow<TSharedPtr<FLevelSnapshotsEditorFilterRowGroup>>::ConstructInternal(
		STableRow::FArguments()
		.ShowSelection(false),
		InOwnerTableView
	);
}

TSharedPtr<FLevelSnapshotsEditorFilterRowGroup> FLevelSnapshotsEditorFilterRowGroup::AsGroup()
{
	return SharedThis(this);
}

ULevelSnapshotEditorFilterGroup* FLevelSnapshotsEditorFilterRowGroup::GetFilterGroupObject() const
{
	return FilterGroupPtr.Get();
}

#undef LOCTEXT_NAMESPACE
