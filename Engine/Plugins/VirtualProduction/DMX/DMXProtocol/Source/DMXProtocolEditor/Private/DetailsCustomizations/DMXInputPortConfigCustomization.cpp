// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXInputPortConfigCustomization.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
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

void FDMXInputPortConfigCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FDMXPortConfigCustomizationBase::CustomizeHeader(StructPropertyHandle, HeaderRow, StructCustomizationUtils);
}

void FDMXInputPortConfigCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	FDMXPortConfigCustomizationBase::CustomizeChildren(StructPropertyHandle, ChildBuilder, StructCustomizationUtils);

	// Notify port manager about any property changes not handled in base	
	FSimpleDelegate NotifyPortConfigChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXPortConfigCustomizationBase::NotifyEditorChangedPortConfig);

	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, LocalUniverseStart))->SetOnPropertyValueChanged(NotifyPortConfigChangedDelegate);
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, NumUniverses))->SetOnPropertyValueChanged(NotifyPortConfigChangedDelegate);
	StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, ExternUniverseStart))->SetOnPropertyValueChanged(NotifyPortConfigChangedDelegate);
}

FName FDMXInputPortConfigCustomization::GetProtocolNamePropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, ProtocolName);
}

FName FDMXInputPortConfigCustomization::GetCommunicationTypePropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, CommunicationType);
}

FName FDMXInputPortConfigCustomization::GetAddressPropertyNameChecked() const
{
	return GET_MEMBER_NAME_CHECKED(FDMXInputPortConfig, Address);
}

FName FDMXInputPortConfigCustomization::GetPortGuidPropertyNameChecked() const
{
	return FDMXInputPortConfig::GetPortGuidPropertyName();
}

const TArray<EDMXCommunicationType> FDMXInputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	return GetProtocolChecked()->GetInputPortCommunicationTypes();
}

#undef LOCTEXT_NAMESPACE
