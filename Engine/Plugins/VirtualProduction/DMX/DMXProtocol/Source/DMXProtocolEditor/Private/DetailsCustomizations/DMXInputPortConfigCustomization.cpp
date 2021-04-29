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

void FDMXInputPortConfigCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FDMXPortConfigCustomizationBase::CustomizeChildren(InStructPropertyHandle, ChildBuilder, StructCustomizationUtils);

	StructPropertyHandle = InStructPropertyHandle;

	// Update corresponding port on property changes
	FSimpleDelegate UpdatePortDelegate = FSimpleDelegate::CreateSP(this, &FDMXInputPortConfigCustomization::UpdatePort);
	StructPropertyHandle->SetOnChildPropertyValueChanged(UpdatePortDelegate);

	// Since base may make changes to data we want to update the port already
	UpdatePort();
}

FName FDMXInputPortConfigCustomization::GetProtocolNamePropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, ProtocolName);
}

FName FDMXInputPortConfigCustomization::GetCommunicationTypePropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, CommunicationType);
}

FName FDMXInputPortConfigCustomization::GetDeviceAddressPropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, DeviceAddress);
}

FName FDMXInputPortConfigCustomization::GetPortGuidPropertyNameChecked() const
{
	return FDMXInputPortConfig::GetPortGuidPropertyName();
}

const TArray<EDMXCommunicationType> FDMXInputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	return GetProtocolChecked()->GetInputPortCommunicationTypes();
}

void FDMXInputPortConfigCustomization::UpdatePort()
{
	check(StructPropertyHandle.IsValid());

	TArray<void*> RawData;
	StructPropertyHandle->AccessRawData(RawData);

	// Multiediting is not supported, may fire if this is used in a blueprint way that would support it
	if (ensureAlwaysMsgf(RawData.Num() == 1, TEXT("Using port config in ways that would enable multiediting is not supported.")))
	{
		FDMXInputPortConfig* PortConfigPtr = reinterpret_cast<FDMXInputPortConfig*>(RawData[0]);
		if (ensureAlways(PortConfigPtr))
		{
			FDMXInputPortSharedPtr InputPort = FDMXPortManager::Get().FindInputPortByGuid(PortConfigPtr->GetPortGuid());
			if (InputPort.IsValid())
			{
				InputPort->UpdateFromConfig(*PortConfigPtr);
			}
			else
			{
				PortConfigPtr->PortName = FString(TEXT("Invalid Port Config"));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
