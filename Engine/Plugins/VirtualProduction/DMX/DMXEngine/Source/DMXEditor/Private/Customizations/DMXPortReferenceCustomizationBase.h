// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Templates/SharedPointer.h"

class FDMXPort;
class SDMXPortSelector;
class UDMXLibrary;

class SBorder;
class IPropertyHandle;


/** Base class for input and output port reference details customizations. */
class FDMXPortReferenceCustomizationBase
	: public IPropertyTypeCustomization
{
protected:
	FDMXPortReferenceCustomizationBase();
	
	virtual ~FDMXPortReferenceCustomizationBase()
	{}

	// ~Begin IPropertyTypecustomization Interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	// ~End IPropertyTypecustomization Interface

	/** Called to deduce if the child is an input (or an output) */
	virtual bool IsInputPort() const = 0;

	/** Called to get the port guid handle */
	virtual const TSharedPtr<IPropertyHandle>& GetPortGuidHandle() const = 0;

private:
	/** Called when a port was selected in the port selector */
	void OnPortSelected();
	/** Updates the info row */
	void UpdateInfoRow();

	/** Generates an info widget for specified port */
	TSharedRef<SWidget> GeneratePortInfoWidget(TSharedRef<FDMXPort, ESPMode::ThreadSafe> Port) const;

	/** Helper function that finds the corresponding input port, or nullptr if no corresponding port */
	TSharedPtr<FDMXPort, ESPMode::ThreadSafe> FindPortItem() const;

	/** Applies the selected port Guid to the customized struct */
	void ApplySelectedPortGuid();

	/** Helper function that gets the Guid of the edited port */
	FGuid GetPortGuid() const;

	/** Tries to get an outer library, if the port resides in one */
	UDMXLibrary* GetOuterDMXLibrary() const;

	/** Port selector widget */
	TSharedPtr<SDMXPortSelector> PortSelector;

	/** Border to hold info content */
	TSharedPtr<SBorder> InfoContentBorder;

	/** Error text shown when the port couldn't be found, may contain the last error if there is no error - I11t is not a state. */
	FText ErrorText;
};
