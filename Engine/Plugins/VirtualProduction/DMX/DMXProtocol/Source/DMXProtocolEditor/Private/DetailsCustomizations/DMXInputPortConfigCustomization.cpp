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
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Misc/Guid.h" 
#include "Widgets/Text/STextBlock.h" 


#define LOCTEXT_NAMESPACE "DMXInputPortConfigCustomization"

TSharedRef<IPropertyTypeCustomization> FDMXInputPortConfigCustomization::MakeInstance()
{
	return MakeShared<FDMXInputPortConfigCustomization>();
}

void FDMXInputPortConfigCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	];
}

void FDMXInputPortConfigCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
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
	ProtocolNameHandle = PropertyHandles.FindChecked(FDMXInputPortConfig::GetProtocolNamePropertyNameChecked());
	CommunicationTypeHandle = PropertyHandles.FindChecked(FDMXInputPortConfig::GetCommunicationTypePropertyNameChecked());
	DeviceAddressHandle = PropertyHandles.FindChecked(FDMXInputPortConfig::GetDeviceAddressPropertyNameChecked());
	PortGuidHandle = PropertyHandles.FindChecked(FDMXInputPortConfig::GetPortGuidPropertyNameChecked());

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
		else if (Iter.Key() == FDMXInputPortConfig::GetPriorityStrategyPropertyNameChecked())
		{
			TAttribute<EVisibility> PriorityStrategyVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]() 
				{
					const bool bPriorityStrategyIsVisible = GetProtocol().IsValid() && GetProtocol()->SupportsPrioritySettings();
					return bPriorityStrategyIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
				}));

			PropertyRow.Visibility(PriorityStrategyVisibilityAttribute);
		}
		else if (Iter.Key() == FDMXInputPortConfig::GetPriorityPropertyNameChecked())
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

void FDMXInputPortConfigCustomization::GenerateProtocolNameRow(IDetailPropertyRow& PropertyRow)
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
			.OnProtocolNameSelected(this, &FDMXInputPortConfigCustomization::OnProtocolNameSelected)
		];

}

void FDMXInputPortConfigCustomization::GenerateCommunicationTypeRow(IDetailPropertyRow& PropertyRow)
{
	// Customizate the Communication Type property depending on the port's supported communication modes

	TAttribute<EVisibility> CommunicationTypeVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXInputPortConfigCustomization::GetCommunicationTypeVisibility));
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
			.OnCommunicationTypeSelected(this, &FDMXInputPortConfigCustomization::OnCommunicationTypeSelected)
		];
}

void FDMXInputPortConfigCustomization::GenerateIPAddressRow(IDetailPropertyRow& PropertyRow)
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
			.bShowLocalNICComboBox(true)
			.OnIPAddressSelected(this, &FDMXInputPortConfigCustomization::OnIPAddressSelected)
		];
}	

EVisibility FDMXInputPortConfigCustomization::GetCommunicationTypeVisibility() const
{
	if (CommunicationTypeComboBox.IsValid() && GetProtocol().IsValid())
	{
		return CommunicationTypeComboBox->GetVisibility();
	}

	return EVisibility::Collapsed;
}

void FDMXInputPortConfigCustomization::OnProtocolNameSelected()
{
	FName ProtocolName = ProtocolNameComboBox->GetSelectedProtocolName();

	const FScopedTransaction Transaction(LOCTEXT("ProtocolSelected", "DMX: Selected Protocol"));
	
	ProtocolNameHandle->NotifyPreChange();
	ProtocolNameHandle->SetValue(ProtocolName);
	ProtocolNameHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	PropertyUtilities->ForceRefresh();
}

void FDMXInputPortConfigCustomization::OnCommunicationTypeSelected()
{
	EDMXCommunicationType SelectedCommunicationType = CommunicationTypeComboBox->GetSelectedCommunicationType();

	const FScopedTransaction Transaction(LOCTEXT("CommunicationTypeSelected", "DMX: Selected Communication Type"));
	
	CommunicationTypeHandle->NotifyPreChange();
	CommunicationTypeHandle->SetValue(static_cast<uint8>(SelectedCommunicationType));
	CommunicationTypeHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	PropertyUtilities->ForceRefresh();
}

void FDMXInputPortConfigCustomization::OnIPAddressSelected()
{
	FString SelectedIP = IPAddressEditWidget->GetSelectedIPAddress();

	const FScopedTransaction Transaction(LOCTEXT("IPAddressSelected", "DMX: Selected IP Address"));
	
	DeviceAddressHandle->NotifyPreChange();
	ensure(DeviceAddressHandle->SetValue(SelectedIP) == FPropertyAccess::Success);	
	DeviceAddressHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
}

IDMXProtocolPtr FDMXInputPortConfigCustomization::GetProtocol() const
{
	FName ProtocolName;
	ProtocolNameHandle->GetValue(ProtocolName);

	IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName);

	return Protocol;
}

FGuid FDMXInputPortConfigCustomization::GetPortGuid() const
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

const TArray<EDMXCommunicationType> FDMXInputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	if (IDMXProtocolPtr Protocol = GetProtocol())
	{
		return Protocol->GetInputPortCommunicationTypes();
	}

	return TArray<EDMXCommunicationType>();
}

EDMXCommunicationType FDMXInputPortConfigCustomization::GetCommunicationType() const
{
	uint8 CommunicationType;
	ensure(CommunicationTypeHandle->GetValue(CommunicationType) == FPropertyAccess::Success);

	return static_cast<EDMXCommunicationType>(CommunicationType);
}

FString FDMXInputPortConfigCustomization::GetIPAddress() const
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

#undef LOCTEXT_NAMESPACE
