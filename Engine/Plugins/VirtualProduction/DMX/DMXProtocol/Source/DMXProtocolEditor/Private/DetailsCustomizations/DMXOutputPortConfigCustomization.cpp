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

	// Update corresponding port on property changes
	FSimpleDelegate UpdatePortDelegate = FSimpleDelegate::CreateSP(this, &FDMXOutputPortConfigCustomization::UpdatePort);
	StructPropertyHandle->SetOnChildPropertyValueChanged(UpdatePortDelegate);

	// Since base may make changes to data we want to update the port already
	UpdatePort();
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

void FDMXOutputPortConfigCustomization::UpdatePort()
{
	check(StructPropertyHandle.IsValid());

	TArray<void*> RawDataArray;
	StructPropertyHandle->AccessRawData(RawDataArray);

	for (void* RawData : RawDataArray)
	{
		FDMXOutputPortConfig* PortConfigPtr = reinterpret_cast<FDMXOutputPortConfig*>(RawData);
		if (ensureAlways(PortConfigPtr))
		{
			FDMXOutputPortSharedPtr OutputPort = FDMXPortManager::Get().FindOutputPortByGuid(PortConfigPtr->GetPortGuid());
			if (OutputPort.IsValid())
			{
				OutputPort->UpdateFromConfig(*PortConfigPtr);
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
