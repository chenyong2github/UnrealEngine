// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkReplayStreaming.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/ConsoleManager.h"

IMPLEMENT_MODULE( FNetworkReplayStreaming, NetworkReplayStreaming );

INetworkReplayStreamingFactory& FNetworkReplayStreaming::GetFactory(const TCHAR* FactoryNameOverride)
{
	static const FString DefaultFactoryName = TEXT("LocalFileNetworkReplayStreaming");

	FString FactoryName = DefaultFactoryName;

	if (FactoryNameOverride == nullptr)
	{
		GConfig->GetString(TEXT("NetworkReplayStreaming"), TEXT("DefaultFactoryName"), FactoryName, GEngineIni);
	}
	else
	{
		FactoryName = FactoryNameOverride;
	}

	FString CmdlineFactoryName;
	if (FParse::Value(FCommandLine::Get(), TEXT("-REPLAYSTREAMER="), CmdlineFactoryName) || FParse::Value(FCommandLine::Get(), TEXT("-REPLAYSTREAMEROVERRIDE="), CmdlineFactoryName))
	{
		FactoryName = CmdlineFactoryName;
	}

	// See if we need to forcefully fallback to the null streamer
	if (!FModuleManager::Get().IsModuleLoaded(*FactoryName))
	{
		FModuleManager::Get().LoadModule(*FactoryName);
	
		if (!FModuleManager::Get().IsModuleLoaded(*FactoryName))
		{
			FactoryName = DefaultFactoryName;
		}
	}

	LoadedFactories.Add(*FactoryName);

	return FModuleManager::Get().LoadModuleChecked<INetworkReplayStreamingFactory>(*FactoryName);
}

int32 FNetworkReplayStreaming::GetMaxNumberOfAutomaticReplays()
{
	static const int32 DefaultMax = 10;

	int32 MaxAutomaticReplays = DefaultMax;
	GConfig->GetInt(TEXT("NetworkReplayStreaming"), TEXT("MaxNumberAutomaticReplays"), MaxAutomaticReplays, GEngineIni);

	if (!ensureMsgf(MaxAutomaticReplays >= 0, TEXT("INetworkReplayStreamer::GetMaxNumberOfAutomaticReplays: Invalid configured value, using default. %d"), MaxAutomaticReplays))
	{
		MaxAutomaticReplays = DefaultMax;
	}

	return MaxAutomaticReplays;
}

static TAutoConsoleVariable<FString> CVarReplayStreamerAutoDemoPrefix(
	TEXT("demo.ReplayStreamerAutoDemoPrefix"),
	FString(TEXT("demo")),
	TEXT("Prefix to use when generating automatic demo names.")
);

static TAutoConsoleVariable<int32> CVarReplayStreamerAutoDemoUseDateTimePostfix(
	TEXT("demo.ReplayStreamerAutoDemoUseDateTimePostfix"),
	0,
	TEXT("When enabled, uses the current time as a postfix for automatic demo names instead of indices")
);

FString FNetworkReplayStreaming::GetAutomaticReplayPrefix()
{
	return CVarReplayStreamerAutoDemoPrefix.GetValueOnAnyThread();
}

bool FNetworkReplayStreaming::UseDateTimeAsAutomaticReplayPostfix()
{
	return !!CVarReplayStreamerAutoDemoUseDateTimePostfix.GetValueOnAnyThread();
}

const FString FNetworkReplayStreaming::GetAutomaticReplayPrefixExtern() const
{
	return GetAutomaticReplayPrefix();
}

const int32 FNetworkReplayStreaming::GetMaxNumberOfAutomaticReplaysExtern() const
{
	return GetMaxNumberOfAutomaticReplays();
}

void FNetworkReplayStreaming::Flush()
{
	for (const FName& FactoryName : LoadedFactories)
	{
		if (FModuleManager::Get().IsModuleLoaded(FactoryName))
		{
			INetworkReplayStreamingFactory& ReplayFactory = FModuleManager::Get().LoadModuleChecked<INetworkReplayStreamingFactory>(FactoryName);
			ReplayFactory.Flush();
		}
	}
}

bool FNetworkReplayStreaming::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
	// expected usage is "replaystreamer <streamer factory name> <streamer specific args>"
	if (FParse::Command(&Cmd, TEXT("REPLAYSTREAMER")))
	{
		FString FactoryName = FParse::Token(Cmd, false);
		if (!FactoryName.IsEmpty())
		{
			TSharedPtr<INetworkReplayStreamer> Streamer = GetFactory(*FactoryName).CreateReplayStreamer();
			if (Streamer.IsValid())
			{
				Streamer->Exec(Cmd, Ar);
			}
		}

		return true;
	}

	return false;
}

void INetworkReplayStreamer::StartStreaming(const FString& CustomName, const FString& FriendlyName, const TArray<FString>& UserNames, bool bRecord, const FNetworkReplayVersion& ReplayVersion, const FStartStreamingCallback& Delegate)
{
	FStartStreamingParameters Params;
	Params.CustomName = CustomName;
	Params.FriendlyName = FriendlyName;
	
	for (const FString& UserString : UserNames)
	{
		Params.UserIndices.Emplace(GetUserIndexFromUserString(UserString));
	}
	
	Params.bRecord = bRecord;
	Params.ReplayVersion = ReplayVersion;

	StartStreaming(Params, Delegate);
}

void INetworkReplayStreamer::StartStreaming(const FString& CustomName, const FString& FriendlyName, const TArray<int32>& UserIndices, bool bRecord, const FNetworkReplayVersion& ReplayVersion, const FStartStreamingCallback& Delegate)
{
	FStartStreamingParameters Params;
	Params.CustomName = CustomName;
	Params.FriendlyName = FriendlyName;
	Params.UserIndices = UserIndices;
	Params.bRecord = bRecord;
	Params.ReplayVersion = ReplayVersion;

	StartStreaming(Params, Delegate);
}

void INetworkReplayStreamer::EnumerateStreams(const FNetworkReplayVersion& ReplayVersion, const FString& UserString, const FString& MetaString, const FEnumerateStreamsCallback& Delegate)
{
	EnumerateStreams(ReplayVersion, GetUserIndexFromUserString(UserString), MetaString, TArray<FString>(), Delegate);
}

void INetworkReplayStreamer::EnumerateStreams(const FNetworkReplayVersion& ReplayVersion, const FString& UserString, const FString& MetaString, const TArray<FString>& ExtraParms, const FEnumerateStreamsCallback& Delegate)
{
	EnumerateStreams(ReplayVersion, GetUserIndexFromUserString(UserString), MetaString, ExtraParms, Delegate);
}

void INetworkReplayStreamer::EnumerateRecentStreams(const FNetworkReplayVersion& ReplayVersion, const FString& RecentViewer, const FEnumerateStreamsCallback& Delegate)
{
	EnumerateRecentStreams(ReplayVersion, GetUserIndexFromUserString(RecentViewer), Delegate);
}