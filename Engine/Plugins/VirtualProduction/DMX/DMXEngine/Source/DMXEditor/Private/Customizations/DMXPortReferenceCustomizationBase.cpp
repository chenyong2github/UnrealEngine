// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXPortReferenceCustomizationBase.h"

#include "DMXEditorLog.h"
#include "DMXProtocolCommon.h"
#include "ScopedTransaction.h" 
#include "Interfaces/IDMXProtocol.h"
#include "Library/DMXLibrary.h"
#include "IO/DMXPortManager.h"
#include "IO/DMXInputPort.h"
#include "IO/DMXOutputPort.h"
#include "Widgets/SDMXPortSelector.h"

#include "DetailWidgetRow.h"
#include "EditorStyleSet.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailPropertyRow.h" 
#include "Misc/Guid.h" 
#include "Widgets/Text/STextBlock.h" 
#include "Widgets/Layout/SWrapBox.h"


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
			ErrorText = LOCTEXT("PortReferenceNoLongerValid", "The referenced Port got force deleted. Please select another port.");
			UE_LOG(LogDMXEditor, Error, TEXT("A referenced Port got force deleted. Please review your libraries."));
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

	UpdateInfoRow();
}

void FDMXPortReferenceCustomizationBase::OnPortSelected()
{
	check(PortSelector.IsValid());
	
	ApplySelectedPortGuid();

	UpdateInfoRow();
}

void FDMXPortReferenceCustomizationBase::UpdateInfoRow()
{
	check(InfoContentBorder.IsValid());

	const TSharedRef<SWidget> InfoWidget = [this]()
	{	
		if (TSharedPtr<FDMXPort, ESPMode::ThreadSafe> Port = FindPortItem())
		{
			return GeneratePortInfoWidget(Port.ToSharedRef());
		}

		return StaticCastSharedRef<SWidget>(
			SNew(STextBlock)
			.Font(FEditorStyle::GetFontStyle("PropertyWindow.NormalFont"))
			.Text(ErrorText)
		);
	}();

	InfoContentBorder->SetContent(InfoWidget);
}

TSharedRef<SWidget> FDMXPortReferenceCustomizationBase::GeneratePortInfoWidget(TSharedRef<FDMXPort, ESPMode::ThreadSafe> Port) const
{
	IDMXProtocolPtr Protocol = Port->GetProtocol();
	check(Protocol.IsValid());

	FSlateFontInfo PropertyWindowNormalFont = FEditorStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont"));

	const int32 LocalUniverseStart = Port->GetLocalUniverseStart();
	const int32 LocalUniverseEnd = Port->GetLocalUniverseEnd();
	const int32 ExternUniverseStart = Port->GetExternUniverseStart();
	const int32 ExternUniverseEnd = Port->GetExternUniverseEnd();

	const FText PortName = FText::FromString(Port->GetPortName() + TEXT(":"));
	const FText ProtocolName = FText::FromName(Protocol->GetProtocolName());
	const FText LocalUniverseStartText = FText::FromString(FString::FromInt(LocalUniverseStart));
	const FText LocalUniverseEndText = FText::FromString(FString::FromInt(LocalUniverseEnd));
	const FText ExternUniverseStartText = FText::FromString(FString::FromInt(ExternUniverseStart));
	const FText ExternUniverseEndText = FText::FromString(FString::FromInt(ExternUniverseEnd));
	
	return
		SNew(SWrapBox)
		.InnerSlotPadding(FVector2D(4.f, 4.f))
		.UseAllottedWidth(true)

		// Port Name
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(PortName)
		]

		// Protocol Name
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(ProtocolName)
		]
		
		// Local Universe Label
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(LOCTEXT("LocalUniverseStartLabel", "Local Universe:"))
		]

		// Local Universe Start
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(LocalUniverseStartText)
		]

		// Local Universe 'to' Label
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(LOCTEXT("LocalUniverseToLabel", "-"))
		]

		// Local Universe End
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(LocalUniverseEndText)
		]

		// Extern Universe Label
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(LOCTEXT("ExternUniverseStartLabel", "mapped to extern Universe:"))
		]

		// Extern Universe Start
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(ExternUniverseStartText)
		]

		// Extern Universe 'to' Label
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(LOCTEXT("ExternUniverseToLabel", "-"))
		]

		// Extern Universe End
		+ SWrapBox::Slot()
		[
			SNew(STextBlock)
			.Font(PropertyWindowNormalFont)
			.Text(ExternUniverseEndText)
		];
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


	// If the port resides in a DMX Library, raise a property changed event on the outer array to ease handling property changes
	// Begin Library Porperty Change Section
	struct FDMXScopedLibraryChangedHandler
	{
		FDMXScopedLibraryChangedHandler(UDMXLibrary* InLibrary, FProperty* InPortGuidProperty, bool bInputPort)
			: Library(InLibrary)
			, PortGuidProperty(InPortGuidProperty)
			, PortArrayProperty(nullptr)
		{
			if (IsValid(Library))
			{
				PortArrayProperty = [this, bInputPort]() {
					if (bInputPort)
					{
						return FindFProperty<FArrayProperty>(UDMXLibrary::StaticClass(), Library->GetInputPortReferencesPropertyName());
					}

					return FindFProperty<FArrayProperty>(UDMXLibrary::StaticClass(), Library->GetOutputPortReferencesPropertyName());
				}();

				PropertyChain.AddHead(PortArrayProperty);
				PropertyChain.AddTail(PortGuidProperty);
				PropertyChain.SetActiveMemberPropertyNode(PortArrayProperty);
				PropertyChain.SetActivePropertyNode(PortGuidProperty);

				Library->PreEditChange(PortArrayProperty);
				Library->Modify();
			}
		}

		~FDMXScopedLibraryChangedHandler()
		{
			if (IsValid(Library))
			{
				FPropertyChangedEvent PropertyChangedEvent(PortArrayProperty, EPropertyChangeType::ValueSet);
				FPropertyChangedChainEvent PropertyChangedChainEvent(PropertyChain, PropertyChangedEvent);

				Library->PostEditChangeChainProperty(PropertyChangedChainEvent);
			}
		}

	private:
		UDMXLibrary* Library;
		FProperty* PortGuidProperty;
		FArrayProperty* PortArrayProperty;
		FEditPropertyChain PropertyChain;
	};
	
	const FDMXScopedLibraryChangedHandler ScopedLibraryChangedHandler(GetOuterDMXLibrary(), PortGuidHandle->GetProperty(), IsInputPort());
	// End Library Porperty Change Section


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

UDMXLibrary* FDMXPortReferenceCustomizationBase::GetOuterDMXLibrary() const
{
	const TSharedPtr<IPropertyHandle>& PortGuidHandle = GetPortGuidHandle();
	check(PortGuidHandle.IsValid() && PortGuidHandle->IsValidHandle());

	TArray<UObject*> OuterObjects;
	PortGuidHandle->GetOuterObjects(OuterObjects);

	if(OuterObjects.Num() == 1)
	{
		return Cast<UDMXLibrary>(OuterObjects[0]);
	}

	return nullptr;
}

#undef LOCTEXT_NAMESPACE
