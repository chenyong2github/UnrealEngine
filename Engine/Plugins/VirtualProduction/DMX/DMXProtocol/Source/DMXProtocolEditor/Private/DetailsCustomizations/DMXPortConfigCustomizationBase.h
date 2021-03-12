// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IPropertyTypeCustomization.h"

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"

enum class EDMXCommunicationType : uint8;
class SDMXCommunicationTypeComboBox;
class SDMXIPAddressEditWidget;
class SDMXProtocolNameComboBox;

struct EVisibility;
class IDetailPropertyRow;


/**
 * Base details customization for input and output port configs.
 */
class FDMXPortConfigCustomizationBase
	: public IPropertyTypeCustomization
{
protected:
	// ~Begin IPropertyTypecustomization Interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	// ~End IPropertyTypecustomization Interface

	/** Returns the name of the Protocol Name property */
	virtual FName GetProtocolNamePropertyNameChecked() const = 0;

	/** Returns the name of the Communication Type property */
	virtual FName GetCommunicationTypePropertyNameChecked() const = 0;

	/** Returns the name of the Address property */
	virtual FName GetAddressPropertyNameChecked() const = 0;

	/** Returns the name of the Port Guid property */
	virtual FName GetPortGuidPropertyNameChecked() const = 0;

	/** Returns an array of supported communication types */
	virtual const TArray<EDMXCommunicationType> GetSupportedCommunicationTypes() const = 0;

	/** Gets the protocol, checks if it is valid */
	IDMXProtocolPtr GetProtocolChecked() const;

	/** Helper function that gets the Guid of the edited port */
	FGuid GetPortGuidChecked() const;

	/** Gets the communication type */
	EDMXCommunicationType GetCommunicationType() const;

	/** Gets the IP Address */
	FString GetIPAddress() const;

public:
	/** Notifies others that the port config has changed */
	void NotifyEditorChangedPortConfig();

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

private:
	/** Property handle to the ProtocolName property */
	TSharedPtr<IPropertyHandle> ProtocolNameHandle;

	/** Property handle to the IPAddress property */
	TSharedPtr<IPropertyHandle> AddressHandle;

	/** Property handle to the CommunicationType property */
	TSharedPtr<IPropertyHandle> CommunicationTypeHandle;

	/** Property handle to the PortGuid property */
	TSharedPtr<IPropertyHandle> PortGuidHandle;

	/** ComboBox to select a protocol name */
	TSharedPtr<SDMXProtocolNameComboBox> ProtocolNameComboBox;

	/** ComboBox that displays local ip addresses */
	TSharedPtr<SDMXIPAddressEditWidget> IPAddressEditWidget;

	/** ComboBox that exposes a selection of communication types to the user */
	TSharedPtr<SDMXCommunicationTypeComboBox> CommunicationTypeComboBox;
};
