// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPortConfigCustomizationBase.h"

#include "DMXProtocolModule.h"
#include "DMXProtocolSettings.h"
#include "DMXProtocolTypes.h"
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXOutputPortConfig.h"
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
	DeviceAddressHandle = PropertyHandles.FindChecked(GetDeviceAddressPropertyNameChecked());
	PortGuidHandle = PropertyHandles.FindChecked(GetPortGuidPropertyNameChecked());

	// Ports always need a valid Guid (cannot be blueprinted)
	if (!GetPortGuid().IsValid())
	{
		ChildBuilder.AddCustomRow(LOCTEXT("InvalidPortConfigSearchString", "Invalid"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InvalidPortConfigText", "Cannot utilize Port Configs in Blueprints"))
			];

		return;
	}

	// Add customized properties
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
		else if (Iter.Value() == DeviceAddressHandle)
		{
			GenerateIPAddressRow(PropertyRow);
		}
		if (Iter.Key() == GET_MEMBER_NAME_CHECKED(FDMXOutputPortConfig, DestinationAddress))
		{
			// Customize the destination address for DMXOutputPortConfig only, instead of doing it in DMXOutputPortConfigCustomization.
			// This is not beautiful code, but otherwise the whole customization with almost identical code would have to be moved to child classes.

			// Bind to communication type changes, instead of directly using in the visibility attribute
			// since the CommunicationTypeHandle may no longer be accessible when the getter is called.
			FSimpleDelegate OnCommunicationTypeChangedDelegate = FSimpleDelegate::CreateSP(this, &FDMXPortConfigCustomizationBase::UpdateDestinationAddressVisibility);
			CommunicationTypeHandle->SetOnPropertyValueChanged(OnCommunicationTypeChangedDelegate);

			UpdateDestinationAddressVisibility();

			TAttribute<EVisibility> DestinationAddressVisibilityAttribute =
				TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]() {
				return DestinationAddressVisibility;
			}));

			PropertyRow.Visibility(DestinationAddressVisibilityAttribute);
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

FGuid FDMXPortConfigCustomizationBase::GetPortGuid() const
{
	check(PortGuidHandle.IsValid());

	TArray<void*> RawData;
	PortGuidHandle->AccessRawData(RawData);

	// Multiediting is not supported, may fire if this is used in a blueprint way that would support it
	if (ensureMsgf(RawData.Num() == 1, TEXT("Using port config in ways that would enable multiediting is not supported.")))
	{
		const FGuid* PortGuidPtr = reinterpret_cast<FGuid*>(RawData[0]);
		if (PortGuidPtr && PortGuidPtr->IsValid())
		{
			return *PortGuidPtr;
		}
	}

	return FGuid();
}

FGuid FDMXPortConfigCustomizationBase::GetPortGuidChecked() const
{
	check(PortGuidHandle.IsValid());

	TArray<void*> RawData;
	PortGuidHandle->AccessRawData(RawData);

	// Multiediting is not supported, may fire if this is used in a blueprint way that would support it
	if (ensureMsgf(RawData.Num() == 1, TEXT("Using port config in ways that would enable multiediting is not supported.")))
	{
		const FGuid* PortGuidPtr = reinterpret_cast<FGuid*>(RawData[0]);
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
	check(DeviceAddressHandle.IsValid());

	FString IPAddress;
	ensure(DeviceAddressHandle->GetValue(IPAddress) == FPropertyAccess::Success);

	return IPAddress;
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
}

void FDMXPortConfigCustomizationBase::OnCommunicationTypeSelected()
{
	check(CommunicationTypeHandle.IsValid());
	check(CommunicationTypeComboBox.IsValid());
	check(IPAddressEditWidget.IsValid());

	EDMXCommunicationType SelectedCommunicationType = CommunicationTypeComboBox->GetSelectedCommunicationType();

	const FScopedTransaction Transaction(LOCTEXT("CommunicationTypeSelected", "DMX: Selected Communication Type"));
	
	CommunicationTypeHandle->NotifyPreChange();
	CommunicationTypeHandle->SetValue(static_cast<uint8>(SelectedCommunicationType));
	CommunicationTypeHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
}

void FDMXPortConfigCustomizationBase::OnIPAddressSelected()
{
	check(DeviceAddressHandle.IsValid());
	check(IPAddressEditWidget.IsValid());

	TSharedPtr<FString> SelectedIP = IPAddressEditWidget->GetSelectedIPAddress();

	const FScopedTransaction Transaction(LOCTEXT("CommunicationTypeSelected", "DMX: Selected IP Address"));
	
	DeviceAddressHandle->NotifyPreChange();
	ensure(DeviceAddressHandle->SetValue(*SelectedIP) == FPropertyAccess::Success);	
	DeviceAddressHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
}

void FDMXPortConfigCustomizationBase::UpdateDestinationAddressVisibility()
{
	EDMXCommunicationType CommunicationType = GetCommunicationType();
	if (CommunicationType == EDMXCommunicationType::Unicast)
	{
		DestinationAddressVisibility = EVisibility::Visible;
	}
	else
	{
		DestinationAddressVisibility = EVisibility::Collapsed;
	}
}

#undef LOCTEXT_NAMESPACE
