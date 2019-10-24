// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#include "PropertyHandle.h"
#include "SSearchableComboBox.h"
#include "Types/SlateEnums.h"
#include "Widgets/SWidget.h"


// Forward Declarations
class FDetailWidgetRow;


class SSoundModulationControlComboBox : public SSearchableComboBox
{
public:
	SLATE_BEGIN_ARGS(SSoundModulationControlComboBox)
		: _InitControlName(NAME_None)
	{}
		SLATE_ARGUMENT(TSharedPtr<IPropertyHandle>, ControlNameProperty)
		SLATE_ARGUMENT(FName, InitControlName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

protected:
	FText GetControlComboBoxContent() const;
	TSharedPtr<FString> GetControlString(FName ControlName) const;

	TSharedRef<SWidget> MakeControlComboWidget(TSharedPtr<FString> InItem);

	void OnControlChanged(TSharedPtr<FString> NewSelection, ESelectInfo::Type SelectInfo);
	void OnControlComboOpening();

	TSharedPtr<IPropertyHandle> ControlNameProperty;
	TArray<TSharedPtr<FString>> ControlNameStrings;
};
