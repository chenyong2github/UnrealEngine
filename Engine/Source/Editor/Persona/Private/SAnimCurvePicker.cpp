// Copyright Epic Games, Inc. All Rights Reserved.

#include "SAnimCurvePicker.h"
#include "AssetRegistryModule.h"
#include "IEditableSkeleton.h"
#include "Animation/AnimationAsset.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "SAnimCurvePicker"

void SAnimCurvePicker::Construct(const FArguments& InArgs, const TSharedRef<IEditableSkeleton>& InEditableSkeleton)
{
	OnCurveNamePicked = InArgs._OnCurveNamePicked;
	EditableSkeleton = InEditableSkeleton;

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchBoxHint", "Search Available Curves"))
			.OnTextChanged(this, &SAnimCurvePicker::HandleFilterTextChanged)
		]
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(NameListView, SListView<TSharedPtr<FString>>)
			.SelectionMode(ESelectionMode::Single)
			.ItemHeight(20.0f)
			.ListItemsSource(&CurveNames)
			.OnSelectionChanged(this, &SAnimCurvePicker::HandleSelectionChanged)
			.OnGenerateRow(this, &SAnimCurvePicker::HandleGenerateRow)
		]
	];

	RefreshListItems();
}

void SAnimCurvePicker::HandleSelectionChanged(TSharedPtr<FString> InItem, ESelectInfo::Type InSelectionType)
{
	OnCurveNamePicked.ExecuteIfBound(FName(**InItem.Get()));
}

TSharedRef<ITableRow> SAnimCurvePicker::HandleGenerateRow(TSharedPtr<FString> InItem, const TSharedRef<STableViewBase>& InOwnerTable)
{
	return 
		SNew(STableRow<TSharedPtr<FString>>, InOwnerTable)
		[
			SNew(SBox)
			.MinDesiredHeight(20.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(*InItem.Get()))
				.HighlightText_Lambda([this]() { return FText::FromString(FilterText); })
			]
		];
}

void SAnimCurvePicker::RefreshListItems()
{
	CurveNames.Empty();
	UniqueCurveNames.Empty();

	// We use the asset registry to query all assets with the supplied skeleton, and accumulate their curve names
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	FARFilter Filter;
	Filter.bRecursiveClasses = true;
	Filter.ClassNames.Add(UAnimationAsset::StaticClass()->GetFName());
	Filter.TagsAndValues.Add(TEXT("Skeleton"), FAssetData(&EditableSkeleton.Pin()->GetSkeleton()).GetExportTextName());

	TArray<FAssetData> FoundAssetData;
	AssetRegistryModule.Get().GetAssets(Filter, FoundAssetData);

	// now build set of unique curve names
	for (FAssetData& AssetData : FoundAssetData)
	{
		const FString TagValue = AssetData.GetTagValueRef<FString>(USkeleton::CurveNameTag);
		if (!TagValue.IsEmpty())
		{
			// parse notifies
			TArray<FString> CurveValues;
			if (TagValue.ParseIntoArray(CurveValues, *USkeleton::CurveTagDelimiter, true) > 0)
			{
				for (const FString& CurveValue : CurveValues)
				{
					UniqueCurveNames.Add(CurveValue);
				}
			}
		}
	}

	FilterAvailableCurves();
}

void SAnimCurvePicker::FilterAvailableCurves()
{
	CurveNames.Empty();

	for (const FString& UniqueCurveName : UniqueCurveNames)
	{
		if (FilterText.IsEmpty() || UniqueCurveName.Contains(FilterText))
		{
			CurveNames.Add(MakeShared<FString>(UniqueCurveName));
		}
	}

	NameListView->RequestListRefresh();
}

void SAnimCurvePicker::HandleFilterTextChanged(const FText& InFilterText)
{
	FilterText = InFilterText.ToString();

	FilterAvailableCurves();
}

#undef LOCTEXT_NAMESPACE