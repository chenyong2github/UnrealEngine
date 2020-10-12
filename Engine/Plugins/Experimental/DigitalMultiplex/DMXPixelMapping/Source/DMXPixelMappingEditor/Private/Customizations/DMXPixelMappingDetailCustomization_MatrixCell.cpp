// Copyright Epic Games, Inc. All Rights Reserved.

#include "Customizations/DMXPixelMappingDetailCustomization_MatrixCell.h"

#include "DMXPixelMappingTypes.h"
#include "Components/DMXPixelMappingMatrixCellComponent.h"
#include "Toolkits/DMXPixelMappingToolkit.h"

#include "DetailLayoutBuilder.h"

void FDMXPixelMappingDetailCustomization_MatrixCell::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
	// Hide absolute postition property handles
	TSharedPtr<IPropertyHandle> PositionXPropertyHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixCellComponent, PositionX), UDMXPixelMappingOutputComponent::StaticClass());
	InDetailLayout.HideProperty(PositionXPropertyHandle);
	TSharedPtr<IPropertyHandle> PositionYPropertyHandle = InDetailLayout.GetProperty(GET_MEMBER_NAME_CHECKED(UDMXPixelMappingMatrixCellComponent, PositionY), UDMXPixelMappingOutputComponent::StaticClass());
	InDetailLayout.HideProperty(PositionYPropertyHandle);
}
