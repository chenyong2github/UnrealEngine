// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "OptimusDataType.h"

#include "IPropertyTypeCustomization.h"

class FOptimusDataTypeRefCustomization : 
	public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	// IPropertyTypeCustomization overrides
	void CustomizeHeader( 
		TSharedRef<IPropertyHandle> InPropertyHandle, 
		FDetailWidgetRow& InHeaderRow, 
		IPropertyTypeCustomizationUtils& InCustomizationUtils 
		) override;

	void CustomizeChildren(
		TSharedRef<IPropertyHandle> InPropertyHandle, 
		IDetailChildrenBuilder& InChildBuilder, 
		IPropertyTypeCustomizationUtils& InCustomizationUtils
		) override;;

private:	
	FOptimusDataTypeHandle GetCurrentDataType() const;
	void OnDataTypeChanged(FOptimusDataTypeHandle InDataType);

	FText GetDeclarationText() const;

	TSharedPtr<IPropertyHandle> TypeNameProperty;
	TAttribute<FOptimusDataTypeHandle> CurrentDataType;

};
