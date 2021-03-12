// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXOutputPortConfigCustomization.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
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

void FDMXOutputPortConfigCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FDMXPortConfigCustomizationBase::CustomizeHeader(StructPropertyHandle, HeaderRow, StructCustomizationUtils);
}

void FDMXOutputPortConfigCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FDMXPortConfigCustomizationBase::CustomizeChildren(StructPropertyHandle, ChildBuilder, StructCustomizationUtils);

	// Handle property changes of the communication type, to set bLookbackToEngine according to if the protocol IsCausingLoopback 
	LoopbackToEngineHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, bLoopbackToEngine));

	FSimpleDelegate OnCommunicationTypeChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXOutputPortConfigCustomization::OnCommunicationTypeChanged);
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, CommunicationType))->SetOnPropertyValueChanged(OnCommunicationTypeChangedDelegate);

	// Notify port manager about any unhandled property changes
	FSimpleDelegate NotifyPortConfigChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXPortConfigCustomizationBase::NotifyEditorChangedPortConfig);
	
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, bLoopbackToEngine))->SetOnPropertyValueChanged(NotifyPortConfigChangedDelegate);
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, LocalUniverseStart))->SetOnPropertyValueChanged(NotifyPortConfigChangedDelegate);
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, NumUniverses))->SetOnPropertyValueChanged(NotifyPortConfigChangedDelegate);
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, ExternUniverseStart))->SetOnPropertyValueChanged(NotifyPortConfigChangedDelegate);
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, Priority))->SetOnPropertyValueChanged(NotifyPortConfigChangedDelegate);
}

FName FDMXOutputPortConfigCustomization::GetProtocolNamePropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, ProtocolName);
}

FName FDMXOutputPortConfigCustomization::GetCommunicationTypePropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, CommunicationType);
}

FName FDMXOutputPortConfigCustomization::GetAddressPropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, Address);
}

FName FDMXOutputPortConfigCustomization::GetPortGuidPropertyNameChecked() const
{
	return FDMXOutputPortConfig::GetPortGuidPropertyName();
}

const TArray<EDMXCommunicationType> FDMXOutputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	return GetProtocolChecked()->GetOutputPortCommunicationTypes();
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
