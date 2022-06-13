// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FConcertLog;

/** Converts members of FConcertLog into a string. Used e.g. to make search respect the display settings. */
class FConcertLogTokenizer
{
public:

	FConcertLogTokenizer();

	/** Tokenizes a property of FConcertLog into a string */
	FString Tokenize(const FConcertLog& Data, const FProperty& ConcertLogProperty) const;

	FString TokenizeTimestamp(const FConcertLog& Data) const;
	FString TokenizeMessageTypeName(const FConcertLog& Data) const;
	FString TokenizeCustomPayloadUncompressedByteSize(const FConcertLog& Data) const;
	FString TokenizeUsingPropertyExport(const FConcertLog& Data, const FProperty& ConcertLogProperty) const;

private:

	using FTokenizeFunc = TFunction<FString(const FConcertLog&)>;

	/** Override functions for tokenizing certain properties */
	TMap<const FProperty*, FTokenizeFunc> TokenizerFunctions;
};
