// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPortReferenceCustomizationBase.h"

#include "DMXProtocolLog.h"
#include "DMXProtocolCommon.h"
#include "ScopedTransaction.h" 
#include "Interfaces/IDMXProtocol.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "Widgets/SDMXPortSelector.h"

#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h" 
#include "IPropertyUtilities.h"
#include "Misc/Guid.h" 
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h" 


#define LOCTEXT_NAMESPACE "DMXPortConfigCustomizationBase"

FDMXPortReferenceCustomizationBase::FDMXPortReferenceCustomizationBase()
{}

void FDMXPortReferenceCustomizationBase::CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
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

void FDMXPortReferenceCustomizationBase::CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils)
{
	PropertyUtilities = StructCustomizationUtils.GetPropertyUtilities();

	// Hide the 'reset to default' option
	StructPropertyHandle->MarkResetToDefaultCustomized();

	// Create the info content border, so the port can set its info during its construction
	SAssignNew(InfoContentBorder, SBorder)
		.BorderImage(FEditorStyle::GetBrush("NoBorder"));

	// Add the port selector row
	EDMXPortSelectorMode PortSelectorMode = IsInputPort() ? EDMXPortSelectorMode::SelectFromAvailableInputs : EDMXPortSelectorMode::SelectFromAvailableOutputs;

	ChildBuilder
		.AddCustomRow(LOCTEXT("PortSelectorSearchString", "Port"))
		.NameContent()
		[
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.Text(LOCTEXT("PortLabel", "Port"))
		]
		.ValueContent()
		[
			SAssignNew(PortSelector, SDMXPortSelector)
			.Mode(PortSelectorMode)
			.OnPortSelected(this, &FDMXPortReferenceCustomizationBase::OnPortSelected)
		];

	// Add the info row
	ChildBuilder.AddCustomRow(LOCTEXT("PortInfoSearchString", "Info"))
		.WholeRowContent()
		[
			InfoContentBorder.ToSharedRef()
		];

	// Four possible states here that all need be handled:
	// a) This is an existing port reference, with a corresponding port config
	// b) This is an existing port reference, but the corresponding port config got deleted in settings
	// c) This is a new port reference and doesn't point to any port yet
	// d) There are no ports specified in project settings

	TSharedPtr<FDMXPort, ESPMode::ThreadSafe> Port = FindPortItem();
	if(Port.IsValid())
	{
		// a) This is an existing port reference, with a corresponding port config
		PortSelector->SelectPort(Port->GetPortGuid());
	}
	else
	{
		FGuid PortGuid = GetPortGuid();
		if (PortGuid.IsValid())
		{
			// b) This is an existing port reference, but the corresponding port config got deleted in settings

			// Let the port remain invalid, but let users know
			ErrorText = LOCTEXT("PortReferenceNoLongerValid", "The referenced Port was deleted. Please select another port.");
			UE_LOG(LogDMXProtocol, Error, TEXT("The referenced Port was deleted. Please review your libraries."));
		}
		else
		{
			if (PortSelector->HasSelection())
			{
				// c) This is a new port reference and doesn't point to any port yet
				//	  The port selector makes an initial selection already, so if it's valid there are ports
				//    It only needs be applied on the new reference.
				ApplySelectedPortGuid();
			}
			else
			{
				// d) There are no ports specified in project settings
				ErrorText = LOCTEXT("NoPortAvailableText", "No ports available. Please create DMX Ports in Project Settings -> Plugins -> DMX Plugin.");
			}
		}
	}

	FDMXPortManager::Get().OnPortsChanged.AddSP(this, &FDMXPortReferenceCustomizationBase::OnPortsChanged);
}

void FDMXPortReferenceCustomizationBase::OnPortSelected()
{
	check(PortSelector.IsValid());
	
	ApplySelectedPortGuid();
}

void FDMXPortReferenceCustomizationBase::OnPortsChanged()
{
	if (PropertyUtilities.IsValid())
	{
		PropertyUtilities->ForceRefresh();
	}
}

TSharedPtr<FDMXPort, ESPMode::ThreadSafe> FDMXPortReferenceCustomizationBase::FindPortItem() const
{
	const FGuid PortGuid = GetPortGuid();

	if (PortGuid.IsValid())
	{
		const FDMXInputPortSharedRef* InputPortPtr = FDMXPortManager::Get().GetInputPorts().FindByPredicate([&PortGuid](const FDMXInputPortSharedRef& InputPort) {
			return InputPort->GetPortGuid() == PortGuid;
			});

		if (InputPortPtr)
		{
			return *InputPortPtr;
		}

		const FDMXOutputPortSharedRef* OutputPortPtr = FDMXPortManager::Get().GetOutputPorts().FindByPredicate([&PortGuid](const FDMXOutputPortSharedRef& OutputPort) {
				return OutputPort->GetPortGuid() == PortGuid;
			});

		if (OutputPortPtr)
		{
			return *OutputPortPtr;
		}
	}

	return nullptr;
}

void FDMXPortReferenceCustomizationBase::ApplySelectedPortGuid()
{
	const FGuid SelectedGuid = [this]()
	{
		if (IsInputPort())
		{
			if (const FDMXInputPortSharedPtr& InputPort = PortSelector->GetSelectedInputPort())
			{
				return InputPort->GetPortGuid();
			}
		}

		if (const FDMXOutputPortSharedPtr& OutputPort = PortSelector->GetSelectedOutputPort())
		{
			return OutputPort->GetPortGuid();
		}

		return FGuid();
	}();

	const TSharedPtr<IPropertyHandle>& PortGuidHandle = GetPortGuidHandle();
	check(PortGuidHandle.IsValid() && PortGuidHandle->IsValidHandle());

	PortGuidHandle->NotifyPreChange();

	TArray<void*> RawData;
	PortGuidHandle->AccessRawData(RawData);

	for(void* RawElement : RawData)
	{
		FMemory::Memcpy(RawElement, &SelectedGuid, sizeof(FGuid));
	}

	PortGuidHandle->NotifyPostChange();
}

FGuid FDMXPortReferenceCustomizationBase::GetPortGuid() const
{
	const TSharedPtr<IPropertyHandle>& PortGuidHandle = GetPortGuidHandle();
	check(PortGuidHandle.IsValid() && PortGuidHandle->IsValidHandle());

	TArray<void*> RawData;
	PortGuidHandle->AccessRawData(RawData);

	if (ensureMsgf(RawData.Num() == 1, TEXT("The port configs reside in an array, multi-editing is unexpected")))
	{
		FGuid* PortGuidPtr = reinterpret_cast<FGuid*>(RawData[0]);
		if (ensureMsgf(PortGuidPtr, TEXT("Expected to be valid")))
		{
			if (PortGuidPtr->IsValid())
			{
				return *PortGuidPtr;
			};
		}
	}

	return FGuid();
}

#undef LOCTEXT_NAMESPACE
