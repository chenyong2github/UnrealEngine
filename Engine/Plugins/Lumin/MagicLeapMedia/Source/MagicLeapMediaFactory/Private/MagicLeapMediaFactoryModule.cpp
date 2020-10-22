// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "IMediaModule.h"
#include "IMediaOptions.h"
#include "IMagicLeapMediaModule.h"
#include "MagicLeapMediaFactoryPrivate.h"
#include "IMediaPlayerFactory.h"

DEFINE_LOG_CATEGORY(LogMagicLeapMediaFactory);

#define LOCTEXT_NAMESPACE "FMagicLeapMediaFactoryModule"

/**
 * Implements the MagicLeapMediaFactory module.
 */
class FMagicLeapMediaFactoryModule : public IMediaPlayerFactory, public IModuleInterface
{
public:

	/** IMediaPlayerFactory interface */
	virtual bool CanPlayUrl(const FString& Url, const IMediaOptions* Options, TArray<FText>* OutWarnings, TArray<FText>* OutErrors) const override
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

		// check file extension
		if (Scheme == TEXT("file"))
		{
			const FString Extension = FPaths::GetExtension(Location, false);

			if (!SupportedFileExtensions.Contains(Extension))
			{
				if (OutErrors != nullptr)
				{
					OutErrors->Add(FText::Format(LOCTEXT("ExtensionNotSupported", "The file extension '{0}' is not supported"), FText::FromString(Extension)));
				}

				return false;
			}
		}

		// check options
		if ((OutWarnings != nullptr) && (Options != nullptr))
		{
			if (Options->GetMediaOption("PrecacheFile", false))
			{
				OutWarnings->Add(LOCTEXT("PrecachingNotSupported", "Precaching is not supported in Magic Leap Media. Use Magic Leap Media Codec instead if this feature is necessarry."));
			}
		}

		return true;
	}

	virtual TSharedPtr<IMediaPlayer, ESPMode::ThreadSafe> CreatePlayer(IMediaEventSink& EventSink) override
	{
		auto MagicLeapMediaModule = FModuleManager::LoadModulePtr<IMagicLeapMediaModule>("MagicLeapMedia");
		return (MagicLeapMediaModule != nullptr) ? MagicLeapMediaModule->CreatePlayer(EventSink) : nullptr;
	}

	virtual FText GetDisplayName() const override
	{
		return LOCTEXT("MediaPlayerDisplayName", "MagicLeap Media");
	}

	virtual FName GetPlayerName() const override
	{
		static FName PlayerName(TEXT("MagicLeapMedia"));
		return PlayerName;
	}

	virtual FGuid GetPlayerPluginGUID() const override
	{
		static FGuid PlayerPluginGUID(0x9af3a209, 0xed2641e8, 0x9bcee61b, 0x2d529a06);
		return PlayerPluginGUID;
	}

	virtual const TArray<FString>& GetSupportedPlatforms() const override
	{
		return SupportedPlatforms;
	}

	virtual bool SupportsFeature(EMediaFeature Feature) const
	{
		return ((Feature == EMediaFeature::AudioTracks) ||
			(Feature == EMediaFeature::VideoSamples) ||
			(Feature == EMediaFeature::VideoTracks));
	}

public:

	/** IModuleInterface interface */
	
	virtual void StartupModule() override
	{
		// supported file extensions
		SupportedFileExtensions.Add(TEXT("3gp"));
		SupportedFileExtensions.Add(TEXT("mp4"));
		SupportedFileExtensions.Add(TEXT("mp4a"));
		SupportedFileExtensions.Add(TEXT("aac"));
		SupportedFileExtensions.Add(TEXT("ts"));
		SupportedFileExtensions.Add(TEXT("mkv"));
		SupportedFileExtensions.Add(TEXT("webm"));
		SupportedFileExtensions.Add(TEXT("m4v"));

    	// supported platforms
    	SupportedPlatforms.Add(TEXT("Lumin"));

		// supported schemes
		SupportedUriSchemes.Add(TEXT("file"));
		SupportedUriSchemes.Add(TEXT("http"));
		SupportedUriSchemes.Add(TEXT("https"));
		SupportedUriSchemes.Add(TEXT("rtsp"));
		// custom schema for user shared files
		SupportedUriSchemes.Add(TEXT("mlshared"));
		// Not supporting streaming right now.
		// SupportedUriSchemes.Add(TEXT("httpd"));
		// SupportedUriSchemes.Add(TEXT("mms"));
		// SupportedUriSchemes.Add(TEXT("rtspt"));
		// SupportedUriSchemes.Add(TEXT("rtspu"));

		// register media player info
		auto MediaModule = FModuleManager::LoadModulePtr<IMediaModule>("Media");

		if (MediaModule != nullptr)
		{
			MediaModule->RegisterPlayerFactory(*this);
		}
	}

	virtual void ShutdownModule() override
	{
		// unregister player factory
		auto MediaModule = FModuleManager::GetModulePtr<IMediaModule>("Media");
		
		if (MediaModule != nullptr)
		{
			MediaModule->UnregisterPlayerFactory(*this);
		}
	}

private:

	/** List of supported media file types. */
	TArray<FString> SupportedFileExtensions;
	
	/** List of platforms that the media player support. */
	TArray<FString> SupportedPlatforms;
	
	/** List of supported URI schemes. */
	TArray<FString> SupportedUriSchemes;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMagicLeapMediaFactoryModule, MagicLeapMediaFactory);
