// Copyright Epic Games, Inc. All Rights Reserved.

#include "RemoteControlProtocolMappingDetails.h"

#define LOCTEXT_NAMESPACE "RemoteControlProtocolWidgets"

void FRemoteControlProtocolMappingDMXTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);
}

void FRemoteControlProtocolMappingDMXTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);
}

void FRemoteControlProtocolMappingMIDITypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);
}

void FRemoteControlProtocolMappingMIDITypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);
}

void FRemoteControlProtocolMappingOSCTypeCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> InPropertyHandle, FDetailWidgetRow& InHeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeHeader(InPropertyHandle, InHeaderRow, CustomizationUtils);
}

void FRemoteControlProtocolMappingOSCTypeCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> InPropertyHandle, IDetailChildrenBuilder& InChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils)
{
	Super::CustomizeChildren(InPropertyHandle, InChildBuilder, CustomizationUtils);
}

#undef LOCTEXT_NAMESPACE