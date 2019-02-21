// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"


class IPropertyHandle;
class FDetailWidgetRow;
class IDetailChildrenBuilder;
class SWidget;


class FGameplayTagBlueprintPropertyMappingDetails : public IPropertyTypeCustomization
{
public:

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

protected:

	void OnChangeProperty(UProperty* ItemSelected, ESelectInfo::Type SelectInfo);

	FGuid GetPropertyGuid(UProperty* Property) const;
	FString GetPropertyName(UProperty* Property) const;

	TSharedRef<SWidget> GeneratePropertyWidget(UProperty* Property);

	FText GetSelectedValueText() const;

protected:

	TSharedPtr<IPropertyHandle> NamePropertyHandle;
	TSharedPtr<IPropertyHandle> GuidPropertyHandle;

	TArray<UProperty*> PropertyOptions;

	UProperty* SelectedProperty;
};
