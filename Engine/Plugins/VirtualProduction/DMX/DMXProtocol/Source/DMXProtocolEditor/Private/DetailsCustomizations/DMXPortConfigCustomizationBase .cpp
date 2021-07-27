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
#include "IPropertyUtilities.h"
#include "IPAddress.h" 
#include "ScopedTransaction.h"
#include "SocketSubsystem.h"
#include "Misc/Guid.h" 
#include "Widgets/Text/STextBlock.h" 


#define LOCTEXT_NAMESPACE "DMXPortConfigCustomization"

void FDMXPortConfigCustomizationBase::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

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
	ProtocolNameHandle = PropertyHandles.FindChecked(GetProtocolNamePropertyName());
	CommunicationTypeHandle = PropertyHandles.FindChecked(GetCommunicationTypePropertyName());
	DeviceAddressHandle = PropertyHandles.FindChecked(GetDeviceAddressPropertyName());
	PortGuidHandle = PropertyHandles.FindChecked(GetPortGuidPropertyName());

	// Ports always need a valid Guid (cannot be blueprinted)
	if (!GetPortGuid().IsValid())
	{
		ChildBuilder.AddCustomRow(LOCTEXT("InvalidPortConfigSearchString", "Invalid"))
			.WholeRowContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("InvalidPortConfigText", "Invalid Port Guid. Cannot utilize this port."))
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
		else if (Iter.Key() == GetDestinationAddressPropertyName())
		{
			UpdateDestinationAddressVisibility();

			TAttribute<EVisibility> DestinationAddressVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]() 
				{
					return DestinationAddressVisibility;
				}));

			PropertyRow.Visibility(DestinationAddressVisibilityAttribute);
		}
		else if (GetPriorityStrategyPropertyName() == Iter.Key())
		{
			TAttribute<EVisibility> PriorityStrategyVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]() 
				{
					const bool bPriorityStrategyIsVisible = GetProtocol().IsValid() && GetProtocol()->SupportsPrioritySettings();
					return bPriorityStrategyIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
				}));

			PropertyRow.Visibility(PriorityStrategyVisibilityAttribute);
		}
		else if (GetPriorityPropertyName() == Iter.Key())
		{
			TAttribute<EVisibility> PriorityVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]() 
				{
					const bool bPriorityIsVisible = GetProtocol().IsValid() && GetProtocol()->SupportsPrioritySettings();
					return bPriorityIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
				}));

			PropertyRow.Visibility(PriorityVisibilityAttribute);
		}
	}
}

IDMXProtocolPtr FDMXPortConfigCustomizationBase::GetProtocol() const
{
	FName ProtocolName;
	ProtocolNameHandle->GetValue(ProtocolName);

	IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName);
	
	return Protocol;
}

FGuid FDMXPortConfigCustomizationBase::GetPortGuid() const
{
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

EDMXCommunicationType FDMXPortConfigCustomizationBase::GetCommunicationType() const
{
	uint8 CommunicationType;
	ensure(CommunicationTypeHandle->GetValue(CommunicationType) == FPropertyAccess::Success);

	return static_cast<EDMXCommunicationType>(CommunicationType);
}

FString FDMXPortConfigCustomizationBase::GetIPAddress() const
{
	FGuid PortGuid = GetPortGuid();
	if (PortGuid.IsValid())
	{
		const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
		checkf(ProtocolSettings, TEXT("Unexpected protocol settings not available when its details are customized"));

		const FDMXInputPortConfig* InputPortConfigPtr = ProtocolSettings->InputPortConfigs.FindByPredicate([&PortGuid](const FDMXInputPortConfig& InputPortConfig)
			{
				return InputPortConfig.GetPortGuid() == PortGuid;
			});
		if (InputPortConfigPtr)
		{
			return InputPortConfigPtr->GetDeviceAddress();
		}

		const FDMXOutputPortConfig* OutputPortConfigPtr = ProtocolSettings->OutputPortConfigs.FindByPredicate([&PortGuid](const FDMXOutputPortConfig& OutputPortConfig)
			{
				return OutputPortConfig.GetPortGuid() == PortGuid;
			});
		if (OutputPortConfigPtr)
		{
			return OutputPortConfigPtr->GetDeviceAddress();
		}
	}

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
	
	const FName InitialSelection = [this]() -> FName
	{
		if (const IDMXProtocolPtr& Protocol = GetProtocol())
		{
			return Protocol->GetProtocolName();
		}

		return NAME_None;
	}();

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
	if (CommunicationTypeComboBox.IsValid() && GetProtocol().IsValid())
	{
		return CommunicationTypeComboBox->GetVisibility();
	}

	return EVisibility::Collapsed;
}

void FDMXPortConfigCustomizationBase::OnProtocolNameSelected()
{
	FName ProtocolName = ProtocolNameComboBox->GetSelectedProtocolName();

	const FScopedTransaction Transaction(LOCTEXT("ProtocolSelected", "DMX: Selected Protocol"));
	
	ProtocolNameHandle->NotifyPreChange();
	ProtocolNameHandle->SetValue(ProtocolName);
	ProtocolNameHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	PropertyUtilities->ForceRefresh();
}

void FDMXPortConfigCustomizationBase::OnCommunicationTypeSelected()
{
	EDMXCommunicationType SelectedCommunicationType = CommunicationTypeComboBox->GetSelectedCommunicationType();

	const FScopedTransaction Transaction(LOCTEXT("CommunicationTypeSelected", "DMX: Selected Communication Type"));
	
	CommunicationTypeHandle->NotifyPreChange();
	CommunicationTypeHandle->SetValue(static_cast<uint8>(SelectedCommunicationType));
	CommunicationTypeHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	PropertyUtilities->ForceRefresh();
}

void FDMXPortConfigCustomizationBase::OnIPAddressSelected()
{
	FString SelectedIP = IPAddressEditWidget->GetSelectedIPAddress();

	const FScopedTransaction Transaction(LOCTEXT("CommunicationTypeSelected", "DMX: Selected IP Address"));
	
	DeviceAddressHandle->NotifyPreChange();
	ensure(DeviceAddressHandle->SetValue(SelectedIP) == FPropertyAccess::Success);	
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
