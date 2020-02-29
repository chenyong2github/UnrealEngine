// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Styling/SlateTypes.h"

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

#define LOCTEXT_NAMESPACE "SDMXFader"

/** Individual fader UI class */
class SDMXFader
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXFader)
		: _InText(LOCTEXT("FaderLabel", "Fader"))
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
	SLATE_ATTRIBUTE(FText, InText)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	void SetFaderEntity(const TWeakObjectPtr<UDMXEntityFader> InFaderEntity);

	void SetParentFaderList(const TSharedPtr<SDMXOutputFaderList>& InFaderList);
		
	void SetFaderLabel(const FString& InLabel);

	void AddChannelWidget(const FString& InUniverse, const FString& InChannel, uint16 InUniverseNumber, uint32 InChannelNumber, const TSharedPtr<IDMXProtocol>& DMXProtocol);

	void RemoveAllChannelWidgets();

	void SelectThisFader();

	const TWeakObjectPtr<UDMXEntityFader>& GetWeakFaderEntity() const { return WeakFaderEntity; }

	const TSharedPtr<SBorder>& GetBackgroundBorder() const { return BackgroundBorder; }

	const TSharedPtr<SSpinBoxVertical<uint8>>& GetFaderBoxVertical() const { return FaderBoxVertical; }

private:
	//~ Begin SWidget implementation
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	//~ END SWidget implementation

	FReply HandleRemoveFaderClicked();

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

	/** Pointer back to assosiated fader entity UObject */
	TWeakObjectPtr<UDMXEntityFader> WeakFaderEntity;

	/** Pointer back to fader list widget */
	TWeakPtr<SDMXOutputFaderList> WeakFaderList;

	TArray<TSharedPtr<SDMXFaderChannel>> FaderChannels;

	TSharedPtr<SCheckBox> SendDMXCheckBox;
};

#undef LOCTEXT_NAMESPACE