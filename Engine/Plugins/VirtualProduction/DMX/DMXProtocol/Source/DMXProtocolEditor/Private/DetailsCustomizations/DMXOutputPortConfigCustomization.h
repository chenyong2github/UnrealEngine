// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPortConfigCustomizationBase.h"

enum class EDMXCommunicationType : uint8;

class IDetailPropertyRow;


/** Details customization for input and output port configs. */
class FDMXOutputPortConfigCustomization
	: public FDMXPortConfigCustomizationBase
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

protected:
	// ~Begin IPropertyTypecustomization Interface
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	// ~End IPropertyTypecustomization Interface

	// ~Begin DMXPortConfigCustomizationBase Interface
	virtual FName GetProtocolNamePropertyNameChecked() const override;
	virtual FName GetCommunicationTypePropertyNameChecked() const override;
	virtual FName GetDeviceAddressPropertyNameChecked() const override;
	virtual FName GetPortGuidPropertyNameChecked() const override;
	virtual const TArray<EDMXCommunicationType> GetSupportedCommunicationTypes() const override;
	// ~End DMXPortConfigCustomizationBase Interface

	/** Updates the port from the output port config customized here */
	void UpdatePort();

private:
	/** Handle for the customized FDMXOutputPortConfig struct */
	TSharedPtr<IPropertyHandle> StructPropertyHandle;
};
