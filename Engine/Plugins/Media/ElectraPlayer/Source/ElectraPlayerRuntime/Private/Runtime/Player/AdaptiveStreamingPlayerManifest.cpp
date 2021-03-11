// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Misc/Paths.h"
#include "Player/AdaptiveStreamingPlayerInternal.h"
#include "Player/HLS/PlaylistReaderHLS.h"
#include "Player/mp4/PlaylistReaderMP4.h"
#include "Player/DASH/PlaylistReaderDASH.h"
#include "Utilities/Utilities.h"
#include "Utilities/StringHelpers.h"
#include "Utilities/URLParser.h"
#include "ParameterDictionary.h"



namespace Electra
{

namespace Playlist
{
static const FString MIMETypeMP4(TEXT("video/mp4"));
static const FString MIMETypeMP4A(TEXT("audio/mp4"));
static const FString MIMETypeHLS(TEXT("application/vnd.apple.mpegURL"));
static const FString MIMETypeDASH(TEXT("application/dash+xml"));


/**
 * Returns the MIME type of the media playlist pointed to by the given URL.
 * This only parses the URL for known extensions or scheme.
 * If the MIME type cannot be precisely determined an empty string is returned.
 *
 * @param URL    Media playlist URL
 *
 * @return MIME type or empty string if it cannot be determined
 */
FString GetMIMETypeForURL(const FString &URL)
{
	FString MimeType;
	TUniquePtr<IURLParser> UrlParser(IURLParser::Create());
	if (!UrlParser)
	{
		return MimeType;
	}
	UEMediaError Error = UrlParser->ParseURL(URL);
	if (Error != UEMEDIA_ERROR_OK)
	{
		return MimeType;
	}

	//	FString PathOnly = UrlParser->GetPath();
	TArray<FString> PathComponents;
	UrlParser->GetPathComponents(PathComponents);
	if (PathComponents.Num())
	{
		FString LowerCaseExtension = FPaths::GetExtension(PathComponents.Last().ToLower());

		// Check for known extensions.
		static const FString kTextMP4(TEXT("mp4"));
		static const FString kTextMP4V(TEXT("m4v"));
		static const FString kTextMP4A(TEXT("m4a"));
		static const FString kTextMPD(TEXT("mpd"));
		static const FString kTextM3U8(TEXT("m3u8"));
		if (LowerCaseExtension == kTextMP4 || LowerCaseExtension == kTextMP4V)
		{
			MimeType = MIMETypeMP4;
		}
		else if (LowerCaseExtension == kTextMP4A)
		{
			MimeType = MIMETypeMP4A;
		}
		else if (LowerCaseExtension == kTextMPD)
		{
			MimeType = MIMETypeDASH;
		}
		else if (LowerCaseExtension == kTextM3U8)
		{
			MimeType = MIMETypeHLS;
		}
	}

	return MimeType;
}


} // namespace Playlist









//-----------------------------------------------------------------------------
/**
 * Starts asynchronous loading and parsing of a manifest.
 *
 * @param URL
 * @param MimeType
 */
void FAdaptiveStreamingPlayer::InternalLoadManifest(const FString& URL, const FString& MimeType)
{
	if (CurrentState == EPlayerState::eState_Idle)
	{
		FString mimeType = Playlist::GetMIMETypeForURL(URL);
		if (mimeType.Len())
		{
			CurrentState = EPlayerState::eState_ParsingManifest;

			if (mimeType == Playlist::MIMETypeHLS)
			{
				ManifestReader = IPlaylistReaderHLS::Create(this);
				DispatchEvent(FMetricEvent::ReportOpenSource(URL));
				ManifestReader->LoadAndParse(URL);
			}
			else if (mimeType == Playlist::MIMETypeMP4)
			{
				ManifestReader = IPlaylistReaderMP4::Create(this);
				DispatchEvent(FMetricEvent::ReportOpenSource(URL));
				ManifestReader->LoadAndParse(URL);
			}
			else if (mimeType == Playlist::MIMETypeDASH)
			{
				ManifestReader = IPlaylistReaderDASH::Create(this);
				DispatchEvent(FMetricEvent::ReportOpenSource(URL));
				ManifestReader->LoadAndParse(URL);
			}
			else
			{
				FErrorDetail err;
				err.SetFacility(Facility::EFacility::Player);
				err.SetMessage(TEXT("Unsupported stream MIME type"));
				err.SetCode(INTERR_UNSUPPORTED_FORMAT);
				PostError(err);
			}
		}
		else
		{
			FErrorDetail err;
			err.SetFacility(Facility::EFacility::Player);
			err.SetMessage(TEXT("Could not determine stream MIME type"));
			err.SetCode(INTERR_UNSUPPORTED_FORMAT);
			PostError(err);
		}
	}
	else
	{
// TODO: Error: Not idle
	}
}


//-----------------------------------------------------------------------------
/**
 * Selects the internal presentation for playback after having selected/disabled candidate streams via AccessManifest().
 *
 * @return
 */
bool FAdaptiveStreamingPlayer::SelectManifest()
{
	if (ManifestReader)
	{
		check(Manifest == nullptr);
		if (ManifestReader->GetPlaylistType() == TEXT("hls") ||
			ManifestReader->GetPlaylistType() == TEXT("mp4") ||
			ManifestReader->GetPlaylistType() == TEXT("dash"))
		{
			TArray<FTimespan> SeekablePositions;
			TSharedPtrTS<IManifest> NewPresentation = ManifestReader->GetManifest();
			check(NewPresentation.IsValid());

			PlaybackState.SetSeekableRange(NewPresentation->GetSeekableTimeRange());
			NewPresentation->GetSeekablePositions(SeekablePositions);
			PlaybackState.SetSeekablePositions(SeekablePositions);
			PlaybackState.SetTimelineRange(NewPresentation->GetTotalTimeRange());
			PlaybackState.SetDuration(NewPresentation->GetDuration());

			TArray<FStreamMetadata> VideoStreamMetadata;
			TArray<FStreamMetadata> AudioStreamMetadata;
			NewPresentation->GetStreamMetadata(VideoStreamMetadata, EStreamType::Video);
			NewPresentation->GetStreamMetadata(AudioStreamMetadata, EStreamType::Audio);
			PlaybackState.SetStreamMetadata(VideoStreamMetadata, AudioStreamMetadata);
			PlaybackState.SetHaveMetadata(true);

			Manifest = NewPresentation;

			CurrentState = EPlayerState::eState_Ready;

			double minBufTimeMPD = Manifest->GetMinBufferTime().GetAsSeconds();
			PlayerConfig.InitialBufferMinTimeAvailBeforePlayback = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDInitialBufferMinTimeBeforePlayback,   PlayerConfig.InitialBufferMinTimeAvailBeforePlayback);
			PlayerConfig.SeekBufferMinTimeAvailBeforePlayback    = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDSeekBufferMinTimeAvailBeforePlayback, PlayerConfig.SeekBufferMinTimeAvailBeforePlayback);
			PlayerConfig.RebufferMinTimeAvailBeforePlayback 	 = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDRebufferMinTimeAvailBeforePlayback,   PlayerConfig.RebufferMinTimeAvailBeforePlayback);

			return true;
		}
		else
		{
			// Handle other types of playlist here.
			FErrorDetail err;
			err.SetFacility(Facility::EFacility::Player);
			err.SetMessage(TEXT("Unsupported playlist/manifest type"));
			err.SetCode(INTERR_UNSUPPORTED_FORMAT);
			PostError(err);
		}
	}

	return false;
}

void FAdaptiveStreamingPlayer::UpdateManifest()
{
	check(Manifest.IsValid());

	TArray<FTimespan> SeekablePositions;

	PlaybackState.SetSeekableRange(Manifest->GetSeekableTimeRange());
	Manifest->GetSeekablePositions(SeekablePositions);
	PlaybackState.SetSeekablePositions(SeekablePositions);
	PlaybackState.SetTimelineRange(Manifest->GetTotalTimeRange());
	PlaybackState.SetDuration(Manifest->GetDuration());

	TArray<FStreamMetadata> VideoStreamMetadata;
	TArray<FStreamMetadata> AudioStreamMetadata;
	Manifest->GetStreamMetadata(VideoStreamMetadata, EStreamType::Video);
	Manifest->GetStreamMetadata(AudioStreamMetadata, EStreamType::Audio);
	PlaybackState.SetStreamMetadata(VideoStreamMetadata, AudioStreamMetadata);
	PlaybackState.SetHaveMetadata(true);
}

} // namespace Electra


