// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXOutputPortConfigCustomization.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXOutputPortConfig.h"
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

TSharedRef<IPropertyTypeCustomization> FDMXOutputPortConfigCustomization::MakeInstance()
{
	return MakeShared<FDMXOutputPortConfigCustomization>();
}

FName FDMXOutputPortConfigCustomization::GetProtocolNamePropertyName() const
{
	return FDMXOutputPortConfig::GetProtocolNamePropertyNameChecked();
}

FName FDMXOutputPortConfigCustomization::GetCommunicationTypePropertyName() const
{
	return FDMXOutputPortConfig::GetCommunicationTypePropertyNameChecked();
}

FName FDMXOutputPortConfigCustomization::GetDeviceAddressPropertyName() const
{
	return FDMXOutputPortConfig::GetDeviceAddressPropertyNameChecked();
}

FName FDMXOutputPortConfigCustomization::GetDestinationAddressPropertyName() const
{
	return FDMXOutputPortConfig::GetDestinationAddressPropertyNameChecked();
}

FName FDMXOutputPortConfigCustomization::GetPriorityStrategyPropertyName() const
{
	// Doesn't exist for output ports
	return NAME_None;
}

FName FDMXOutputPortConfigCustomization::GetPriorityPropertyName() const
{
	return FDMXOutputPortConfig::GetPriorityPropertyNameChecked();
}

FName FDMXOutputPortConfigCustomization::GetPortGuidPropertyName() const
{
	return FDMXOutputPortConfig::GetPortGuidPropertyNameChecked();
}

const TArray<EDMXCommunicationType> FDMXOutputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	if (IDMXProtocolPtr Protocol = GetProtocol())
	{
		return Protocol->GetOutputPortCommunicationTypes();
	}

	return TArray<EDMXCommunicationType>();
}

#undef LOCTEXT_NAMESPACE
