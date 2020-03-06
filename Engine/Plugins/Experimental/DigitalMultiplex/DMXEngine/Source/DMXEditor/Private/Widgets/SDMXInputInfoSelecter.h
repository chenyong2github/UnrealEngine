// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Types/SlateEnums.h"

#include "Interfaces/IDMXProtocol.h"
#include "DMXProtocolTypes.h"

template<typename NumericType>
class SSpinBox;

/** Widget to configure DMX Input inspector settings */
class SDMXInputInfoSelecter
	: public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnUniverseSelectionChanged, const FName&);

	SLATE_BEGIN_ARGS(SDMXInputInfoSelecter)
	{}
	SLATE_EVENT(FOnUniverseSelectionChanged, OnUniverseSelectionChanged)
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	const TSharedRef<SSpinBox<uint32>> GetUniverseField() const { return UniverseIDField.ToSharedRef(); }
	const void SetProtocol(const FName& Name) { CurrentProtocol = FDMXProtocolName(Name); };

protected:
	/** Universe ID value computed using Net, Subnet and Universe values */
	uint16 CurrentUniverseID;
	/** Final Universe field widget */
	TSharedPtr<SSpinBox<uint32>> UniverseIDField;

	/** The user-selected protocol */
	FDMXProtocolName CurrentProtocol;

public:
	/** Returns a label text with the currently selected directionality */
	//const FText& GetCurrentDirectionality() const { return CurrentDirectionality; }

	/** Returns the UniverseID value computed from Net, Subnet and Universe values */
	uint32 GetCurrentUniverseID() const { return CurrentUniverseID; }

	/** Returns the user-selected DMX protocol */
	FName GetCurrentProtocolName() const { return CurrentProtocol; }

protected:
	/** Create an option for the Protocol combo box */
	TSharedRef<SWidget> GenerateProtocolItemWidget(TSharedPtr<FName> InItem);
	/** Handles new selection from the Directionality combo box */
	void HandleProtocolChanged(FName SelectedProtocol);

	/** Handles when the user changes the universe value, including while spinning the value */
	void HandleUniverseIDChanged(uint32 NewValue);

	/** Handles when the user commit the Universe value. Doesn't fire while spinning the value */
	void HandleUniverseIDValueCommitted(uint32 NewValue, ETextCommit::Type CommitType);

protected:
	FOnUniverseSelectionChanged OnUniverseSelectionChanged;
}; 
