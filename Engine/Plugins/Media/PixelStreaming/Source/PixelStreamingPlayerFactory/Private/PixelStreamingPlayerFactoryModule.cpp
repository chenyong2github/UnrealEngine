// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "IMediaOptions.h"
#include "IMediaPlayerFactory.h"
#include "IMediaModule.h"
#include "Internationalization/Internationalization.h"
#include "Modules/ModuleManager.h"
#include "UObject/NameTypes.h"
#include "Misc/Paths.h"
#include "Logging/LogMacros.h"

#include "IPixelStreamingModule.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPixelStreamingPlayerFactory, Log, All); 
DEFINE_LOG_CATEGORY(LogPixelStreamingPlayerFactory);

#define LOCTEXT_NAMESPACE "FPixelStreamingPlayerFactoryModule"

class FFactoryModule
	: public IMediaPlayerFactory
	, public IModuleInterface
{
public:
	FFactoryModule() { }

public:
	// IMediaPlayerFactory interface

	bool CanPlayUrl(const FString& Url, const IMediaOptions* Options, TArray<FText>* OutWarnings, TArray<FText>* OutErrors) const override
	{
		FString Scheme;
		FString Location;

		// check scheme
		if (!Url.Split(TEXT("://"), &Scheme, &Location, ESearchCase::CaseSensitive))
		{
			if (OutErrors != nullptr)
			{
				OutErrors->Add(LOCTEXT("NoSchemeFound", "No URI scheme found"));
			}

			return false;
		}

		if (!SupportedUriSchemes.Contains(Scheme))
		{
			if (OutErrors != nullptr)
			{
				OutErrors->Add(FText::Format(LOCTEXT("SchemeNotSupported", "The URI scheme '{0}' is not supported"), FText::FromString(Scheme)));
			}

			return false;
		}

		return true;
	}

	TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override
	{
		auto PixelStreamingModule = FModuleManager::LoadModulePtr<IPixelStreamingModule>("PixelStreaming");
		return (PixelStreamingModule != nullptr) ? PixelStreamingModule->CreatePlayer(EventSink) : nullptr;
	}

	FText GetDisplayName() const override
	{
		return LOCTEXT("MediaPlayerDisplayName", "PixelStreaming Player");
	}

	virtual FName GetPlayerName() const override
	{
		static FName PlayerName(TEXT("PixelStreamingPlayer"));
		return PlayerName;
	}

	const TArray<FString>& GetSupportedPlatforms() const override
	{
		return SupportedPlatforms;
	}

	bool SupportsFeature(EMediaFeature Feature) const override
	{
		return (Feature == EMediaFeature::VideoSamples);
	}

public:

	// IModuleInterface interface

	void StartupModule() override
	{
		auto PixelStreamingModule = FModuleManager::LoadModulePtr<IPixelStreamingModule>("PixelStreaming");
		if (!PixelStreamingModule || !PixelStreamingModule->IsPlayerInitialized())
		{
			// it can be not initialised for various compatibility problems
			// return early w/o declaring any playback support so effectively disable the plugin
			return;
		}

		// supported platforms
		SupportedPlatforms.Add(TEXT("Windows"));

		// supported schemes
		SupportedUriSchemes.Add(TEXT("webrtc"));

		// register player factory
		auto MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			MediaModule->RegisterPlayerFactory(*this);
		}
	}

	void ShutdownModule() override
	{
		// unregister player factory
		auto MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			MediaModule->UnregisterPlayerFactory(*this);
		}
	}

private:
	/** List of platforms that the media player support. */
	TArray<FString> SupportedPlatforms;

	/** List of supported URI schemes. */
	TArray<FString> SupportedUriSchemes;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FFactoryModule, PixelStreamingPlayerFactory);
