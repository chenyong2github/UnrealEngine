// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"

class SComboButton;
class UBlueprint;

class SBlueprintHeaderView : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBlueprintHeaderView)
	{
	}

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	/** Gets the text for the class picker combo button */
	FText GetClassPickerText() const;

	/** Constructs a Blueprint Class picker menu widget */
	TSharedRef<SWidget> GetClassPickerMenuContent();

	/** Callback for class picker menu selecting a blueprint asset */
	void OnAssetSelected(const FAssetData& SelectedAsset);

private:
	/** The blueprint currently being displayed by the header view */
	TWeakObjectPtr<UBlueprint> SelectedBlueprint;

	/** Reference to the Class Picker combo button widget */
	TSharedPtr<SComboButton> ClassPickerComboButton;
};