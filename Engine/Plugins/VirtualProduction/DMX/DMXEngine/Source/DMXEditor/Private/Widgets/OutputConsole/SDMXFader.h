// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DMXProtocolCommon.h"
#include "DMXProtocolTypes.h"

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "UObject/NameTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SComboBox.h"

class SDMXOutputFaderList;
class IDMXProtocol;

struct FSpinBoxStyle;
class SBorder;
class STextBlock;

template<typename NumericType>
class SSpinBoxVertical;
class SInlineEditableTextBlock;


/** Individual fader UI class */
class SDMXFader
	: public SCompoundWidget
{	
	DECLARE_DELEGATE_OneParam(FDMXFaderDelegate, TSharedRef<SDMXFader>);

public:
	SLATE_BEGIN_ARGS(SDMXFader)
		: _FaderName()
		, _Value(0)
		, _MaxValue(255)
		, _MinValue(0)
		, _UniverseID(1)
		, _StartingAddress(1)
		, _EndingAddress(1)
		, _ProtocolName(NAME_None)
	{}
		/** The name displayed above the fader */
		SLATE_ARGUMENT(FText, FaderName)

		/** The value of the fader */
		SLATE_ARGUMENT(int32, Value)

		/** The max value of the fader */
		SLATE_ARGUMENT(int32, MaxValue)

		/** The min value of the fader */
		SLATE_ARGUMENT(int32, MinValue)

		/** The universe the fader sends DMX to */
		SLATE_ARGUMENT(int32, UniverseID)

		/** The starting channel Address to send DMX to */
		SLATE_ARGUMENT(int32, StartingAddress)

		/** The end channel Address to send DMX to */
		SLATE_ARGUMENT(int32, EndingAddress)

		/** The protocol name which should be used to send dmx */
		SLATE_ARGUMENT(FName, ProtocolName)

		/** Called when the fader gets selected */
		SLATE_EVENT(FDMXFaderDelegate, OnRequestDelete)

		/** Called when the fader got selected */
		SLATE_EVENT(FDMXFaderDelegate, OnRequestSelect)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	/** Flags the fader selected and highlights it */
	void Select();

	/** Flags the fader unselected clears the highlight */
	void Unselect();

	/** Returns wether the fader is flagged selected */
	bool IsSelected() const { return bSelected; }

private:
	/** Sends the current value to the channels in universe specified by the widget */
	void SendDMX();

	/** True if the fader is selected */
	bool bSelected = false;

protected:
	//~ Begin SWidget implementation
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	//~ End SWidget implementation

private:
	/** Generates a widget to edit the adresses the fader sould send DMX to */
	TSharedRef<SWidget> GenerateAdressEditWidget();

	/** Generates the protocol combo box */
	TSharedRef<SComboBox<TSharedPtr<FName>>> GenerateProtocolComboBox(const FName& InitialProtocolName);

	/** Generates an entry in the protocol combo box */
	TSharedRef<SWidget> GenerateProtocolComboBoxEntry(TSharedPtr<FName> ProtocolName);

	/** Returns the selected protocol name as text */
	FText GetSelectedProtocolText() const;

	/** Called when a protocol was selected */
	void OnProtocolSelected(TSharedPtr<FName> NewProtocolName, ESelectInfo::Type SelectInfo);

	/** Combo box with available protocols */
	TSharedPtr<SComboBox<TSharedPtr<FName>>> ProtocolComboBox;

	/** Protocols to be shown in the protocol combo box */
	TArray<TSharedPtr<FName>> ProtocolNameArray;

	/** The protocol that is used to send dmx */
	IDMXProtocolPtr Protocol;

public:
	/** Returns the name of the fader */
	FString GetFaderName() const { return FaderName; };

	/** Returns the universe ID to which to should send DMX to */
	int32 GetUniverseID() const { return UniverseID; }

	/** Returns the Starting Channel of where to send DMX to */
	int32 GetStartingAddress() const { return StartingAddress; }

	/** Returns the Ending Channel to which to send DMX to */
	int32 GetEndingAddress() const { return StartingAddress; }

	/** Gets the value of the fader */
	uint8 GetValue() const;

	/** Sets the value of the fader by a percentage value */
	void SetValueByPercentage(float InNewPercentage);

	/** Gets the min value of the fader */
	uint8 GetMinValue() const { return MinValue; }

	/** Gets the max value of the fader */
	uint8 GetMaxValue() const { return MaxValue; }

private:
	/** Cached Name of the Fader */
	FString FaderName;

	/** The universe the should send to fader */
	int32 UniverseID;

	/** The starting channel Address to send DMX to */
	int32 StartingAddress;

	/** The end channel Address to send DMX to */
	int32 EndingAddress;

	/** The current Fader Value */
	uint8 Value;

	/** The minimum Fader Value */
	uint8 MinValue;

	/** The maximum Fader Value */
	uint8 MaxValue;

private:
	/** Called when the delete button was clicked */
	FReply OnDeleteClicked();

	/** Handles when the user changes the Fader value */
	void HandleValueChanged(uint8 NewValue);

	/** Called when the fader name changes */
	void OnFaderNameCommitted(const FText& NewFaderName, ETextCommit::Type InCommit);

	/** Called when the UniverseID border was doubleclicked, to give some more click-space to users */
	FReply OnUniverseIDBorderDoubleClicked(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent);

	/** Called when the Starting Adress border was doubleclicked, to give some more click-space to users */
	FReply OnStartingAddressBorderDoubleClicked(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent);

	/** Called when the Ending Adress was doubleclicked, to give some more click-space to users */
	FReply OnEndingAddressBorderDoubleClicked(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent);

	/** Returns the UniverseID as text */
	FText GetUniverseIDAsText() const { return FText::FromString(FString::FromInt(UniverseID)); }

	/** Called when UniverseID changes, but is not commited yet */
	bool VerifyUniverseID(const FText& UniverseIDText, FText& OutErrorText);

	/** Called when the UniverseID was commited */
	void OnUniverseIDCommitted(const FText& UniverseIDText, ETextCommit::Type InCommit);

	/** Returns the max value as text */
	FText GetStartingAddressAsText() const { return FText::FromString(FString::FromInt(StartingAddress)); }

	/** Called when max value changes, but is not commited yet */
	bool VerifyStartingAddress(const FText& StartingAddressText, FText& OutErrorText);

	/** Called when the UniverseID was commited */
	void OnStartingAddressCommitted(const FText& StartingAddressText, ETextCommit::Type InCommit);

	/** Returns the max value as text */
	FText GetEndingAddressAsText() const { return FText::FromString(FString::FromInt(EndingAddress)); }

	/** Called when max value changes, but is not commited yet */
	bool VerifyEndingAddress(const FText& EndingAddressText, FText& OutErrorText);

	/** Called when the UniverseID was commited */
	void OnEndingAddressCommitted(const FText& EndingAddressText, ETextCommit::Type InCommit);

	/** Returns the max value as text */
	FText GetMaxValueAsText() const { return FText::FromString(FString::FromInt(MaxValue)); }

	/** Called when max value changes, but is not commited yet */
	bool VerifyMaxValue(const FText& MaxValueText, FText& OutErrorText);

	/** Called when the max value was commited */
	void OnMaxValueCommitted(const FText& MaxValueText, ETextCommit::Type InCommit);

	/** Returns the min value as text */
	FText GetMinValueAsText() const { return FText::FromString(FString::FromInt(MinValue)); }

	/** Called when min value changes, but is not commited yet */
	bool VerifyMinValue(const FText& MinValueText, FText& OutErrorText);

	/** Called when the min value was commited */
	void OnMinValueCommitted(const FText& MinValueText, ETextCommit::Type InCommit);

	/**Change fader background color on hover */
	const FSlateBrush* GetBorderImage() const;

public:
	/** Gets the protocol name used for this fader */
	const FName& GetProtocolName() const;

	/** Gets the protocol used for this fader */
	const IDMXProtocolPtr& GetProtocol() const { return Protocol; }

private:
	/** Background of the fader */
	TSharedPtr<SBorder> BackgroundBorder;

	/** SpinBox Style of the fader */
	FSpinBoxStyle OutputFaderStyle;

	/** Widget showing the freely definable name of the fader */
	TSharedPtr<SInlineEditableTextBlock> FaderNameTextBox;

	/** The actual editable fader */
	TSharedPtr<SSpinBoxVertical<uint8>> FaderSpinBox;

	/** Textblock to edit the UniverseID */
	TSharedPtr<SInlineEditableTextBlock> UniverseIDEditableTextBlock;

	/** Textblock to edit the Starting Adress */
	TSharedPtr<SInlineEditableTextBlock> StartingAddressEditableTextBlock;

	/** Textblock to edit the Ending Adress */
	TSharedPtr<SInlineEditableTextBlock> EndingAddressEditableTextBlock;

	/** Called when the fader wants to be deleted */
	FDMXFaderDelegate OnRequestDelete;

	/** Called when the fader wants to be selected */
	FDMXFaderDelegate OnRequestSelect;
};
