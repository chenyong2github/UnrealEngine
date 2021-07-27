// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SDMXInputPortReferenceGraphPin.h"

#include "DMXProtocolSettings.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "IO/DMXInputPortReference.h"
#include "Widgets/SDMXPortSelector.h"

#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "SDMXInputPortReferenceGraphPin"

void SDMXInputPortReferenceGraphPin::Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj)
{
	SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
}

TSharedRef<SWidget> SDMXInputPortReferenceGraphPin::GetDefaultValueWidget()
{
	FDMXInputPortReference InitiallySelectedPortReference = GetPinValue();

	// Create entity picker button
	PortSelector = SNew(SDMXPortSelector)
		.Mode(EDMXPortSelectorMode::SelectFromAvailableInputs)
		.InitialSelection(InitiallySelectedPortReference.GetPortGuid())
		.OnPortSelected(this, &SDMXInputPortReferenceGraphPin::OnPortSelected)
		.Visibility(this, &SGraphPin::GetDefaultValueVisibility);
	
	return PortSelector.ToSharedRef();
}

FDMXInputPortReference SDMXInputPortReferenceGraphPin::GetPinValue() const
{
	FDMXInputPortReference PortReference;

	const FString EntityRefStr = GraphPinObj->GetDefaultAsString();
	if (!EntityRefStr.IsEmpty())
	{
		FDMXInputPortReference::StaticStruct()->ImportText(*EntityRefStr, &PortReference, nullptr, EPropertyPortFlags::PPF_None, GLog, FDMXInputPortReference::StaticStruct()->GetName());
	}

	// If no value is set or the value doesn't exist in protocol settings, return the first port in settings instead.
	if (!PortReference.GetPortGuid().IsValid())
	{
		const UDMXProtocolSettings* ProtocolSettings = GetDefault<UDMXProtocolSettings>();
		if (ensure(ProtocolSettings))
		{
			if (ProtocolSettings->InputPortConfigs.Num() > 0)
			{
				PortReference = FDMXInputPortReference(ProtocolSettings->InputPortConfigs[0].GetPortGuid(), true);

				constexpr bool bMarkAsModified = false;
				SetPinValue(PortReference, bMarkAsModified);
			}
		}
	}

	return PortReference;
}
	
void SDMXInputPortReferenceGraphPin::SetPinValue(const FDMXInputPortReference& InputPortReference, bool bMarkAsModified) const
{
	FString ValueString;
	FDMXInputPortReference::StaticStruct()->ExportText(ValueString, &InputPortReference, nullptr, nullptr, EPropertyPortFlags::PPF_None, nullptr);
	GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, ValueString, bMarkAsModified);
}

void SDMXInputPortReferenceGraphPin::OnPortSelected() const
{
	if (PortSelector.IsValid())
	{
		const FScopedTransaction Transaction(LOCTEXT("ChangeObjectPinValue", "Select DMX Port"));
		GraphPinObj->Modify();

		const FGuid& PortGuid = [this]() {
			if (FDMXInputPortSharedPtr SelectedInputPort = PortSelector->GetSelectedInputPort())
			{
				return SelectedInputPort->GetPortGuid();
			}
			else if (FDMXOutputPortSharedPtr SelectedOutputPort = PortSelector->GetSelectedOutputPort())
			{
				return SelectedOutputPort->GetPortGuid();
			}

			return FGuid::NewGuid();
		}();


		FDMXInputPortReference PortReference(PortGuid, true);

		SetPinValue(PortReference);
	}
}

#undef LOCTEXT_NAMESPACE
