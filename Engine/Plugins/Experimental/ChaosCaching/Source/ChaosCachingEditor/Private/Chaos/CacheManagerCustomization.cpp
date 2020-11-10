// Copyright Epic Games, Inc. All Rights Reserved.

#include "Chaos/CacheManagerCustomization.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Chaos/CacheManagerActor.h"
#include "IDetailChildrenBuilder.h"

#define LOCTEXT_NAMESPACE "CacheManagerDetails"

FReply OnClickSetAllButton(TArray<AChaosCacheManager*> Managers, ECacheMode NewMode)
{
	for(AChaosCacheManager* Manager : Managers)
	{
		if(Manager)
		{
			Manager->SetAllMode(NewMode);
		}
	}

	return FReply::Handled();
}

FReply OnClickResetTransforms(TArray<AChaosCacheManager*> Managers)
{
	for(AChaosCacheManager* Manager : Managers)
	{
		if(Manager)
		{
			Manager->ResetAllComponentTransforms();
		}
	}

	return FReply::Handled();
}

TSharedRef<IDetailCustomization> FCacheManagerDetails::MakeInstance()
{
	return MakeShareable(new FCacheManagerDetails);
}

void FCacheManagerDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> Selected = DetailBuilder.GetSelectedObjects();
	TArray<AChaosCacheManager*> CacheManagersInSelection;

	for(TWeakObjectPtr<UObject> Ptr : Selected)
	{
		if(AChaosCacheManager* Manager = Cast<AChaosCacheManager>(Ptr.Get()))
		{
			CacheManagersInSelection.Add(Manager);
		}
	}

	if(CacheManagersInSelection.Num() == 0)
	{
		return;
	}

	IDetailCategoryBuilder& CachingCategory = DetailBuilder.EditCategory("Caching");
	FDetailWidgetRow&       SetAllRow       = CachingCategory.AddCustomRow(FText::GetEmpty());

	SetAllRow.NameContent()
	[
		SNew(STextBlock)
		.Font(DetailBuilder.GetDetailFont())
		.Text(LOCTEXT("SetAllLabel", "Set All"))
	];

	SetAllRow.ValueContent()
	.MinDesiredWidth(300.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.Padding(0.0f, 0.0f, 3.0f, 0.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Static(&OnClickSetAllButton, CacheManagersInSelection, ECacheMode::Record)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SetAllRecord", "Record"))
			]
		]
		+ SHorizontalBox::Slot()
		.Padding(0.0f, 0.0f, 3.0f, 0.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Static(&OnClickSetAllButton, CacheManagersInSelection, ECacheMode::Play)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SetAllPlay", "Play"))
			]
		]
		+ SHorizontalBox::Slot()
		.Padding(0.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Static(&OnClickSetAllButton, CacheManagersInSelection, ECacheMode::None)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("SetAllNone", "None"))
			]
		]
	];

	FDetailWidgetRow& ResetPositionsRow = CachingCategory.AddCustomRow(FText::GetEmpty());

	ResetPositionsRow.ValueContent()
	.MinDesiredWidth(300.0f)
	[
		SNew(SButton)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked_Static(&OnClickResetTransforms, CacheManagersInSelection)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ResetPositions", "Reset All Component Transforms"))
		]
	];
}

void FCacheManagerDetails::GenerateCacheArrayElementWidget(TSharedRef<IPropertyHandle> PropertyHandle, int32 ArrayIndex, IDetailChildrenBuilder& ChildrenBuilder, IDetailLayoutBuilder* DetailLayout)
{
}

TSharedRef<IPropertyTypeCustomization> FObservedComponentDetails::MakeInstance()
{
	return MakeShareable(new FObservedComponentDetails);
}

void FObservedComponentDetails::CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	HeaderRow
		.NameContent()
		[
			PropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		.VAlign(VAlign_Center)
		.HAlign(HAlign_Left)
		[
			PropertyHandle->CreatePropertyValueWidget(/*bDisplayDefaultPropertyButtons =*/false)
		];
}

FReply OnClickResetSingleTransform(AChaosCacheManager* InManager, int32 InIndex)
{
	if(InManager)
	{
		InManager->ResetSingleTransform(InIndex);
	}

	return FReply::Handled();
}

FReply OnClickSelectComponent(AChaosCacheManager* InManager, int32 InIndex)
{
	if(InManager)
	{
		InManager->SelectComponent(InIndex);
	}

	return FReply::Handled();
}

void FObservedComponentDetails::CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	uint32 NumChildren = 0;
	PropertyHandle->GetNumChildren(NumChildren);

	for(uint32 ChildNum = 0; ChildNum < NumChildren; ++ChildNum)
	{
		ChildBuilder.AddProperty(PropertyHandle->GetChildHandle(ChildNum).ToSharedRef());
	}

	TArray<TWeakObjectPtr<UObject>> SelectedObjects = ChildBuilder.GetParentCategory().GetParentLayout().GetSelectedObjects();
	if(SelectedObjects.Num() == 1)
	{
		if(AChaosCacheManager* SelectedManager = Cast<AChaosCacheManager>(SelectedObjects[0].Get()))
		{
			const int32 ArrayIndex = PropertyHandle->GetIndexInArray();

			ChildBuilder.AddCustomRow(FText::GetEmpty())
				.ValueContent()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnClicked_Static(&OnClickResetSingleTransform, SelectedManager, ArrayIndex)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ItemResetTransform", "Reset Transform"))
						]
					]
					+ SVerticalBox::Slot()
					.Padding(0.0f, 0.0f, 0.0f, 3.0f)
					[
						SNew(SButton)
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.OnClicked_Static(&OnClickSelectComponent, SelectedManager, ArrayIndex)
						[
							SNew(STextBlock)
							.Text(LOCTEXT("ItemSelect", "Select Component"))
						]
					]
				];
		}
	}
}

#undef LOCTEXT_NAMESPACE
