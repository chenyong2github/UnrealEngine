// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "UObject/WeakObjectPtr.h"

class IPropertyHandle;
class IPropertyUtilities;
class UDMXEntityFixturePatch;


/** Property customization for Attributes in certain PixelMapping views that can provide a Fixture Patch */
class FDMXPixelMappingAttributeNamesDetails
	: public IPropertyTypeCustomization
{
public:
	/** Creates an instance of this Detail Customization*/
	static TSharedRef<IPropertyTypeCustomization> MakeInstance(const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> FixturePatches);

	/** Constructor */
	FDMXPixelMappingAttributeNamesDetails(const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> InFixturePatches)
		: FixturePatches(InFixturePatches)
	{};

private:
	//~Begin IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& Builder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~End IPropertyTypeCustomization interface

	/** Returns true if the given Attribute Handle has multiple values */
	bool HasMultipleAttributeValues(TSharedRef<IPropertyHandle> AttributeHandle) const;

	/** Gets the value of the given Attribute Handle. Should not be called if HasMultipleAttributeValues returns true */
	FName GetAttributeValue(TSharedRef<IPropertyHandle> AttributeHandle) const;

	/** Sets the value of the given Attribute Handle */
	void SetAttributeValue(TSharedRef<IPropertyHandle> AttributeHandle, const FName& NewValue);

	/** Gets an array of Attribute names from the given array of Components */
	TArray<FName> GetFixtureGroupItemsAttributes(const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& Patches);

private:
	/** The Fixture Patches from which the attributes are generated */
	TArray<TWeakObjectPtr<UDMXEntityFixturePatch>> FixturePatches;

	/** The attributes available to this Fixture Group Item */
	TArray<FName> FixtureGroupItemComponentsAttributes;
};
