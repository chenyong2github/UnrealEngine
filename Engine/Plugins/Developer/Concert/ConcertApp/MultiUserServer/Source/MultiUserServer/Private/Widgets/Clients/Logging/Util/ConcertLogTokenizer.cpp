// Copyright Epic Games, Inc. All Rights Reserved.

#include "ConcertLogTokenizer.h"

#include "ConcertFrontendUtils.h"
#include "ConcertTransportEvents.h"
#include "MessageTypeUtils.h"
#include "Settings/ConcertTransportLogSettings.h"

FConcertLogTokenizer::FConcertLogTokenizer()
{
	TokenizerFunctions = {
			{ FConcertLog::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConcertLog, Timestamp)), [this](const FConcertLog& Log) { return TokenizeTimestamp(Log); } },
			{ FConcertLog::StaticStruct()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(FConcertLog, MessageTypeName)), [this](const FConcertLog& Log) { return TokenizeMessageTypeName(Log); } }
		};
}

FString FConcertLogTokenizer::Tokenize(const FConcertLog& Data, const FProperty& ConcertLogProperty) const
{
	if (const FTokenizeFunc* CustomTokenizer = TokenizerFunctions.Find(&ConcertLogProperty))
	{
		return (*CustomTokenizer)(Data);
	}
	return TokenizeUsingPropertyExport(Data, ConcertLogProperty);
}

FString FConcertLogTokenizer::TokenizeTimestamp(const FConcertLog& Data) const
{
	return ConcertFrontendUtils::FormatTime(Data.Timestamp, UConcertTransportLogSettings::GetSettings()->TimestampTimeFormat).ToString();
}

FString FConcertLogTokenizer::TokenizeMessageTypeName(const FConcertLog& Data) const
{
	return UE::MultiUserServer::MessageTypeUtils::SanitizeMessageTypeName(Data.MessageTypeName);
}

FString FConcertLogTokenizer::TokenizeUsingPropertyExport(const FConcertLog& Data, const FProperty& ConcertLogProperty) const
{
	FString Exported;
	const void* ValuePtr = ConcertLogProperty.ContainerPtrToValuePtr<void>(&Data);
	const void* DeltaPtr = ValuePtr; // We have no real delta - in this case the API expects the same ptr
	const bool bSuccess = ConcertLogProperty.ExportText_Direct(Exported, ValuePtr, DeltaPtr, nullptr, PPF_ExternalEditor);
	check(bSuccess);

	return Exported;
}
