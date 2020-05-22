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

	DECLARE_DELEGATE_OneParam(FOnListenForChanged, const FName&);

	DECLARE_DELEGATE(FOnClearUniverses);
	DECLARE_DELEGATE(FOnClearChannelsView);

	SLATE_BEGIN_ARGS(SDMXInputInfoSelecter)
	{}
	SLATE_EVENT(FOnUniverseSelectionChanged, OnUniverseSelectionChanged)
	SLATE_EVENT(FOnListenForChanged, OnListenForChanged)
	SLATE_EVENT(FOnClearUniverses, OnClearUniverses)
	SLATE_EVENT(FOnClearChannelsView, OnClearChannelsView)
	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	const TSharedRef<SSpinBox<uint32>> GetUniverseField() const { return UniverseIDField.ToSharedRef(); }

	/** Change the current procotol */
	const void SetProtocol(const FName& Name) { CurrentProtocol = FDMXProtocolName(Name); };

	static FName LookForAddresses;
	static FName LookForUniverses;
	

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

	FName GetCurrentListenFor() const { return CurrentListenFor; }

	/** Change between Universe Monitor and Channels Monitor */
	void SetCurrentListenFor(FName InNewListenFor)
	{
		CurrentListenFor = InNewListenFor;
		InitializeInputInfo();
	}

	/** Setup default monitor */
	void InitializeInputInfo();

protected:
	/** Create an option for the Protocol combo box */
	TSharedRef<SWidget> GenerateProtocolItemWidget(TSharedPtr<FName> InItem);
	/** Handles new selection from the Directionality combo box */
	void HandleProtocolChanged(FName SelectedProtocol);
	/** Checks if the selected protocol is still a valid option */
	bool DoesProtocolExist() const;

	/** Handles when the user changes the universe value, including while spinning the value */
	void HandleUniverseIDChanged(uint32 NewValue);

	/** Handles when the user commit the Universe value. Doesn't fire while spinning the value */
	void HandleUniverseIDValueCommitted(uint32 NewValue, ETextCommit::Type CommitType);

	/** Handles when the user changes the type of monitoring */
	void HandleListenForChanged(FName ListenFor);

	/** Handles clear ui values button */
	FReply HandleClearButton();

protected:
	FOnUniverseSelectionChanged OnUniverseSelectionChanged;

	FOnListenForChanged OnListenForChanged;

	FOnClearUniverses OnClearUniverses;

	FOnClearChannelsView OnClearChannelsView;

	TArray<FName> ListenForOptions;

	FName CurrentListenFor;

	TSharedPtr<class STextBlock> UniverseIDLabel;
	TSharedPtr<class SHorizontalBox> UniverseIDSelector;
	TSharedPtr<class SButton> ClearUniverseButton;
}; 
