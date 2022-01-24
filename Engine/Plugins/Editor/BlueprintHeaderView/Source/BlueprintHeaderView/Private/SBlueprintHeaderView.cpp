// Copyright Epic Games, Inc. All Rights Reserved.

#include "SBlueprintHeaderView.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Input/SComboButton.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "Engine/Blueprint.h"

#define LOCTEXT_NAMESPACE "SBlueprintHeaderView"

void SBlueprintHeaderView::Construct(const FArguments& InArgs)
{
	const float PaddingAmount = 8.0f;
	SelectedBlueprint = nullptr;

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(PaddingAmount))
		[
			SNew(SHorizontalBox)
			+SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ClassPickerLabel", "Displaying Blueprint:"))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(SSpacer)
				.Size(FVector2D(PaddingAmount))
			]
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SNew(SBox)
				.WidthOverride(400.0f)
				[
					SAssignNew(ClassPickerComboButton, SComboButton)
					.OnGetMenuContent(this, &SBlueprintHeaderView::GetClassPickerMenuContent)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(this, &SBlueprintHeaderView::GetClassPickerText)
					]
				]
			]
		]
		+SVerticalBox::Slot()
		[
			SNullWidget::NullWidget // TODO: UE-138705 replace this with a list view and start adding items to it
		]
	];
}

FText SBlueprintHeaderView::GetClassPickerText() const
{
	if (const UBlueprint* Blueprint = SelectedBlueprint.Get())
	{
		return FText::FromName(Blueprint->GetFName());
	}

	return LOCTEXT("ClassPickerPickClass", "Select Blueprint Class");
}

TSharedRef<SWidget> SBlueprintHeaderView::GetClassPickerMenuContent()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>("ContentBrowser");

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.SelectionMode = ESelectionMode::Single;
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SBlueprintHeaderView::OnAssetSelected);
	AssetPickerConfig.Filter.ClassNames.Add(UBlueprint::StaticClass()->GetFName());
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	TSharedRef<SWidget> AssetPickerWidget = ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig);

	return SNew(SBox)
		.HeightOverride(500.f)
		[
			AssetPickerWidget
		];
}

void SBlueprintHeaderView::OnAssetSelected(const FAssetData& SelectedAsset)
{
	ClassPickerComboButton->SetIsOpen(false);

	SelectedBlueprint = Cast<UBlueprint>(SelectedAsset.GetAsset());
}

#undef LOCTEXT_NAMESPACE
