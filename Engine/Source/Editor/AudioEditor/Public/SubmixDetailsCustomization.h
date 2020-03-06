// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Layout/Visibility.h"
#include "Input/Reply.h"
#include "IDetailCustomization.h"
#include "IPropertyTypeCustomization.h"
#include "PropertyHandle.h"

class IDetailLayoutBuilder;
class IDetailGroup;

// Utility class to build combo boxes out of arrays of names.
class FNameSelectorGenerator : public TSharedFromThis<FNameSelectorGenerator>
{

public:
	struct FNameSelectorCallbacks
	{
		TUniqueFunction<void(FName)> OnNewNameSelected;
		TUniqueFunction<FName()> GetCurrentlySelectedName;
		TUniqueFunction<FString()> GetTooltipText;
	};

	// Use this to generate a combo box widget.
	TSharedRef<SWidget> MakeNameSelectorWidget(TArray<FName>& InNameArray, FNameSelectorCallbacks&& InCallbacks);

	

protected:
	void OnSelectionChanged(TSharedPtr<FName> NameItem, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> HandleResponseComboBoxGenerateWidget(TSharedPtr<FName> StringItem);
	FText GetComboBoxToolTip() const;
	FText GetComboBoxContent() const;

	TArray<TSharedPtr<FName>> CachedNameArray;
	FNameSelectorCallbacks CachedCallbacks;
};

class AUDIOEDITOR_API FSoundfieldSubmixDetailsCustomization : public IDetailCustomization
{
public:
	// Makes a new instance of this detail layout class
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:
	TSharedPtr<FNameSelectorGenerator> SoundfieldFormatNameSelectorGenerator;
};

class AUDIOEDITOR_API FEndpointSubmixDetailsCustomization : public IDetailCustomization, FNameSelectorGenerator
{
public:
	// Makes a new instance of this detail layout class
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:
	TSharedPtr<FNameSelectorGenerator> EndpointTypeNameSelectorGenerator;
};

class AUDIOEDITOR_API FSoundfieldEndpointSubmixDetailsCustomization : public IDetailCustomization, FNameSelectorGenerator
{
public:
	// Makes a new instance of this detail layout class
	static TSharedRef<IDetailCustomization> MakeInstance();

	// IDetailCustomization interface
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;
	// End of IDetailCustomization interface

private:
	TSharedPtr<FNameSelectorGenerator> EndpointTypeNameSelectorGenerator;
};
