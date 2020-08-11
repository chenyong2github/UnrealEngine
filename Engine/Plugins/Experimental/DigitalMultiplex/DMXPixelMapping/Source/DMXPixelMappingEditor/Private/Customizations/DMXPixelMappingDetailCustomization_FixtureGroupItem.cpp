// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingDetailCustomization_FixtureGroupItem.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "DMXPixelMappingTypes.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "Layout/Visibility.h"

#define LOCTEXT_NAMESPACE "FixtureGroupItem"

void FDMXPixelMappingDetailCustomization_FixtureGroupItem::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	DetailLayout = &InDetailLayout;

	// Get editing object
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayout->GetObjectsBeingCustomized(Objects);
	if (Objects.Num() == 1)
	{
		FixtureGroupItemComponent = Cast<UDMXPixelMappingFixtureGroupItemComponent>(Objects[0]);


		// Get editing categories
		IDetailCategoryBuilder& OutputSettingsCategory = DetailLayout->EditCategory("Output Settings", FText::GetEmpty(), ECategoryPriority::Important);

		// Add Function and ColorMode properties at the beginning
		TSharedPtr<IPropertyHandle> ExtraAttributesHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, ExtraAttributes), UDMXPixelMappingFixtureGroupItemComponent::StaticClass());
		TSharedPtr<IPropertyHandle> ColorModePropertyHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, ColorMode), UDMXPixelMappingFixtureGroupItemComponent::StaticClass());
		OutputSettingsCategory.AddProperty(ExtraAttributesHandle);
		OutputSettingsCategory.AddProperty(ColorModePropertyHandle);

		// Register attributes
		TSharedPtr<FFunctionAttribure> AttributeR = MakeShared<FFunctionAttribure>();
		AttributeR->Handle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeR));
		AttributeR->ExposeHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeRExpose));
		AttributeR->InvertHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeRInvert));

		TSharedPtr<FFunctionAttribure> AttributeG = MakeShared<FFunctionAttribure>();
		AttributeG->Handle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeG));
		AttributeG->ExposeHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeGExpose));
		AttributeG->InvertHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeGInvert));

		TSharedPtr<FFunctionAttribure> AttributeB = MakeShared<FFunctionAttribure>();
		AttributeB->Handle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeB));
		AttributeB->ExposeHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeBExpose));
		AttributeB->InvertHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, AttributeBInvert));

		RGBAttributes.Add(AttributeR);
		RGBAttributes.Add(AttributeG);
		RGBAttributes.Add(AttributeB);

		// Register Monochrome attribute
		TSharedPtr<FFunctionAttribure> MonochromeAttribute = MakeShared<FFunctionAttribure>();
		MonochromeAttribute->Handle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, MonochromeIntensity));
		MonochromeAttribute->ExposeHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, MonochromeExpose));
		MonochromeAttribute->InvertHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingFixtureGroupItemComponent, MonochromeInvert));
		MonochromeAttributes.Add(MonochromeAttribute);

		// Generate all RGB Expose and Invert rows
		OutputSettingsCategory.AddCustomRow(FText::GetEmpty())
			.Visibility(TAttribute<EVisibility>(this, &FDMXPixelMappingDetailCustomization_FixtureGroupItem::GetRGBAttributesVisibility))
			.NameContent()
			[
				SNew(STextBlock).Text(LOCTEXT("ColorSample", "Color Sample"))
			]
			.ValueContent()
			[
				SAssignNew(ExposeAndInvertListView, SListView<TSharedPtr<FFunctionAttribure>>)
				.ListItemsSource(&RGBAttributes)
				.OnGenerateRow(this, &FDMXPixelMappingDetailCustomization_FixtureGroupItem::GenerateExposeAndInvertRow)
			];

		// Update RGB attributes
		for (TSharedPtr<FFunctionAttribure>& Attribute : RGBAttributes)
		{
			DetailLayout->HideProperty(Attribute->ExposeHandle);
			DetailLayout->HideProperty(Attribute->InvertHandle);

			OutputSettingsCategory
				.AddProperty(Attribute->Handle)
				.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroupItem::GetRGBAttributeRowVisibilty, Attribute.Get())));
		}

		// Generate all Monochrome Expose and Invert rows
		OutputSettingsCategory.AddCustomRow(FText::GetEmpty())
			.Visibility(TAttribute<EVisibility>(this, &FDMXPixelMappingDetailCustomization_FixtureGroupItem::GetMonochromeAttributesVisibility))
			.NameContent()
			[
				SNew(STextBlock).Text(LOCTEXT("ColorSample", "Color Sample"))
			]
			.ValueContent()
			[
				SAssignNew(ExposeAndInvertListView, SListView<TSharedPtr<FFunctionAttribure>>)
				.ListItemsSource(&MonochromeAttributes)
				.OnGenerateRow(this, &FDMXPixelMappingDetailCustomization_FixtureGroupItem::GenerateExposeAndInvertRow)
			];

		// Update Monochrome attributes
		for (TSharedPtr<FFunctionAttribure>& Attribute : MonochromeAttributes)
		{
			DetailLayout->HideProperty(Attribute->ExposeHandle);
			DetailLayout->HideProperty(Attribute->InvertHandle);

			OutputSettingsCategory
				.AddProperty(Attribute->Handle)
				.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXPixelMappingDetailCustomization_FixtureGroupItem::GetMonochromeRowVisibilty, Attribute.Get())));
		}
	}
}

EVisibility FDMXPixelMappingDetailCustomization_FixtureGroupItem::GetRGBAttributeRowVisibilty(FFunctionAttribure* Attribute) const
{
	bool bIsVisible = false;

	// 1. Check if current attribute is sampling now
	Attribute->ExposeHandle->GetValue(bIsVisible);

	// 2. Check if current color mode is RGB
	if (FixtureGroupItemComponent->ColorMode != EDMXColorMode::CM_RGB)
	{
		bIsVisible = false;
	}

	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FDMXPixelMappingDetailCustomization_FixtureGroupItem::GetRGBAttributesVisibility() const
{
	bool bIsVisible = false;

	if (FixtureGroupItemComponent->ColorMode == EDMXColorMode::CM_RGB)
	{
		bIsVisible = true;
	}
	
	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FDMXPixelMappingDetailCustomization_FixtureGroupItem::GetMonochromeRowVisibilty(FFunctionAttribure* Attribute) const
{
	bool bIsVisible = false;

	// 1. Check if current attribute is sampling now
	Attribute->ExposeHandle->GetValue(bIsVisible);

	// 2. Check if current color mode is Monochrome
	if (FixtureGroupItemComponent->ColorMode != EDMXColorMode::CM_Monochrome)
	{
		bIsVisible = false;
	}


	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FDMXPixelMappingDetailCustomization_FixtureGroupItem::GetMonochromeAttributesVisibility() const
{
	return (GetRGBAttributesVisibility() == EVisibility::Visible) ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<ITableRow> FDMXPixelMappingDetailCustomization_FixtureGroupItem::GenerateExposeAndInvertRow(TSharedPtr<FFunctionAttribure> InAtribute, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (!InAtribute.IsValid())
	{
		return SNew(STableRow<TSharedPtr<FString>>, OwnerTable);
	}

	return SNew(STableRow<TSharedPtr<FString>>, OwnerTable)
		.Padding(2.0f)
		.Style(FEditorStyle::Get(), "UMGEditor.PaletteItem")
		.ShowSelection(false)
		[
			SNew(SBox)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.HAlign(HAlign_Left)
				[
					InAtribute->ExposeHandle->CreatePropertyNameWidget()
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.HAlign(HAlign_Left)
				[
					InAtribute->ExposeHandle->CreatePropertyValueWidget()
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.HAlign(HAlign_Left)
				[
					InAtribute->InvertHandle->CreatePropertyNameWidget()
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1)
				.AutoWidth()
				.Padding(2.0f, 0.0f)
				.HAlign(HAlign_Left)
				[
					InAtribute->InvertHandle->CreatePropertyValueWidget()
				]
			]
		];
}

#undef LOCTEXT_NAMESPACE
