// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPortConfigCustomizationBase.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXPortManager.h"
#include "Widgets/SDMXCommunicationTypeComboBox.h"
#include "Widgets/SDMXIPAddressEditWidget.h"
#include "Widgets/SDMXProtocolNameComboBox.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h" 
#include "IPAddress.h" 
#include "ScopedTransaction.h"
#include "SocketSubsystem.h"
#include "Misc/Guid.h" 
#include "Widgets/Text/STextBlock.h" 


#define LOCTEXT_NAMESPACE "DMXPortConfigCustomization"

void FDMXPortConfigCustomizationBase::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	const bool bDisplayResetToDefault = false;
	const FText DisplayNameOverride = FText::GetEmpty();
	const FText DisplayToolTipOverride = FText::GetEmpty();

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget(DisplayNameOverride, DisplayToolTipOverride, bDisplayResetToDefault)
	];
}

void FDMXPortConfigCustomizationBase::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	// Retrieve structure's child properties
	uint32 NumChildren;
	StructPropertyHandle->GetNumChildren(NumChildren);
	TMap<FName, TSharedPtr< IPropertyHandle>> PropertyHandles;
	for (uint32 ChildIndex = 0; ChildIndex < NumChildren; ++ChildIndex)
	{
		TSharedRef<IPropertyHandle> ChildHandle = StructPropertyHandle->GetChildHandle(ChildIndex).ToSharedRef();
		const FName PropertyName = ChildHandle->GetProperty()->GetFName();

		PropertyHandles.Add(PropertyName, ChildHandle);
	}

	// Cache customized properties
	ProtocolNameHandle = PropertyHandles.FindChecked(GetProtocolNamePropertyNameChecked());
	CommunicationTypeHandle = PropertyHandles.FindChecked(GetCommunicationTypePropertyNameChecked());
	AddressHandle = PropertyHandles.FindChecked(GetAddressPropertyNameChecked());
	PortGuidHandle = PropertyHandles.FindChecked(GetPortGuidPropertyNameChecked());

	for (auto Iter(PropertyHandles.CreateConstIterator()); Iter; ++Iter)
	{
		// Don't add the PortGuid property
		if (Iter.Value() == PortGuidHandle)
		{
			continue;
		}
		
		// Add the property
		IDetailPropertyRow& PropertyRow = ChildBuilder.AddProperty(Iter.Value().ToSharedRef());

		if (Iter.Value() == ProtocolNameHandle)
		{
			GenerateProtocolNameRow(PropertyRow);
		}
		else if (Iter.Value() == CommunicationTypeHandle)
		{
			GenerateCommunicationTypeRow(PropertyRow);
		}
		else if (Iter.Value() == AddressHandle)
		{
			GenerateIPAddressRow(PropertyRow);
		}
	}
}

IDMXProtocolPtr FDMXPortConfigCustomizationBase::GetProtocolChecked() const
{
	check(ProtocolNameHandle.IsValid());

	FName ProtocolName;
	ProtocolNameHandle->GetValue(ProtocolName);

	IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName);
	check(Protocol.IsValid());

	return Protocol;
}

FGuid FDMXPortConfigCustomizationBase::GetPortGuidChecked() const
{
	check(PortGuidHandle.IsValid());

	TArray<void*> RawData;
	PortGuidHandle->AccessRawData(RawData);

	if (ensureMsgf(RawData.Num() == 1, TEXT("The port configs reside in an array, multi-editing is unexpected")))
	{
		FGuid* PortGuidPtr = reinterpret_cast<FGuid*>(RawData[0]);
		if (ensureMsgf(PortGuidPtr, TEXT("Expected to be valid")))
		{
			check(PortGuidPtr->IsValid());
			return *PortGuidPtr;
		}
	}

	checkNoEntry();

	return FGuid();
}

EDMXCommunicationType FDMXPortConfigCustomizationBase::GetCommunicationType() const
{
	check(CommunicationTypeHandle.IsValid());

	uint8 CommunicationType;
	ensure(CommunicationTypeHandle->GetValue(CommunicationType) == FPropertyAccess::Success);

	return static_cast<EDMXCommunicationType>(CommunicationType);
}

FString FDMXPortConfigCustomizationBase::GetIPAddress() const
{
	check(AddressHandle.IsValid());

	FString IPAddress;
	ensure(AddressHandle->GetValue(IPAddress) == FPropertyAccess::Success);

	return IPAddress;
}

void FDMXPortConfigCustomizationBase::NotifyEditorChangedPortConfig()
{
	FDMXPortManager::Get().NotifyPortConfigChanged(GetPortGuidChecked());
}

void FDMXPortConfigCustomizationBase::GenerateProtocolNameRow(IDetailPropertyRow& PropertyRow)
{
	// Customizate the Protocol Name property to draw a combo box with all protocol names

	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;
	FDetailWidgetRow Row;
	PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);
			
	FName InitialSelection = GetProtocolChecked()->GetProtocolName();

	PropertyRow.CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		[
			SAssignNew(ProtocolNameComboBox, SDMXProtocolNameComboBox)
			.InitiallySelectedProtocolName(InitialSelection)
			.OnProtocolNameSelected(this, &FDMXPortConfigCustomizationBase::OnProtocolNameSelected)
		];
}

void FDMXPortConfigCustomizationBase::GenerateCommunicationTypeRow(IDetailPropertyRow& PropertyRow)
{
	// Customizate the Communication Type property depending on the port's supported communication modes

	TAttribute<EVisibility> CommunicationTypeVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXPortConfigCustomizationBase::GetCommunicationTypeVisibility));
	PropertyRow.Visibility(CommunicationTypeVisibilityAttribute);

	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;
	FDetailWidgetRow Row;
	PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

	PropertyRow.CustomWidget()
		.Visibility(CommunicationTypeVisibilityAttribute)
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		[
			SAssignNew(CommunicationTypeComboBox, SDMXCommunicationTypeComboBox)
			.CommunicationTypes(GetSupportedCommunicationTypes())
			.InitialCommunicationType(GetCommunicationType())
			.OnCommunicationTypeSelected(this, &FDMXPortConfigCustomizationBase::OnCommunicationTypeSelected)
		];
}

void FDMXPortConfigCustomizationBase::GenerateIPAddressRow(IDetailPropertyRow& PropertyRow)
{
	// Customizate the IPAddress property to show a combo box with available IP addresses

	// Set the initial edit mode depending on the communication type
	const EDMXCommunicationType CommunicationType = GetCommunicationType();
	const EDMXIPEditWidgetMode IPEditWidgetMode = [CommunicationType](){
		if (CommunicationType == EDMXCommunicationType::Unicast)
		{
			return EDMXIPEditWidgetMode::EditableTextBox;
		}
		return EDMXIPEditWidgetMode::LocalAdapterAddresses;
	}();

	FString InitialValue = GetIPAddress();

	TSharedPtr<SWidget> NameWidget;
	TSharedPtr<SWidget> ValueWidget;
	FDetailWidgetRow Row;
	PropertyRow.GetDefaultWidgets(NameWidget, ValueWidget, Row);

	PropertyRow.CustomWidget()
		.NameContent()
		[
			NameWidget.ToSharedRef()
		]
		.ValueContent()
		[
			SAssignNew(IPAddressEditWidget, SDMXIPAddressEditWidget)
			.Mode(IPEditWidgetMode)
			.InitialValue(InitialValue)
			.OnIPAddressSelected(this, &FDMXPortConfigCustomizationBase::OnIPAddressSelected)
		];
}

EVisibility FDMXPortConfigCustomizationBase::GetCommunicationTypeVisibility() const
{
	if (CommunicationTypeComboBox.IsValid())
	{
		return CommunicationTypeComboBox->GetVisibility();
	}

	return EVisibility::Collapsed;
}

void FDMXPortConfigCustomizationBase::OnProtocolNameSelected()
{
	check(ProtocolNameHandle.IsValid());
	check(ProtocolNameComboBox.IsValid());

	FName ProtocolName = ProtocolNameComboBox->GetSelectedProtocolName();

	const FScopedTransaction Transaction(LOCTEXT("ProtocolSelected", "DMX: Selected Protocol"));
	
	ProtocolNameHandle->NotifyPreChange();
	ProtocolNameHandle->SetValue(ProtocolName);
	ProtocolNameHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	NotifyEditorChangedPortConfig();
}

void FDMXPortConfigCustomizationBase::OnCommunicationTypeSelected()
{
	check(CommunicationTypeHandle.IsValid());
	check(CommunicationTypeComboBox.IsValid());
	check(IPAddressEditWidget.IsValid());

	EDMXCommunicationType SelectedCommunicationType = CommunicationTypeComboBox->GetSelectedCommunicationType();

	const FScopedTransaction Transaction(LOCTEXT("CommunicationTypeSelected", "DMX: Selected Communication Type"));
	
	AddressHandle->NotifyPreChange();
	CommunicationTypeHandle->SetValue(static_cast<uint8>(SelectedCommunicationType));
	AddressHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	// Set the IP Edit Mode depending on the Communication Type
	const EDMXCommunicationType CommunicationType = GetCommunicationType();
	if (CommunicationType == EDMXCommunicationType::Unicast)
	{
		IPAddressEditWidget->SetEditMode(EDMXIPEditWidgetMode::EditableTextBox);
	}
	else
	{
		IPAddressEditWidget->SetEditMode(EDMXIPEditWidgetMode::LocalAdapterAddresses);
	}

	NotifyEditorChangedPortConfig();
}

void FDMXPortConfigCustomizationBase::OnIPAddressSelected()
{
	check(AddressHandle.IsValid());
	check(IPAddressEditWidget.IsValid());

	TSharedPtr<FString> SelectedIP = IPAddressEditWidget->GetSelectedIPAddress();

	const FScopedTransaction Transaction(LOCTEXT("CommunicationTypeSelected", "DMX: Selected IP Address"));
	
	AddressHandle->NotifyPreChange();
	ensure(AddressHandle->SetValue(*SelectedIP) == FPropertyAccess::Success);	
	AddressHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	NotifyEditorChangedPortConfig();
}

#undef LOCTEXT_NAMESPACE
