// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingDetailCustomization_Matrix.h"

#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "DMXEditorStyle.h"
#include "Toolkits/DMXPixelMappingToolkit.h"
#include "DMXPixelMapping.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Components/DMXPixelMappingRootComponent.h"
#include "DMXPixelMappingEditorUtils.h"
#include "DMXPixelMappingTypes.h"

#include "DetailWidgetRow.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "Misc/Attribute.h"
#include "Layout/Visibility.h"

#define LOCTEXT_NAMESPACE "DMXPixelMappingDetailCustomization_Matrix"

void FDMXPixelMappingDetailCustomization_Matrix::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	DetailLayout = &InDetailLayout;

	// Get editing UObject
	TArray<TWeakObjectPtr<UObject>> OuterObjects;
	DetailLayout->GetObjectsBeingCustomized(OuterObjects);

	MatrixComponents.Empty();

	for (TWeakObjectPtr<UObject> SelectedObject : OuterObjects)
	{
		MatrixComponents.Add(Cast<UDMXPixelMappingMatrixComponent>(SelectedObject));
	}

	// Get editing categories
	IDetailCategoryBuilder& OutputSettingsCategory = DetailLayout->EditCategory("Output Settings", FText::GetEmpty(), ECategoryPriority::Important);

	// Add Fixture Patch change delegates
	TSharedRef<IPropertyHandle> FixturePatchMatrixRefPropertyHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, FixturePatchMatrixRef));
	FSimpleDelegate OnFixturePatchMatrixChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXPixelMappingDetailCustomization_Matrix::OnFixturePatchMatrixChanged);
	FixturePatchMatrixRefPropertyHandle->SetOnChildPropertyValueChanged(OnFixturePatchMatrixChangedDelegate);
	FixturePatchMatrixRefPropertyHandle->SetOnPropertyValueChanged(OnFixturePatchMatrixChangedDelegate);

	// Add color mode property
	ColorModePropertyHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, ColorMode), UDMXPixelMappingMatrixComponent::StaticClass());
	OutputSettingsCategory.AddProperty(ColorModePropertyHandle);

	// Register attributes
	TSharedPtr<FDMXCellAttributeGroup> AttributeR = MakeShared<FDMXCellAttributeGroup>();
	AttributeR->Handle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeR));
	AttributeR->ExposeHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeRExpose));
	AttributeR->InvertHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeRInvert));

	TSharedPtr<FDMXCellAttributeGroup> AttributeG = MakeShared<FDMXCellAttributeGroup>();
	AttributeG->Handle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeG));
	AttributeG->ExposeHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeGExpose));
	AttributeG->InvertHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeGInvert));

	TSharedPtr<FDMXCellAttributeGroup> AttributeB = MakeShared<FDMXCellAttributeGroup>();
	AttributeB->Handle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeB));
	AttributeB->ExposeHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeBExpose));
	AttributeB->InvertHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, AttributeBInvert));

	RGBAttributes.Add(AttributeR);
	RGBAttributes.Add(AttributeG);
	RGBAttributes.Add(AttributeB);

	// Register Monochrome attribute
	TSharedPtr<FDMXCellAttributeGroup> MonochromeAttribute = MakeShared<FDMXCellAttributeGroup>();
	MonochromeAttribute->Handle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, MonochromeIntensity));
	MonochromeAttribute->ExposeHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, bMonochromeExpose));
	MonochromeAttribute->InvertHandle = DetailLayout->GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixComponent, bMonochromeInvert));
	MonochromeAttributes.Add(MonochromeAttribute);

	// Generate all RGB Expose and Invert rows
	OutputSettingsCategory.AddCustomRow(FText::GetEmpty())
		.Visibility(TAttribute<EVisibility>(this, &FDMXPixelMappingDetailCustomization_Matrix::GetRGBAttributesVisibility))
		.NameContent()
		[
			SNew(STextBlock).Text(LOCTEXT("ColorSample", "Color Sample"))
		]
		.ValueContent()
		[
			SAssignNew(ExposeAndInvertListView, SListView<TSharedPtr<FDMXCellAttributeGroup>>)
			.ListItemsSource(&RGBAttributes)
			.OnGenerateRow(this, &FDMXPixelMappingDetailCustomization_Matrix::GenerateExposeAndInvertRow)
		];

	// Update RGB attributes
	for (TSharedPtr<FDMXCellAttributeGroup>& Attribute : RGBAttributes)
	{
		DetailLayout->HideProperty(Attribute->ExposeHandle);
		DetailLayout->HideProperty(Attribute->InvertHandle);

		OutputSettingsCategory
			.AddProperty(Attribute->Handle)
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXPixelMappingDetailCustomization_Matrix::GetRGBAttributeRowVisibilty, Attribute.Get())));
	}


	// Generate all Monochrome Expose and Invert rows
	OutputSettingsCategory.AddCustomRow(FText::GetEmpty())
		.Visibility(TAttribute<EVisibility>(this, &FDMXPixelMappingDetailCustomization_Matrix::GetMonochromeAttributesVisibility))
		.NameContent()
		[
			SNew(STextBlock).Text(LOCTEXT("ColorSample", "Color Sample"))
		]
		.ValueContent()
		[
			SAssignNew(ExposeAndInvertListView, SListView<TSharedPtr<FDMXCellAttributeGroup>>)
			.ListItemsSource(&MonochromeAttributes)
			.OnGenerateRow(this, &FDMXPixelMappingDetailCustomization_Matrix::GenerateExposeAndInvertRow)
		];

	// Update Monochrome attributes
	for (TSharedPtr<FDMXCellAttributeGroup>& Attribute : MonochromeAttributes)
	{
		DetailLayout->HideProperty(Attribute->ExposeHandle);
		DetailLayout->HideProperty(Attribute->InvertHandle);

		OutputSettingsCategory
			.AddProperty(Attribute->Handle)
			.Visibility(TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXPixelMappingDetailCustomization_Matrix::GetMonochromeRowVisibilty, Attribute.Get())));
	}
	
}

bool FDMXPixelMappingDetailCustomization_Matrix::CheckComponentsDMXColorMode(const EDMXColorMode DMXColorMode) const
{
	for (TWeakObjectPtr<UDMXPixelMappingMatrixComponent> ItemComponent : MatrixComponents)
	{
		if (ItemComponent.IsValid() && ItemComponent->ColorMode == DMXColorMode)
		{
			return true;
		}
	}

	return false;
}

void FDMXPixelMappingDetailCustomization_Matrix::OnFixturePatchMatrixChanged()
{
	TSharedPtr<FDMXPixelMappingToolkit> Toolkit = ToolkitWeakPtr.Pin();
	check(Toolkit.IsValid());

	UDMXPixelMapping* PixelMapping = Toolkit->GetDMXPixelMapping();
	check(PixelMapping != nullptr);

	for (TWeakObjectPtr<UDMXPixelMappingMatrixComponent> MatrixComponentWeakPtr : MatrixComponents)
	{
		UDMXPixelMappingMatrixComponent* MatrixComponent = MatrixComponentWeakPtr.Get();
		check(MatrixComponent != nullptr);

		// Delete old one
		Toolkit->DeleteMatrixPixels(MatrixComponent);
		Toolkit->CreateMatrixPixels(MatrixComponent);
	}
}

EVisibility FDMXPixelMappingDetailCustomization_Matrix::GetRGBAttributeRowVisibilty(FDMXCellAttributeGroup* Attribute) const
{
	bool bIsVisible = false;

	// 1. Check if current attribute is sampling now
	FPropertyAccess::Result Result = Attribute->ExposeHandle->GetValue(bIsVisible);
	if (Result == FPropertyAccess::Result::MultipleValues)
	{
		bIsVisible = true;
	}

	// 2. Check if current color mode is RGB
	if (!CheckComponentsDMXColorMode(EDMXColorMode::CM_RGB))
	{
		bIsVisible = false;
	}

	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FDMXPixelMappingDetailCustomization_Matrix::GetRGBAttributesVisibility() const
{	
	return CheckComponentsDMXColorMode(EDMXColorMode::CM_RGB) ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FDMXPixelMappingDetailCustomization_Matrix::GetMonochromeRowVisibilty(FDMXCellAttributeGroup* Attribute) const
{
	bool bIsVisible = false;

	// 1. Check if current attribute is sampling now
	FPropertyAccess::Result Result = Attribute->ExposeHandle->GetValue(bIsVisible);
	if (Result == FPropertyAccess::Result::MultipleValues)
	{
		bIsVisible = true;
	}

	// 2. Check if current color mode is Monochrome
	if (!CheckComponentsDMXColorMode(EDMXColorMode::CM_Monochrome))
	{
		bIsVisible = false;
	}

	return bIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility FDMXPixelMappingDetailCustomization_Matrix::GetMonochromeAttributesVisibility() const
{
	return (GetRGBAttributesVisibility() == EVisibility::Visible) ? EVisibility::Collapsed : EVisibility::Visible;
}

TSharedRef<ITableRow> FDMXPixelMappingDetailCustomization_Matrix::GenerateExposeAndInvertRow(TSharedPtr<FDMXCellAttributeGroup> InAtribute, const TSharedRef<STableViewBase>& OwnerTable)
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
