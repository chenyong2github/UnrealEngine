// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Widgets/SWidget.h"

class SOpenColorIOColorSpacePicker;

/**
 * Implements a details view customization for the FOpenColorIOColorConversionSettings
 */
class FOpenColorIOColorConversionSettingsCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FOpenColorIOColorConversionSettingsCustomization);
	}

	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<class IPropertyHandle> InPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& PropertyTypeCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<class IPropertyHandle> InPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& PropertyTypeCustomizationUtils) override;

private:
	/** Callback to reset the configuration in the transform source/destination pickers. */
	void OnConfigurationReset();

	/** Pointer to the ColorConversion struct member SourceColorSpace property handle. */
	TSharedPtr<IPropertyHandle> SourceColorSpaceProperty;
	
	/** Pointer to the ColorConversion struct member DestinationColorSpace property handle. */
	TSharedPtr<IPropertyHandle> DestinationColorSpaceProperty;

	/** Pointer to the ColorConversion struct member DestinationColorSpace property handle. */
	TSharedPtr<IPropertyHandle> DestinationDisplayViewProperty;

	/** ColorSpace pickers reference to update them when config asset is changed */
	TSharedPtr<SOpenColorIOColorSpacePicker> TransformSourcePicker = nullptr;
	TSharedPtr<SOpenColorIOColorSpacePicker> TransformDestinationPicker = nullptr;

	/** Raw pointer to the conversion settings struct. */
	struct FOpenColorIOColorConversionSettings* ColorSpaceConversion = nullptr;
};
