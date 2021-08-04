// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXInputPortConfigCustomization.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXInputPortConfig.h"
#include "IO/DMXPortManager.h"
#include "Widgets/SDMXCommunicationTypeComboBox.h"
#include "Widgets/SDMXIPAddressEditWidget.h"
#include "Widgets/SDMXProtocolNameComboBox.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h" 
#include "IPAddress.h" 
#include "SocketSubsystem.h"
#include "Misc/Guid.h" 
#include "Widgets/Text/STextBlock.h" 


#define LOCTEXT_NAMESPACE "DMXPortConfigCustomization"

TSharedRef<IPropertyTypeCustomization> FDMXInputPortConfigCustomization::MakeInstance()
{
	return MakeShared<FDMXInputPortConfigCustomization>();
}

FName FDMXInputPortConfigCustomization::GetProtocolNamePropertyName() const
{
	return FDMXInputPortConfig::GetProtocolNamePropertyNameChecked();
}

FName FDMXInputPortConfigCustomization::GetCommunicationTypePropertyName() const
{
	return FDMXInputPortConfig::GetCommunicationTypePropertyNameChecked();
}

FName FDMXInputPortConfigCustomization::GetDeviceAddressPropertyName() const
{
	return FDMXInputPortConfig::GetDeviceAddressPropertyNameChecked();
}

FName FDMXInputPortConfigCustomization::GetDestinationAddressPropertyName() const
{
	return NAME_None;
}

FName FDMXInputPortConfigCustomization::GetPriorityStrategyPropertyName() const
{
	return FDMXInputPortConfig::GetPriorityStrategyPropertyNameChecked();
}

FName FDMXInputPortConfigCustomization::GetPriorityPropertyName() const
{
	return FDMXInputPortConfig::GetPriorityPropertyNameChecked();
}

FName FDMXInputPortConfigCustomization::GetPortGuidPropertyName() const
{
	return FDMXInputPortConfig::GetPortGuidPropertyNameChecked();
}

const TArray<EDMXCommunicationType> FDMXInputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	if (IDMXProtocolPtr Protocol = GetProtocol())
	{
		return Protocol->GetInputPortCommunicationTypes();
	}

	return TArray<EDMXCommunicationType>();
}

#undef LOCTEXT_NAMESPACE
