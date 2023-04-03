// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "InstancedStructDetails.h"
#include "Templates/SharedPointer.h"

class UPCGGraphInstance;

class FPCGOverrideInstancedPropertyBagDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** ~Begin IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& InCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildrenBuilder, IPropertyTypeCustomizationUtils& InCustomizationUtils) override;
	/** ~End IPropertyTypeCustomization interface */
private:
	TSharedPtr<IPropertyHandle> PropertyHandle;
};

class FPCGOverrideInstancedPropertyBagDataDetails : public FInstancedStructDataDetails
{
public:
	FPCGOverrideInstancedPropertyBagDataDetails(TSharedPtr<IPropertyHandle> InStructProperty, const TSharedPtr<IPropertyUtilities>& InPropUtils);

	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow) override;

private:
	UPCGGraphInstance* Owner = nullptr;
	TSharedPtr<IPropertyHandle> PropertiesIDsOverriddenHandle;
};