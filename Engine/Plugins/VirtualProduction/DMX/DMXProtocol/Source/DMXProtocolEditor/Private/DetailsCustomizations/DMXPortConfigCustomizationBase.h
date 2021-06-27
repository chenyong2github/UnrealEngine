// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"
#include "DMXProtocolCommon.h"
#include "CoreMinimal.h"
#include "Layout/Visibility.h"

enum class EDMXCommunicationType : uint8;
class SDMXCommunicationTypeComboBox;
class SDMXIPAddressEditWidget;
class SDMXProtocolNameComboBox;

struct EVisibility;
class IDetailPropertyRow;
class IPropertyUtilities;

/**
 * Base details customization for input and output port configs.
 */
class FDMXPortConfigCustomizationBase
	: public IPropertyTypeCustomization
{
protected:
	/** Constructor */
	FDMXPortConfigCustomizationBase()
		: DestinationAddressVisibility(EVisibility::Visible)
	{}

	// ~Begin IPropertyTypecustomization Interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	// ~End IPropertyTypecustomization Interface

	/** Returns the name of the Protocol Name property, or NAME_None if it doesn't exist */
	virtual FName GetProtocolNamePropertyName() const = 0;

	/** Returns the name of the Communication Type property, or NAME_None if it doesn't exist */
	virtual FName GetCommunicationTypePropertyName() const = 0;

	/** Returns the name of the Device Address property, or NAME_None if it doesn't exist */
	virtual FName GetDeviceAddressPropertyName() const = 0;

	/** Returns the name of the Pirority Strategy property, or NAME_None if it doesn't exist */
	virtual FName GetDestinationAddressPropertyName() const = 0;

	/** Returns the name of the Pirority Strategy property, or NAME_None if it doesn't exist */
	virtual FName GetPriorityStrategyPropertyName() const = 0;

	/** Returns the name of the Priority property, or NAME_None if it doesn't exist */
	virtual FName GetPriorityPropertyName() const = 0;

	/** Returns the name of the Port Guid property, or NAME_None if it doesn't exist */
	virtual FName GetPortGuidPropertyName() const = 0;

	/** Returns an array of supported communication types */
	virtual const TArray<EDMXCommunicationType> GetSupportedCommunicationTypes() const = 0;

	/** Gets the protocol, checks if it is valid */
	IDMXProtocolPtr GetProtocol() const;

	/** Helper function that gets the Guid of the edited port */
	FGuid GetPortGuid() const;

	/** Gets the communication type */
	EDMXCommunicationType GetCommunicationType() const;

	/** Gets the IP Address */
	FString GetIPAddress() const;

private:
	/** Generates the customized Protocol Name row */
	void GenerateProtocolNameRow(IDetailPropertyRow& PropertyRow);

	/** Generates the customized Communication Type row */
	void GenerateCommunicationTypeRow(IDetailPropertyRow& PropertyRow);

	/** Generates the customized IP Address row */
	void GenerateIPAddressRow(IDetailPropertyRow& PropertyRow);

	/** Returns the visibility of the Communication Type Property */
	EVisibility GetCommunicationTypeVisibility() const;

	/** Called when a Protocol Name was selected */
	void OnProtocolNameSelected();

	/** Called when a local IP Address was selected */
	void OnIPAddressSelected();

	/** Called when a Communication Type was selected */
	void OnCommunicationTypeSelected();

	/** Called when the Destination Address visibility needs to be updated */
	void UpdateDestinationAddressVisibility();

private:
	/** Property handle to the ProtocolName property */
	TSharedPtr<IPropertyHandle> ProtocolNameHandle;

	/** Property handle to the IPAddress property */
	TSharedPtr<IPropertyHandle> DeviceAddressHandle;

	/** Property handle to the CommunicationType property */
	TSharedPtr<IPropertyHandle> CommunicationTypeHandle;

	/** Property handle to the PortGuid property */
	TSharedPtr<IPropertyHandle> PortGuidHandle;

	/** Visibility of the destination address */
	EVisibility DestinationAddressVisibility;

	/** ComboBox to select a protocol name */
	TSharedPtr<SDMXProtocolNameComboBox> ProtocolNameComboBox;

	/** ComboBox that displays local ip addresses */
	TSharedPtr<SDMXIPAddressEditWidget> IPAddressEditWidget;

	/** ComboBox that exposes a selection of communication types to the user */
	TSharedPtr<SDMXCommunicationTypeComboBox> CommunicationTypeComboBox;

	/** Property utilities for this customization */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
