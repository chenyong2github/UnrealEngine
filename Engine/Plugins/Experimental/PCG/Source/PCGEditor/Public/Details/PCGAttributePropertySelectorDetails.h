// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

#include "PCGPoint.h"

struct FPCGAttributePropertySelector;
class SWidget;

class FPCGAttributePropertySelectorDetails : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance()
	{
		return MakeShareable(new FPCGAttributePropertySelectorDetails);
	}

	/** ~Begin IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {};
	/** ~End IPropertyTypeCustomization interface */

protected:
	FPCGAttributePropertySelector* GetStruct();
	const FPCGAttributePropertySelector* GetStruct() const;

	TSharedRef<SWidget> GenerateExtraMenu();

	FText GetText() const;
	void SetText(const FText& NewText, ETextCommit::Type CommitInfo);
	void SetPointProperty(EPCGPointProperties EnumValue);
	void SetAttributeName(FName NewName);

	TSharedPtr<IPropertyHandle> PropertyHandle;
};