// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "IPropertyTypeCustomization.h"
#include "Templates/Casts.h"
#include "Templates/SharedPointer.h"


// Forward Declarations
class IPropertyHandle;
class FDetailWidgetRow;
class IPropertyTypeCustomizationUtils;


class FMetasoundLiteralDescriptionDetailCustomization : public IPropertyTypeCustomization
{
public:
	//~ Begin IPropertyTypeCustomization
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	//~ End IPropertyTypeCustomization
};