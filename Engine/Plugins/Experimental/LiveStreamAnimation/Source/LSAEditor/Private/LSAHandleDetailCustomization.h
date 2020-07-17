// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SWidget.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"

class FDetailWidgetRow;
class IDetailChildrenBuilder;

class FLSAHandleDetailCustomization : public IPropertyTypeCustomization
{
public:

	using ThisClass = FLSAHandleDetailCustomization;
	
	//~ Begin IPropertyTypeCustomization Interface.
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	//~ End IPropertyTypeCustomization Interface.

	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

private:

	FName GetSelectedHandle(bool& bMultipleValues) const;
	void OnHandleSelectionChanged(FName NewHandle);

	TSharedPtr<IPropertyHandle> HandleProperty;
};