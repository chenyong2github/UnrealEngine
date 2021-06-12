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

FName FDMXOutputPortConfigCustomization::GetProtocolNamePropertyNameChecked() const
{
	return FDMXOutputPortConfig::GetProtocolNamePropertyNameChecked();
}

FName FDMXOutputPortConfigCustomization::GetCommunicationTypePropertyNameChecked() const
{
	return FDMXOutputPortConfig::GetCommunicationTypePropertyNameChecked();
}

FName FDMXOutputPortConfigCustomization::GetDeviceAddressPropertyNameChecked() const
{
	return FDMXOutputPortConfig::GetDeviceAddressPropertyNameChecked();
}

FName FDMXOutputPortConfigCustomization::GetPortGuidPropertyNameChecked() const
{
	return FDMXOutputPortConfig::GetPortGuidPropertyNameChecked();
}

const TArray<EDMXCommunicationType> FDMXOutputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	return GetProtocolChecked()->GetOutputPortCommunicationTypes();
}

#undef LOCTEXT_NAMESPACE
