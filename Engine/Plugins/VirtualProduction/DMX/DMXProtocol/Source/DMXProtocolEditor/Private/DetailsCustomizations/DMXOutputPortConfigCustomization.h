// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"

#include "CoreMinimal.h"
#include "IPropertyTypeCustomization.h"
#include "Layout/Visibility.h"

enum class EDMXCommunicationType : uint8;
class SDMXCommunicationTypeComboBox;
class SDMXDelayEditWidget;
class SDMXIPAddressEditWidget;
class SDMXProtocolNameComboBox;

struct FFrameRate;
class IDetailPropertyRow;
class IPropertyHandle;
class IPropertyUtilities;


/** Details customization for input and output port configs. */
class FDMXOutputPortConfigCustomization
	: public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

protected:
	// ~Begin IPropertyTypecustomization Interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	// ~End IPropertyTypecustomization Interface

private:
	/** Generates the customized Protocol Name row */
	void GenerateProtocolNameRow(IDetailPropertyRow& PropertyRow);

	/** Generates the customized Communication Type row */
	void GenerateCommunicationTypeRow(IDetailPropertyRow& PropertyRow);

	/** Generates the customized IP Address row */
	void GenerateIPAddressRow(IDetailPropertyRow& PropertyRow);

	/** Generates the customized Delay Seconds row */
	void GenerateDelayRow(IDetailPropertyRow& PropertyRow);

	/** Returns the visibility of the Communication Type Property */
	EVisibility GetCommunicationTypeVisibility() const;

	/** Called when a Protocol Name was selected */
	void OnProtocolNameSelected();

	/** Called when a local IP Address was selected */
	void OnIPAddressSelected();

	/** Called when a Communication Type was selected */
	void OnCommunicationTypeSelected();

	/** Called when the Destination Addresses Array visibility needs to be updated */
	void UpdateDestinationAddressesVisibility();

	/** Called when the Delay Seconds changed in the Delay Edit Widget */
	void OnDelayChanged();

	/** Called when the Delay Frame Rate changed in the Delay Edit Widget */
	void OnDelayFrameRateChanged();

	/** Returns the delay of the port */
	double GetDelay() const;

	/** Returns the delay frame rate of the port */
	TArray<FFrameRate> GetDelayFrameRates() const;

	/** Gets the protocol, checks if it is valid */
	IDMXProtocolPtr GetProtocol() const;

	/** Helper function that gets the Guid of the edited port */
	FGuid GetPortGuid() const;

	/** Returns an array of supported communication types */
	const TArray<EDMXCommunicationType> GetSupportedCommunicationTypes() const;

	/** Gets the communication type */
	EDMXCommunicationType GetCommunicationType() const;

	/** Gets the IP Address */
	FString GetIPAddress() const;

	/** Property handle to the ProtocolName property */
	TSharedPtr<IPropertyHandle> ProtocolNameHandle;

	/** Property handle to the IPAddress property */
	TSharedPtr<IPropertyHandle> DeviceAddressHandle;

	/** Property handle to the CommunicationType property */
	TSharedPtr<IPropertyHandle> CommunicationTypeHandle;

	/** Property handle to the Delay property */
	TSharedPtr<IPropertyHandle> DelayHandle;

	/** Property handle to the DelayFrameRate property */
	TSharedPtr<IPropertyHandle> DelayFrameRateHandle;

	/** Property handle to the PortGuid property */
	TSharedPtr<IPropertyHandle> PortGuidHandle;

	/** Visibility of the destination addresses array */
	EVisibility DestinationAddressesVisibility;

	/** ComboBox to select a protocol name */
	TSharedPtr<SDMXProtocolNameComboBox> ProtocolNameComboBox;

	/** ComboBox that displays local ip addresses */
	TSharedPtr<SDMXIPAddressEditWidget> IPAddressEditWidget;

	/** ComboBox that exposes a selection of communication types to the user */
	TSharedPtr<SDMXCommunicationTypeComboBox> CommunicationTypeComboBox;

	/** Custom widget that lets the user set the delay */
	TSharedPtr<SDMXDelayEditWidget> DelayEditWidget;

	/** Property utilities for this customization */
	TSharedPtr<IPropertyUtilities> PropertyUtilities;
};
