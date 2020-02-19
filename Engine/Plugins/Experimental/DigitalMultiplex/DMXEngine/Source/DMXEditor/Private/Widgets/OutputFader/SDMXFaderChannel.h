// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class STextBlock;
class FDMXEditor;
class IDMXProtocol;

#define LOCTEXT_NAMESPACE "SDMXFaderChannel"

class SDMXFaderChannel
	: public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDMXFaderChannel)
		: _InText(LOCTEXT("FaderLabel", "Fader"))
		, _UniverseNumber(0)
		, _ChannelNumber(0)
	{}

	SLATE_ARGUMENT(TWeakPtr<FDMXEditor>, DMXEditor)
	SLATE_ARGUMENT(FText, InText)
	SLATE_ARGUMENT(uint16, UniverseNumber)
	SLATE_ARGUMENT(uint32, ChannelNumber)
	SLATE_ARGUMENT(TWeakPtr<IDMXProtocol>, DMXProtocol)

	SLATE_END_ARGS()

	/** Constructs the widget */
	void Construct(const FArguments& InArgs);

	const TSharedPtr<STextBlock> GetUniverseValue() const { return UniverseValue; }

	const TSharedPtr<STextBlock> GetChannelValue() const { return ChannelValue; }

	uint16 GetUniverseNumber() const { return UniverseNumber; }

	uint32 GetChannelNumber() const { return ChannelNumber; }

	const TSharedPtr<IDMXProtocol> GetProtocol() const { return WeakDMXProtocol.Pin(); }

private:
	/** Pointer back to the DMXEditor tool that owns us */
	TWeakPtr<FDMXEditor> WeakDMXEditor;

	TSharedPtr<STextBlock> UniverseValue;

	TSharedPtr<STextBlock> ChannelValue;

	uint16 UniverseNumber;

	uint32 ChannelNumber;

	TWeakPtr<IDMXProtocol> WeakDMXProtocol;
};

#undef LOCTEXT_NAMESPACE