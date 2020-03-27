// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"

#include "DMXProtocolCommon.h"
#include "DMXProtocolTypes.h"

template<typename NumericType>
class SSpinBoxVertical;
class SDMXOutputFaderList;
class SScrollBox;
class FDMXEditor;
class UDMXEntityFader;
class SBorder;
class STextBlock;
class SDMXFaderChannel;
class SCheckBox;
class IDMXProtocol;
struct FDMXUniverse;

#define LOCTEXT_NAMESPACE "SDMXFader"

/** Individual fader UI class */
class SDMXFader
	: public SCompoundWidget
{
public:

	/**
	 * Notification for fader value change
	 * Parameters: Protocol, Universe ID, Channel Address, New Fader Value
	 */
	DECLARE_DELEGATE_OneParam(FOnFaderChanged, TSharedRef<SDMXFader>);

	SLATE_BEGIN_ARGS(SDMXFader)
		: _InText(LOCTEXT("FaderLabel", "Fader"))
		, _OnValueChanged()
		, _OnSendStateChanged()
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
	SLATE_ATTRIBUTE(FText, InText)

	SLATE_EVENT(FOnFaderChanged, OnValueChanged)
	SLATE_EVENT(FOnFaderChanged, OnSendStateChanged)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	void SetFaderEntity(const TWeakObjectPtr<UDMXEntityFader> InFaderEntity);

	void SetParentFaderList(const TSharedPtr<SDMXOutputFaderList>& InFaderList);
		
	void SetFaderLabel(const FString& InLabel);

	void AddChannelWidget(const FString& InUniverse, const FString& InChannel, uint16 InUniverseNumber, uint32 InChannelNumber);

	void RemoveAllChannelWidgets();

	void SelectThisFader();

	const TWeakObjectPtr<UDMXEntityFader>& GetWeakFaderEntity() const { return WeakFaderEntity; }

	const TSharedPtr<SBorder>& GetBackgroundBorder() const { return BackgroundBorder; }

	const TSharedPtr<SSpinBoxVertical<uint8>>& GetFaderBoxVertical() const { return FaderBoxVertical; }

	/** Returns a list of the Universes/Channels this fader widget is using */
	const TArray<TSharedPtr<SDMXFaderChannel>>& GetChannels() const { return FaderChannels; }

	/** Current fader value */
	uint8 GetCurrentValue() const { return CurrentFaderValue; }

	bool ShouldSendDMX() const;

	void SetProtocol(const FDMXProtocolName& InProtocol) { CachedProtocol = InProtocol; }
	FDMXProtocolName GetProtocol() const { return CachedProtocol; }

private:
	//~ Begin SWidget implementation
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ END SWidget implementation

	FReply HandleRemoveFaderClicked();

	void HandleSendDMXCheckChanged(ECheckBoxState NewState);

	void UpdateFaderTemplateProperties();

	/**Change fader background color on hover */
	const FSlateBrush* GetBorderImage() const;

	/** Handles when the user changes the Fader value */
	void HandleFaderChanged(uint8 NewValue);

	FText GetProtocolText() const;

private:
	TSharedPtr<SBorder> BackgroundBorder;

	TSharedPtr<STextBlock> CustomFaderLabel;

	TSharedPtr<SScrollBox> FaderChannelSlots;

	TSharedPtr<SSpinBoxVertical<uint8>> FaderBoxVertical;

	/** The user-selected Fader Label */
	TAttribute<FText> CurrentFaderLabel;

	/** The user-selected Fader Value */
	uint8 CurrentFaderValue;

	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> WeakDMXEditor;

	/** Pointer back to associated fader entity UObject */
	TWeakObjectPtr<UDMXEntityFader> WeakFaderEntity;

	/** Pointer back to fader list widget */
	TWeakPtr<SDMXOutputFaderList> WeakFaderList;

	TArray<TSharedPtr<SDMXFaderChannel>> FaderChannels;

	TSharedPtr<SCheckBox> SendDMXCheckBox;

	FDMXProtocolName CachedProtocol;

	FOnFaderChanged OnValueChanged;
	FOnFaderChanged OnSendStateChanged;
};

#undef LOCTEXT_NAMESPACE