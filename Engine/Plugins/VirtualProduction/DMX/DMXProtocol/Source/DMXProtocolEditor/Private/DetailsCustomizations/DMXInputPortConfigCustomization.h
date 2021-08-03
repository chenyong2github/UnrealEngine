// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPortConfigCustomizationBase.h"

enum class EDMXCommunicationType : uint8;

class IDetailPropertyRow;


/** Details customization for input and output port configs. */
class FDMXInputPortConfigCustomization
	: public FDMXPortConfigCustomizationBase
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	// ~Begin DMXPortConfigCustomizationBase Interface
	virtual FName GetProtocolNamePropertyName() const override;
	virtual FName GetCommunicationTypePropertyName() const override;
	virtual FName GetDeviceAddressPropertyName() const override;
	virtual FName GetDestinationAddressPropertyName() const override;
	virtual FName GetPriorityStrategyPropertyName() const override;
	virtual FName GetPriorityPropertyName() const override;
	virtual FName GetPortGuidPropertyName() const override;
	virtual const TArray<EDMXCommunicationType> GetSupportedCommunicationTypes() const override;
	// ~End DMXPortConfigCustomizationBase Interface
};
