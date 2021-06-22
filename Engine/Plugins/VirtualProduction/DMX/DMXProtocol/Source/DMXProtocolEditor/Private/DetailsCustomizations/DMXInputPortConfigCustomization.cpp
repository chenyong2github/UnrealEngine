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

FName FDMXInputPortConfigCustomization::GetProtocolNamePropertyNameChecked() const
{
	return FDMXInputPortConfig::GetProtocolNamePropertyNameChecked();
}

FName FDMXInputPortConfigCustomization::GetCommunicationTypePropertyNameChecked() const
{
	return FDMXInputPortConfig::GetCommunicationTypePropertyNameChecked();
}

FName FDMXInputPortConfigCustomization::GetDeviceAddressPropertyNameChecked() const
{
	return FDMXInputPortConfig::GetDeviceAddressPropertyNameChecked();
}

FName FDMXInputPortConfigCustomization::GetPortGuidPropertyNameChecked() const
{
	return FDMXInputPortConfig::GetPortGuidPropertyNameChecked();
}

const TArray<EDMXCommunicationType> FDMXInputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	return GetProtocolChecked()->GetInputPortCommunicationTypes();
}

#undef LOCTEXT_NAMESPACE
