// Copyright Epic Games, Inc. All Rights Reserved.

#include "Settings.h"
#include "Utils.h"

template <typename T>
void CommandLineParseValue(const TCHAR* Match, TAutoConsoleVariable<T>& CVar)
{
	T Value;
	if (FParse::Value(FCommandLine::Get(), Match, Value))
		CVar->Set(Value, ECVF_SetByCommandline);
};

void CommandLineParseValue(const TCHAR* Match, TAutoConsoleVariable<FString>& CVar, bool bStopOnSeparator = false)
{
	FString Value;
	if (FParse::Value(FCommandLine::Get(), Match, Value, bStopOnSeparator))
		CVar->Set(*Value, ECVF_SetByCommandline);
};

void CommandLineParseOption(const TCHAR* Match, TAutoConsoleVariable<bool>& CVar)
{
	FString ValueMatch(Match);
	ValueMatch.Append(TEXT("="));
	FString Value;
	if (FParse::Value(FCommandLine::Get(), *ValueMatch, Value))
	{
		if (Value.Equals(FString(TEXT("true")), ESearchCase::IgnoreCase))
		{
			CVar->Set(true, ECVF_SetByCommandline);
		}
		else if (Value.Equals(FString(TEXT("false")), ESearchCase::IgnoreCase))
		{
			CVar->Set(false, ECVF_SetByCommandline);
		}
	}
	else if (FParse::Param(FCommandLine::Get(), Match))
	{
		CVar->Set(true, ECVF_SetByCommandline);
	}
}

namespace UE::PixelStreaming::Settings::Editor
{
    TAutoConsoleVariable<bool> CVarEditorPixelStreamingStartOnLaunch(
		TEXT("PixelStreaming.Editor.StartOnLaunch"),
		false,
		TEXT("Start streaming the Editor as soon as it launches. Default: false"),
		ECVF_Default);

    void InitialiseSettings()
    {
        // Options parse (if these exist they are set to true)
		CommandLineParseOption(TEXT("EditorPixelStreamingStartOnLaunch"), CVarEditorPixelStreamingStartOnLaunch);
    }
}