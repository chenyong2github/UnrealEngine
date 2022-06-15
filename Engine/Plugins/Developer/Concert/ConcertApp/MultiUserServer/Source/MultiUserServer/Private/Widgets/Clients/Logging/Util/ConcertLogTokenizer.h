// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FEndpointToUserNameCache;
struct FConcertLog;

/** Converts members of FConcertLog into a string. Used e.g. to make search respect the display settings. */
class FConcertLogTokenizer
{
public:

	FConcertLogTokenizer(TSharedRef<FEndpointToUserNameCache> EndpointInfoGetter);

	/** Tokenizes a property of FConcertLog into a string */
	FString Tokenize(const FConcertLog& Data, const FProperty& ConcertLogProperty) const;

	FString TokenizeTimestamp(const FConcertLog& Data) const;
	FString TokenizeMessageTypeName(const FConcertLog& Data) const;
	FString TokenizeCustomPayloadUncompressedByteSize(const FConcertLog& Data) const;
	FString TokenizeOriginEndpointId(const FConcertLog& Data) const;
	FString TokenizeDestinationEndpointId(const FConcertLog& Data) const;
	FString TokenizeUsingPropertyExport(const FConcertLog& Data, const FProperty& ConcertLogProperty) const;

private:

	using FTokenizeFunc = TFunction<FString(const FConcertLog&)>;

	/** Override functions for tokenizing certain properties */
	TMap<const FProperty*, FTokenizeFunc> TokenizerFunctions;

	/** Used so we can look up client and server info (even after client has disconnected) */
	TSharedRef<FEndpointToUserNameCache> EndpointInfoGetter;

	FString GetEndpointDisplayString(const FGuid& EndpointId) const;
};
