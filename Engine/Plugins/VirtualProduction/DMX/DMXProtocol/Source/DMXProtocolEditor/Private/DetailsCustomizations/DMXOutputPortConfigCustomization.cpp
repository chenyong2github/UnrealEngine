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
#include "Widgets/SDMXDelayEditWidget.h"
#include "Widgets/SDMXIPAddressEditWidget.h"
#include "Widgets/SDMXProtocolNameComboBox.h"

#include "DetailWidgetRow.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h" 
#include "IPropertyUtilities.h"
#include "ScopedTransaction.h"
#include "Misc/Guid.h" 
#include "Widgets/Text/STextBlock.h" 


#define LOCTEXT_NAMESPACE "DMXOutputPortConfigCustomization"

TSharedRef<IPropertyTypeCustomization> FDMXOutputPortConfigCustomization::MakeInstance()
{
	return MakeShared<FDMXOutputPortConfigCustomization>();
}

void FDMXOutputPortConfigCustomization::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	HeaderRow
	.NameContent()
	[
		StructPropertyHandle->CreatePropertyNameWidget()
	];
}

void FDMXOutputPortConfigCustomization::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
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
	ProtocolNameHandle = PropertyHandles.FindChecked(FDMXOutputPortConfig::GetProtocolNamePropertyNameChecked());
	CommunicationTypeHandle = PropertyHandles.FindChecked(FDMXOutputPortConfig::GetCommunicationTypePropertyNameChecked());
	DeviceAddressHandle = PropertyHandles.FindChecked(FDMXOutputPortConfig::GetDeviceAddressPropertyNameChecked());
	DelayHandle = PropertyHandles.FindChecked(FDMXOutputPortConfig::GetDelayPropertyNameChecked());
	DelayFrameRateHandle = PropertyHandles.FindChecked(FDMXOutputPortConfig::GetDelayFrameRatePropertyNameChecked());
	PortGuidHandle = PropertyHandles.FindChecked(FDMXOutputPortConfig::GetPortGuidPropertyNameChecked());

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
		// Don't add the PortGuid and the DelayFrameRate properties
		if (Iter.Value() == PortGuidHandle ||
			Iter.Value() == DelayFrameRateHandle)
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
		else if (Iter.Key() == FDMXOutputPortConfig::GetDestinationAddressesPropertyNameChecked())
		{
			UpdateDestinationAddressesVisibility();

			TAttribute<EVisibility> DestinationAddressesVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]() 
				{
					return DestinationAddressesVisibility;
				}));

			PropertyRow.Visibility(DestinationAddressesVisibilityAttribute);
		}
		else if (Iter.Key() == FDMXOutputPortConfig::GetPriorityPropertyNameChecked())
		{
			TAttribute<EVisibility> PriorityVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateLambda([this]() 
				{
					const bool bPriorityIsVisible = GetProtocol().IsValid() && GetProtocol()->SupportsPrioritySettings();
					return bPriorityIsVisible ? EVisibility::Visible : EVisibility::Collapsed;
				}));

			PropertyRow.Visibility(PriorityVisibilityAttribute);
		}
		else if (Iter.Key() == FDMXOutputPortConfig::GetDelayPropertyNameChecked())
		{
			GenerateDelayRow(PropertyRow);
		}
	}
}

void FDMXOutputPortConfigCustomization::GenerateProtocolNameRow(IDetailPropertyRow& PropertyRow)
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
			.OnProtocolNameSelected(this, &FDMXOutputPortConfigCustomization::OnProtocolNameSelected)
		];

}

void FDMXOutputPortConfigCustomization::GenerateCommunicationTypeRow(IDetailPropertyRow& PropertyRow)
{
	// Customizate the Communication Type property depending on the port's supported communication modes

	TAttribute<EVisibility> CommunicationTypeVisibilityAttribute = TAttribute<EVisibility>::Create(TAttribute<EVisibility>::FGetter::CreateSP(this, &FDMXOutputPortConfigCustomization::GetCommunicationTypeVisibility));
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
			.OnCommunicationTypeSelected(this, &FDMXOutputPortConfigCustomization::OnCommunicationTypeSelected)
		];
}

void FDMXOutputPortConfigCustomization::GenerateIPAddressRow(IDetailPropertyRow& PropertyRow)
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
			.OnIPAddressSelected(this, &FDMXOutputPortConfigCustomization::OnIPAddressSelected)
		];
}	

void FDMXOutputPortConfigCustomization::GenerateDelayRow(IDetailPropertyRow& PropertyRow)
{
	// Customizate the IPAddress property to show a combo box with available IP addresses
	const double InitialDelay = GetDelay();
	const TArray<FFrameRate> InitialDelayFrameRates = GetDelayFrameRates();
	
	FFrameRate InitialDelayFrameRate;
	if (ensureMsgf(InitialDelayFrameRates.Num() == 1, TEXT("Multi editing Output Port Configs is not supported.")))
	{
		InitialDelayFrameRate = InitialDelayFrameRates[0];
	}

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
			SAssignNew(DelayEditWidget, SDMXDelayEditWidget)
			.InitialDelay(InitialDelay)
			.InitialDelayFrameRate(InitialDelayFrameRate)
			.OnDelayChanged(this, &FDMXOutputPortConfigCustomization::OnDelayChanged)
			.OnDelayFrameRateChanged(this, &FDMXOutputPortConfigCustomization::OnDelayFrameRateChanged)
		];
}

EVisibility FDMXOutputPortConfigCustomization::GetCommunicationTypeVisibility() const
{
	if (CommunicationTypeComboBox.IsValid() && GetProtocol().IsValid())
	{
return CommunicationTypeComboBox->GetVisibility();
	}

	return EVisibility::Collapsed;
}

void FDMXOutputPortConfigCustomization::OnProtocolNameSelected()
{
	FName ProtocolName = ProtocolNameComboBox->GetSelectedProtocolName();

	const FScopedTransaction Transaction(LOCTEXT("ProtocolSelected", "Select DMX Output Port Protocol"));

	ProtocolNameHandle->NotifyPreChange();
	ProtocolNameHandle->SetValue(ProtocolName);
	ProtocolNameHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	PropertyUtilities->ForceRefresh();
}

void FDMXOutputPortConfigCustomization::OnCommunicationTypeSelected()
{
	EDMXCommunicationType SelectedCommunicationType = CommunicationTypeComboBox->GetSelectedCommunicationType();

	const FScopedTransaction Transaction(LOCTEXT("CommunicationTypeSelected", "Select DMX Output Port Communication Type"));

	CommunicationTypeHandle->NotifyPreChange();
	CommunicationTypeHandle->SetValue(static_cast<uint8>(SelectedCommunicationType));
	CommunicationTypeHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	PropertyUtilities->ForceRefresh();
}

void FDMXOutputPortConfigCustomization::OnIPAddressSelected()
{
	FString SelectedIP = IPAddressEditWidget->GetSelectedIPAddress();

	const FScopedTransaction Transaction(LOCTEXT("IPAddressSelected", "Selected DMX Output Port IP Address"));

	DeviceAddressHandle->NotifyPreChange();
	ensure(DeviceAddressHandle->SetValue(SelectedIP) == FPropertyAccess::Success);
	DeviceAddressHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
}

void FDMXOutputPortConfigCustomization::UpdateDestinationAddressesVisibility()
{
	EDMXCommunicationType CommunicationType = GetCommunicationType();
	if (CommunicationType == EDMXCommunicationType::Unicast)
	{
		DestinationAddressesVisibility = EVisibility::Visible;
	}
	else
	{
		DestinationAddressesVisibility = EVisibility::Collapsed;
	}
}

void FDMXOutputPortConfigCustomization::OnDelayChanged()
{
	// Clamp the delay value to a reasonable range
	static const double MaxDelaySeconds = 60.f;

	const double DesiredDelaySeconds = DelayEditWidget->GetDelay() / DelayEditWidget->GetDelayFrameRate().AsDecimal();
	const double NewDelay = DesiredDelaySeconds > MaxDelaySeconds ? MaxDelaySeconds * DelayEditWidget->GetDelayFrameRate().AsDecimal() : DelayEditWidget->GetDelay();

	const FScopedTransaction Transaction(LOCTEXT("DelalySecondsChange", "Set DMX Output Port Delay"));

	DelayHandle->NotifyPreChange();
	if (DelayHandle->SetValue(NewDelay) == FPropertyAccess::Success)
	{
		DelayHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		PropertyUtilities->ForceRefresh();
	}
}

void FDMXOutputPortConfigCustomization::OnDelayFrameRateChanged()
{
	const FFrameRate DelayFrameRate = DelayEditWidget->GetDelayFrameRate();

	const FScopedTransaction Transaction(LOCTEXT("DelayFrameRateChanged", "Set DMX Output Port Delay Type"));

	DeviceAddressHandle->NotifyPreChange();

	TArray<void*> RawDatas;
	DelayFrameRateHandle->AccessRawData(RawDatas);

	for (void* RawData : RawDatas)
	{
		FFrameRate* FrameRatePtr = (FFrameRate*)RawData;
		*FrameRatePtr = DelayFrameRate;
	}
	DeviceAddressHandle->NotifyPostChange(EPropertyChangeType::ValueSet);

	// Clamp the delay value to a reasonable range
	static const double MaxDelaySeconds = 60.f;

	const double DesiredDelaySeconds = DelayEditWidget->GetDelay() / DelayFrameRate.AsDecimal();
	const double NewDelay = DesiredDelaySeconds > MaxDelaySeconds ? MaxDelaySeconds * DelayFrameRate.AsDecimal() : DelayEditWidget->GetDelay();

	DelayHandle->NotifyPreChange();
	if (DelayHandle->SetValue(NewDelay) == FPropertyAccess::Success)
	{
		DelayHandle->NotifyPostChange(EPropertyChangeType::ValueSet);
		PropertyUtilities->ForceRefresh();
	}
}

double FDMXOutputPortConfigCustomization::GetDelay() const
{
	double Delay;
	if (DelayHandle->GetValue(Delay) == FPropertyAccess::Success)
	{
		return Delay;
	}

	return 0.0;
}

TArray<FFrameRate> FDMXOutputPortConfigCustomization::GetDelayFrameRates() const
{
	TArray<FFrameRate> FrameRates;

	TArray<void*> RawDatas;
	DelayFrameRateHandle->AccessRawData(RawDatas);

	for (void* RawData : RawDatas)
	{
		const FFrameRate* FrameRatePtr = (FFrameRate*)RawData;
		if (FrameRatePtr)
		{
			FrameRates.Add(*FrameRatePtr);
		}
	}

	return FrameRates;
}

IDMXProtocolPtr FDMXOutputPortConfigCustomization::GetProtocol() const
{
	FName ProtocolName;
	ProtocolNameHandle->GetValue(ProtocolName);

	IDMXProtocolPtr Protocol = IDMXProtocol::Get(ProtocolName);
	
	return Protocol;
}

FGuid FDMXOutputPortConfigCustomization::GetPortGuid() const
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

const TArray<EDMXCommunicationType> FDMXOutputPortConfigCustomization::GetSupportedCommunicationTypes() const
{
	if (IDMXProtocolPtr Protocol = GetProtocol())
	{
		return Protocol->GetOutputPortCommunicationTypes();
	}

	return TArray<EDMXCommunicationType>();
}

EDMXCommunicationType FDMXOutputPortConfigCustomization::GetCommunicationType() const
{
	uint8 CommunicationType;
	ensure(CommunicationTypeHandle->GetValue(CommunicationType) == FPropertyAccess::Success);

	return static_cast<EDMXCommunicationType>(CommunicationType);
}

FString FDMXOutputPortConfigCustomization::GetIPAddress() const
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
