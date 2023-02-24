// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkinWeightDetailCustomization.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "SkinWeightsPaintTool.h"
#include "SSkinWeightProfileImportOptions.h"

#define LOCTEXT_NAMESPACE "SkinWeightToolSettingsEditor"

void FSkinWeightDetailCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> DetailObjects;
	DetailBuilder.GetObjectsBeingCustomized(DetailObjects);

	// should be impossible to get multiple settings objects for a single tool
	ensure(DetailObjects.Num()==1);
	SkinToolSettings = Cast<USkinWeightsPaintToolProperties>(DetailObjects[0]);
	
	// custom display of falloff mode as segmented toggle buttons
	IDetailCategoryBuilder& BrushCategory = DetailBuilder.EditCategory("Brush", FText::GetEmpty(), ECategoryPriority::Important);

	// add segmented control toggle for brush behavior modes ("Add", "Replace", etc..)
	BrushCategory.AddCustomRow(LOCTEXT("BrushModeCategory", "Brush Mode"), false)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BrushModeLabel", "Brush Mode"))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.ToolTipText(LOCTEXT("BrushModeTooltip", "Determines how the weights are affected by the brush."))
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(SBox)
				.Padding(2.0f)
				.HAlign(HAlign_Center)
				[
					SNew(SSegmentedControl<EBrushBehaviorMode>)
					.Value_Lambda([this]()
					{
						return SkinToolSettings->BrushMode;
					})
					.OnValueChanged_Lambda([this](EBrushBehaviorMode Mode)
					{
						SkinToolSettings->BrushMode = Mode;
					})
					+SSegmentedControl<EBrushBehaviorMode>::Slot(EBrushBehaviorMode::Add)
					.Text(LOCTEXT("AddMode", "Add"))
					+ SSegmentedControl<EBrushBehaviorMode>::Slot(EBrushBehaviorMode::Replace)
					.Text(LOCTEXT("ReplaceMode", "Replace"))
					+ SSegmentedControl<EBrushBehaviorMode>::Slot(EBrushBehaviorMode::Multiply)
					.Text(LOCTEXT("MultiplyMode", "Multiply"))
					+ SSegmentedControl<EBrushBehaviorMode>::Slot(EBrushBehaviorMode::Relax)
					.Text(LOCTEXT("RelaxMode", "Relax"))
				]
			]
		];
	const TSharedRef<IPropertyHandle> BrushPropHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, BrushMode));
	DetailBuilder.HideProperty(BrushPropHandle);

	// add segmented control toggle for brush falloff modes ("Surface" or "Volume")
	BrushCategory.AddCustomRow(LOCTEXT("BrushFalloffCategory", "Brush Falloff"), false)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("BrushFalloffLabel", "Falloff Mode"))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.ToolTipText(LOCTEXT("BrushFalloffTooltip", "Falloff brush influence along the surface of the mesh, or using straight-line distance (volume)."))
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(SBox)
				.Padding(2.0f)
				.HAlign(HAlign_Center)
				[
					SNew(SSegmentedControl<EWeightBrushFalloffMode>)
					.Value_Lambda([this]()
					{
						return SkinToolSettings->FalloffMode;
					})
					.OnValueChanged_Lambda([this](EWeightBrushFalloffMode Mode)
					{
						SkinToolSettings->FalloffMode = Mode;
						SkinToolSettings->bColorModeChanged = true;
					})
					+SSegmentedControl<EWeightBrushFalloffMode>::Slot(EWeightBrushFalloffMode::Surface)
					.Text(LOCTEXT("SurfaceMode", "Surface"))
					+ SSegmentedControl<EWeightBrushFalloffMode>::Slot(EWeightBrushFalloffMode::Volume)
					.Text(LOCTEXT("VolumeMode", "Volume"))
				]
			]
		];
	const TSharedRef<IPropertyHandle> FalloffPropHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, FalloffMode));
	DetailBuilder.HideProperty(FalloffPropHandle);

	// custom display of falloff mode as segmented toggle buttons
	IDetailCategoryBuilder& WeightColorsCategory = DetailBuilder.EditCategory("WeightColors", FText::GetEmpty(), ECategoryPriority::Important);
	WeightColorsCategory.InitiallyCollapsed(true);

	// add segmented control toggle for brush behavior modes ("Add", "Replace", etc..)
	WeightColorsCategory.AddCustomRow(LOCTEXT("ColorModeCategory", "Color Mode"), false)
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("ColorModeLabel", "Color Mode"))
			.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
			.ToolTipText(LOCTEXT("ColorModeTooltip", "Determines how the weight colors are displayed."))
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(SBox)
				.Padding(2.0f)
				.HAlign(HAlign_Center)
				[
					SNew(SSegmentedControl<EWeightColorMode>)
					.Value_Lambda([this]()
					{
						return SkinToolSettings->ColorMode;
					})
					.OnValueChanged_Lambda([this](EWeightColorMode Mode)
					{
						SkinToolSettings->ColorMode = Mode;
						SkinToolSettings->bColorModeChanged = true;
					})
					+ SSegmentedControl<EWeightColorMode>::Slot(EWeightColorMode::Greyscale)
					.Text(LOCTEXT("GreyscaleMode", "Greyscale"))
					+SSegmentedControl<EWeightColorMode>::Slot(EWeightColorMode::ColorRamp)
					.Text(LOCTEXT("RampMode", "Color Ramp"))
				]
			]
		];
	const TSharedRef<IPropertyHandle> ColorModePropHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(USkinWeightsPaintToolProperties, ColorMode));
	DetailBuilder.HideProperty(ColorModePropHandle);
}

#undef LOCTEXT_NAMESPACE