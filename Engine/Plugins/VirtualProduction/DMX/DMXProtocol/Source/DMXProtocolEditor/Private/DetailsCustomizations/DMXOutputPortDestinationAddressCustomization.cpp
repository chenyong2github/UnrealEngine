// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXOutputPortDestinationAddressCustomization.h"

#include "IO/DMXOutputPortConfig.h"
#include "Widgets/SDMXIPAddressEditWidget.h"

#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h" 
#include "DetailWidgetRow.h"


#define LOCTEXT_NAMESPACE "DMXOutputPortDestinationAddressCustomization"

TSharedRef<IPropertyTypeCustomization> FDMXOutputPortDestinationAddressCustomization::MakeInstance()
{
	return MakeShared<FDMXOutputPortDestinationAddressCustomization>();
}

void FDMXOutputPortDestinationAddressCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	// Nothing in the header
}

void FDMXOutputPortDestinationAddressCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	DestinationAddressStringHandle = StructPropertyHandle->GetChildHandle(GET_MEMBER_NAME_CHECKED(FDMXOutputPortDestinationAddress, DestinationAddressString));

	ChildBuilder.AddCustomRow(LOCTEXT("InvalidPortConfigSearchString", "Invalid"))
		.ValueContent()
		[
			SAssignNew(IPAddressEditWidget, SDMXIPAddressEditWidget)
			.InitialValue(GetIPAddress())
			.bShowLocalNICComboBox(false)
			.OnIPAddressSelected(this, &FDMXOutputPortDestinationAddressCustomization::OnIPAddressSelected)
		];
}

FString FDMXOutputPortDestinationAddressCustomization::GetIPAddress() const
{
	FString IPAddress;
	if (DestinationAddressStringHandle->GetValue(IPAddress) == FPropertyAccess::Success)
	{
		return IPAddress;
	}

	return TEXT("");
}

void FDMXOutputPortDestinationAddressCustomization::SetIPAddress(const FString& NewIPAddress)
{
	DestinationAddressStringHandle->NotifyPreChange();
	DestinationAddressStringHandle->SetValue(NewIPAddress);
	DestinationAddressStringHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
}

void FDMXOutputPortDestinationAddressCustomization::OnIPAddressSelected()
{
	TArray<UObject*> OuterObjects;
	DestinationAddressStringHandle->GetOuterObjects(OuterObjects);
	
	// Forward the change to the outer object so it gets the property change. Required due to get the change from the nested property all the way to UDMXProtocolSettings.
	for (UObject* Object : OuterObjects)
	{
		Object->PreEditChange(FDMXOutputPortDestinationAddress::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FDMXOutputPortDestinationAddress, DestinationAddressString)));
	}

	const FString IPAddress = IPAddressEditWidget->GetSelectedIPAddress();
	SetIPAddress(IPAddress);

	for (UObject* Object : OuterObjects)
	{
		Object->PostEditChange();
	}
}

#undef LOCTEXT_NAMESPACE
