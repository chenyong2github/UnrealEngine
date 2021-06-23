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
	// ~Begin DMXPortConfigCustomizationBase Interface
	virtual FName GetProtocolNamePropertyNameChecked() const override;
	virtual FName GetCommunicationTypePropertyNameChecked() const override;
	virtual FName GetDeviceAddressPropertyNameChecked() const override;
	virtual FName GetPortGuidPropertyNameChecked() const override;
	virtual const TArray<EDMXCommunicationType> GetSupportedCommunicationTypes() const override;
	// ~End DMXPortConfigCustomizationBase Interface
};
