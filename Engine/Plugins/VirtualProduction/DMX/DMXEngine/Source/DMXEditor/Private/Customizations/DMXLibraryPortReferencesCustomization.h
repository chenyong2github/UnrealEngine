// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"

class FDetailWidgetRow;
class IPropertyHandle;
class SBorder;


/** Property type customization for the Library Port References struct*/
class FDMXLibraryPortReferencesCustomization
	: public IPropertyTypeCustomization
{
public:
	/** Creates an instance of the property type customization */
	static TSharedRef<IPropertyTypeCustomization> MakeInstance() { return MakeShared<FDMXLibraryPortReferencesCustomization>(); }

	// ~Begin IPropertyTypeCustomization Interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	// ~End IPropertyTypeCustomization Interface

protected:
	/** Generates an port row given the PortReferenceHandles */
	void RefreshPortReferenceWidgets();

	/** The PortReferences property handle (the outermost StructPropertyHandle that is being customized) */
	TSharedPtr<IPropertyHandle> LibraryPortReferencesHandle;

	/** Input port reference content border */
	TSharedPtr<SBorder> InputPortReferenceContentBorder;

	/** Output port reference content border */
	TSharedPtr<SBorder> OutputPortReferenceContentBorder;
};
