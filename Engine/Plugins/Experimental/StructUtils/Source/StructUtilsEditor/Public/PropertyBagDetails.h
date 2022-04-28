// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "InstancedStructDetails.h"
#include "EdGraphSchema_K2.h"

class IPropertyHandle;
class IDetailPropertyRow;
class SInlineEditableTextBlock;
class SWidget;

/**
 * Type customization for FInstancedPropertyBag.
 */
class STRUCTUTILSEDITOR_API FPropertyBagDetails : public IPropertyTypeCustomization
{
public:
	/** Makes a new instance of this detail layout class for a specific detail view requesting it */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	/** Creates add property widget. */
	static TSharedPtr<SWidget> MakeAddPropertyWidget(TSharedPtr<IPropertyHandle> StructProperty, class IPropertyUtilities* PropUtils);
	
protected:
	/** IPropertyTypeCustomization interface */
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& StructBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;

private:

	FReply OnAddProperty() const;
	
	/** Handle to the struct property being edited */
	TSharedPtr<IPropertyHandle> StructProperty;
	TSharedPtr<IPropertyHandle> ValueProperty;

	bool bFixedLayout = false; 
	
	class IPropertyUtilities* PropUtils = nullptr;
};

/** 
 * Node builder for FInstancedPropertyBag children.
 *  - ValueProperty is FInstancedStruct of the FInstancedPropertyBag
 *  - StructProperty is FInstancedPropertyBag
 * Can be used in a implementation of a IPropertyTypeCustomization CustomizeChildren() to display editable FInstancedPropertyBag contents.
 * Use FPropertyBagDetails::MakeAddPropertyWidget() to create the add property widget.
 * OnChildRowAdded() is called right after each property is added, which allows the property row to be customizable.
 */
class STRUCTUTILSEDITOR_API FPropertyBagInstanceDataDetails : public FInstancedStructDataDetails
{
public:
	FPropertyBagInstanceDataDetails(TSharedPtr<IPropertyHandle> InValueProperty, TSharedPtr<IPropertyHandle> InStructProperty, IPropertyUtilities* InPropUtils, const bool bInFixedLayout);

	virtual void OnChildRowAdded(IDetailPropertyRow& ChildRow) override;

protected:
	TSharedRef<SWidget> OnPropertyNameContent(TSharedPtr<IPropertyHandle> ChildPropertyHandle, TSharedPtr<SInlineEditableTextBlock> InlineWidget) const;

	TSharedPtr<IPropertyHandle> StructProperty;
	IPropertyUtilities* PropUtils = nullptr;
	bool bFixedLayout = false;
};
