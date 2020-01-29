// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Customizations/MediaIOCustomizationBase.h"

#include "AjaDeviceProvider.h"
#include "Input/Reply.h"

/**
 * Implements a details view customization for the FAjaMediaTimecodeConfiguration
 */
class FAjaMediaTimecodeConfigurationCustomization : public FMediaIOCustomizationBase
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance() { return MakeShareable(new FAjaMediaTimecodeConfigurationCustomization); }

private:
	virtual TAttribute<FText> GetContentText() override;
	virtual TSharedRef<SWidget> HandleSourceComboButtonMenuContent() override;

	void OnSelectionChanged(FAjaMediaTimecodeConfiguration SelectedItem);
	FReply OnButtonClicked() const;

	TWeakPtr<SWidget> PermutationSelector;
	FAjaMediaTimecodeConfiguration SelectedConfiguration;
};
