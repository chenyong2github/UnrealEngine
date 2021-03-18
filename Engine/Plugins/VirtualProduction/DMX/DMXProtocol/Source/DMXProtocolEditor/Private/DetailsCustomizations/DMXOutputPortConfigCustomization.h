// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXPortConfigCustomizationBase.h"

enum class EDMXCommunicationType : uint8;

class IDetailPropertyRow;


/** Details customization for input and output port configs. */
class FDMXOutputPortConfigCustomization
	: public FDMXPortConfigCustomizationBase
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

protected:
	// ~Begin IPropertyTypecustomization Interface
	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> StructPropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> StructPropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& StructCustomizationUtils) override;
	// ~End IPropertyTypecustomization Interface

	// ~Begin DMXPortConfigCustomizationBase Interface
	virtual FName GetProtocolNamePropertyNameChecked() const override;
	virtual FName GetCommunicationTypePropertyNameChecked() const override;
	virtual FName GetAddressPropertyNameChecked() const override;
	virtual FName GetPortGuidPropertyNameChecked() const override;
	virtual const TArray<EDMXCommunicationType> GetSupportedCommunicationTypes() const override;
	// ~End DMXPortConfigCustomizationBase Interface

	/** Called when the communication type changed */
	void OnCommunicationTypeChanged();

private:
	/** Handle for the bLoopbackToEngine property */
	TSharedPtr<IPropertyHandle> LoopbackToEngineHandle;
};
