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

void FDMXOutputPortConfigCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InStructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FDMXPortConfigCustomizationBase::CustomizeChildren(InStructPropertyHandle, ChildBuilder, StructCustomizationUtils);

	StructPropertyHandle = InStructPropertyHandle;

	// Handle property changes of the communication type, to set bLookbackToEngine according to if the protocol IsCausingLoopback 
	LoopbackToEngineHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, bLoopbackToEngine));

	FSimpleDelegate OnCommunicationTypeChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXOutputPortConfigCustomization::OnCommunicationTypeChanged);
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, CommunicationType))->SetOnPropertyValueChanged(OnCommunicationTypeChangedDelegate);

	// Update corresponding port on property changes
	FSimpleDelegate UpdatePortDelegate = FSimpleDelegate::CreateSP(this, &FDMXOutputPortConfigCustomization::UpdatePort);
	StructPropertyHandle->SetOnChildPropertyValueChanged(UpdatePortDelegate);

	// Since base may make changes to data we want to update the port already
	UpdatePort();
}

FName FDMXOutputPortConfigCustomization::GetProtocolNamePropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, ProtocolName);
}

FName FDMXOutputPortConfigCustomization::GetCommunicationTypePropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, CommunicationType);
}

FName FDMXOutputPortConfigCustomization::GetDeviceAddressPropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, DeviceAddress);
}

FName FDMXOutputPortConfigCustomization::GetPortGuidPropertyNameChecked() const
{
	return FDMXOutputPortConfig::GetPortGuidPropertyName();
}

const TArray<EDMXCommunicationType> FDMXOutputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	return GetProtocolChecked()->GetOutputPortCommunicationTypes();
}

void FDMXOutputPortConfigCustomization::UpdatePort()
{
	check(StructPropertyHandle.IsValid());

	TArray<void*> RawData;
	StructPropertyHandle->AccessRawData(RawData);

	// Multiediting is not supported, may fire if this is used in a blueprint way that would support it
	if (ensureMsgf(RawData.Num() == 1, TEXT("Using port config in ways that would enable multiediting is not supported.")))
	{
		const FDMXOutputPortConfig* PortConfigPtr = reinterpret_cast<FDMXOutputPortConfig*>(RawData[0]);
		if (ensure(PortConfigPtr))
		{
			FDMXOutputPortSharedPtr OutputPort = FDMXPortManager::Get().FindOutputPortByGuid(PortConfigPtr->GetPortGuid());
			if (ensure(OutputPort.IsValid()))
			{
				OutputPort->UpdateFromConfig(*PortConfigPtr);
			}
		}
	}
}

void FDMXOutputPortConfigCustomization::OnCommunicationTypeChanged()
{
	check(LoopbackToEngineHandle.IsValid());

	EDMXCommunicationType CommunicationType = GetCommunicationType();
	IDMXProtocolPtr Protocol = GetProtocolChecked();
	bool bCommunicationTypeCausesExternalLoopback = Protocol->IsCausingLoopback(CommunicationType);

	LoopbackToEngineHandle->SetValue(!bCommunicationTypeCausesExternalLoopback);
}

#undef LOCTEXT_NAMESPACE
