// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "UnrealClient.h"
#include "IPropertyTypeCustomization.h"
#include "PerQualityLevelProperties.h"

class FDetailWidgetDecl;

/**
* Implements a details panel customization for the FPerQualityLevel structures.
*/
template<typename OverrideType>
class FPerQualityLevelPropertyCustomization : public IPropertyTypeCustomization
{
public:
	FPerQualityLevelPropertyCustomization()
	{}

	// IPropertyTypeCustomization interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override {}

	/**
	* Creates a new instance.
	*
	* @return A new customization for FPerQualityLevel structs.
	*/
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

protected:
	TSharedRef<SWidget> GetWidget(FName InQualityLevelName, TSharedRef<IPropertyHandle> StructPropertyHandle) const;
	TArray<FName> GetOverrideNames(TSharedRef<IPropertyHandle> StructPropertyHandle) const;
	bool AddOverride(FName InQualityLevelName, TSharedRef<IPropertyHandle> StructPropertyHandle);
	bool RemoveOverride(FName InQualityLevelName, TSharedRef<IPropertyHandle> StructPropertyHandle);
	float CalcDesiredWidth(TSharedRef<IPropertyHandle> StructPropertyHandle);

private:
	/** Cached utils used for resetting customization when layout changes */
	TWeakPtr<IPropertyUtilities> PropertyUtilities;
};

