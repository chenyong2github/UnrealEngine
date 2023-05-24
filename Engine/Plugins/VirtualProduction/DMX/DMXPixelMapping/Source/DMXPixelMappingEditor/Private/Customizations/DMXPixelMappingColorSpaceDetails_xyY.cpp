// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingColorSpaceDetails_xyY.h"

#include "ColorSpace/DMXPixelMappingColorSpace_xyY.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"


#define LOCTEXT_NAMESPACE "DMXPixelMappingColorSpaceDetails_xyY"

TSharedRef<IDetailCustomization> FDMXPixelMappingColorSpaceDetails_xyY::MakeInstance()
{
	return MakeShared<FDMXPixelMappingColorSpaceDetails_xyY>();
}

void FDMXPixelMappingColorSpaceDetails_xyY::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	IDetailCategoryBuilder& CategoryBuilder = DetailBuilder.EditCategory("XY");

	// Add attributes before the range
	CategoryBuilder.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingColorSpace_xyY, XAttribute)));
	CategoryBuilder.AddProperty(DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingColorSpace_xyY, YAttribute)));

	// Present the range percents property with a percent sign 
	const TSharedRef<IPropertyHandle> ColorSpaceRangePercentsPropertyHandle = DetailBuilder.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingColorSpace_xyY, ColorSpaceRangePercents));

	DetailBuilder.HideProperty(ColorSpaceRangePercentsPropertyHandle);
	
	CategoryBuilder
		.AddCustomRow(LOCTEXT("RangePercentsFilter", "Range"))
		.NameContent()
		[
			ColorSpaceRangePercentsPropertyHandle->CreatePropertyNameWidget()
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				ColorSpaceRangePercentsPropertyHandle->CreatePropertyValueWidget()
			]
			
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(2.f)
			[
				SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle("PropertyWindow.NormalFont"))
				.Text(LOCTEXT("PercentSign", "%"))
			]
		];
}

#undef LOCTEXT_NAMESPACE
