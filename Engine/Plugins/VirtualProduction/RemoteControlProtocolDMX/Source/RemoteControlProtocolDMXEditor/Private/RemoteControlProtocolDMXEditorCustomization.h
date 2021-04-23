// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IPropertyTypeCustomization.h"

#include "CoreMinimal.h"

/**
 * Type Customization for Protocol DMX editor
 */
class FRemoteControlProtocolDMXEditorTypeCustomization final : public IPropertyTypeCustomization
{
public:

	/** Makes a new instance of this detail layout class for a specific detail view requesting it. */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();
	
	//~ Begin IPropertyTypeCustomization Interface
	virtual void CustomizeHeader( TSharedRef<IPropertyHandle> InStructProperty, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils ) override;
	virtual void CustomizeChildren( TSharedRef<IPropertyHandle> InStructProperty, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils ) override;
	//~ End IPropertyTypeCustomization Interface

private:
	/** On fixture signal format property value change handler */
	void OnFixtureSignalFormatChange();
	
	/** On starting channel property value change handler */
	void OnStartingChannelChange();

	/**
	 * Checking if starting channel plus fixture signal format size fits into DMX_UNIVERSE_SIZE.
	 * If that is fit do nothing, otherwise remove decreasing the starting channel by overhead value.
	 */
	void CheckAndApplyStartingChannelValue();

private:
	/** Signal format handle. 1,2,3 or 4 bytes signal format */
	TSharedPtr<IPropertyHandle> FixtureSignalFormatHandle;

	/** Starting channel handle */
	TSharedPtr<IPropertyHandle> StartingChannelPropertyHandle;
};
