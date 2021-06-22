// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingDetailCustomization_MatrixCell.h"

#include "DMXPixelMappingTypes.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "Toolkits/DMXPixelMappingToolkit.h"

#include "DetailLayoutBuilder.h"

void FDMXPixelMappingDetailCustomization_MatrixCell::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	// Hide absolute postition property handles
	TSharedPtr<IPropertyHandle> PositionXPropertyHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionX));
	TSharedPtr<IPropertyHandle> PositionYPropertyHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, PositionY));
	InDetailLayout.HideProperty(PositionXPropertyHandle);

	// Hide size property handles
	TSharedPtr<IPropertyHandle> SizeXPropertyHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeX));
	TSharedPtr<IPropertyHandle> SizeYPropertyHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingOutputComponent, SizeY));
	InDetailLayout.HideProperty(SizeXPropertyHandle);
}
