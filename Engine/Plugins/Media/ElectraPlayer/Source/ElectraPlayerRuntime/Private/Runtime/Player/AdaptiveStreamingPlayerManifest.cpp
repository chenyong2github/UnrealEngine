// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Misc/Paths.h"
#include "Player/AdaptiveStreamingPlayerInternal.h"
#include "Player/HLS/PlaylistReaderHLS.h"
#include "Player/mp4/PlaylistReaderMP4.h"
#include "Utilities/Utilities.h"
#include "Utilities/StringHelpers.h"
#include "Utilities/URLParser.h"
#include "ParameterDictionary.h"



namespace Electra
{

namespace Playlist
{
static const FString MIMETypeMP4(TEXT("video/mp4"));
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

		// Check for an ".mp4" extension.

		static const FString kTextMP4(TEXT("mp4"));
		static const FString kTextMPD(TEXT("mpd"));
		static const FString kTextM3U8(TEXT("m3u8"));
		if (LowerCaseExtension == kTextMP4)
		{
			MimeType = MIMETypeMP4;
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

			check(ManifestReader == nullptr);
			if (mimeType == Playlist::MIMETypeHLS)
			{
				check(ManifestReader == nullptr);
				ManifestReader = IPlaylistReaderHLS::Create(this);
				DispatchEvent(FMetricEvent::ReportOpenSource(URL));
				ManifestReader->LoadAndParse(URL, StreamPreferences, PlayerOptions);
			}
			else if (mimeType == Playlist::MIMETypeMP4)
			{
				check(ManifestReader == nullptr);
				ManifestReader = IPlaylistReaderMP4::Create(this);
				DispatchEvent(FMetricEvent::ReportOpenSource(URL));
				ManifestReader->LoadAndParse(URL, StreamPreferences, PlayerOptions);
			}
			else
			{
				FErrorDetail err;
				err.SetFacility(Facility::EFacility::Player);
				err.SetMessage("Unsupported stream MIME type");
				err.SetCode(INTERR_UNSUPPORTED_FORMAT);
				PostError(err);
			}
		}
		else
		{
			FErrorDetail err;
			err.SetFacility(Facility::EFacility::Player);
			err.SetMessage("Could not determine stream MIME type");
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
		if (ManifestReader->GetPlaylistType() == "hls")
		{
			TArray<FTimespan> SeekablePositions;
			TSharedPtrTS<IManifest> pPresentation = ManifestReader->GetManifest();
			check(pPresentation.IsValid());

			CurrentTimeline = pPresentation->GetTimeline();
			PlaybackState.SetSeekableRange(CurrentTimeline->GetSeekableTimeRange());
			CurrentTimeline->GetSeekablePositions(SeekablePositions);
			PlaybackState.SetSeekablePositions(SeekablePositions);
			PlaybackState.SetTimelineRange(CurrentTimeline->GetTotalTimeRange());
			PlaybackState.SetDuration(CurrentTimeline->GetDuration());

			TArray<FStreamMetadata> VideoStreamMetadata;
			TArray<FStreamMetadata> AudioStreamMetadata;
			pPresentation->GetStreamMetadata(VideoStreamMetadata, EStreamType::Video);
			pPresentation->GetStreamMetadata(AudioStreamMetadata, EStreamType::Audio);
			PlaybackState.SetStreamMetadata(VideoStreamMetadata, AudioStreamMetadata);
			PlaybackState.SetHaveMetadata(true);

			Manifest = pPresentation;

			CurrentState = EPlayerState::eState_Ready;

			double minBufTimeMPD = Manifest->GetMinBufferTime().GetAsSeconds();
			PlayerConfig.InitialBufferMinTimeAvailBeforePlayback = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDInitialBufferMinTimeBeforePlayback,   PlayerConfig.InitialBufferMinTimeAvailBeforePlayback);
			PlayerConfig.SeekBufferMinTimeAvailBeforePlayback    = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDSeekBufferMinTimeAvailBeforePlayback, PlayerConfig.SeekBufferMinTimeAvailBeforePlayback);
			PlayerConfig.RebufferMinTimeAvailBeforePlayback 	 = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDRebufferMinTimeAvailBeforePlayback,   PlayerConfig.RebufferMinTimeAvailBeforePlayback);

			return true;
		}
		else if (ManifestReader->GetPlaylistType() == "mp4")
		{
			TArray<FTimespan> SeekablePositions;
			TSharedPtrTS<IManifest> pPresentation = ManifestReader->GetManifest();
			check(pPresentation.IsValid());

			CurrentTimeline = pPresentation->GetTimeline();
			PlaybackState.SetSeekableRange(CurrentTimeline->GetSeekableTimeRange());
			CurrentTimeline->GetSeekablePositions(SeekablePositions);
			PlaybackState.SetSeekablePositions(SeekablePositions);
			PlaybackState.SetTimelineRange(CurrentTimeline->GetTotalTimeRange());
			PlaybackState.SetDuration(CurrentTimeline->GetDuration());

			TArray<FStreamMetadata> VideoStreamMetadata;
			TArray<FStreamMetadata> AudioStreamMetadata;
			pPresentation->GetStreamMetadata(VideoStreamMetadata, EStreamType::Video);
			pPresentation->GetStreamMetadata(AudioStreamMetadata, EStreamType::Audio);
			PlaybackState.SetStreamMetadata(VideoStreamMetadata, AudioStreamMetadata);
			PlaybackState.SetHaveMetadata(true);

			Manifest = pPresentation;

			CurrentState = EPlayerState::eState_Ready;

			double minBufTimeMPD = Manifest->GetMinBufferTime().GetAsSeconds();
			PlayerConfig.InitialBufferMinTimeAvailBeforePlayback = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDInitialBufferMinTimeBeforePlayback,   PlayerConfig.InitialBufferMinTimeAvailBeforePlayback);
			PlayerConfig.SeekBufferMinTimeAvailBeforePlayback    = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDSeekBufferMinTimeAvailBeforePlayback, PlayerConfig.SeekBufferMinTimeAvailBeforePlayback);
			PlayerConfig.RebufferMinTimeAvailBeforePlayback 	 = Utils::Min(minBufTimeMPD * PlayerConfig.ScaleMPDRebufferMinTimeAvailBeforePlayback,   PlayerConfig.RebufferMinTimeAvailBeforePlayback);

			return true;
		}
		// Handle other types of playlist here.
		check(!"TODO");
	}

	return false;
}

void FAdaptiveStreamingPlayer::UpdateManifest()
{
	check(Manifest != nullptr);

	TArray<FTimespan> SeekablePositions;

	// Update the current timeline.
	CurrentTimeline = Manifest->GetTimeline();
	PlaybackState.SetSeekableRange(CurrentTimeline->GetSeekableTimeRange());
	CurrentTimeline->GetSeekablePositions(SeekablePositions);
	PlaybackState.SetSeekablePositions(SeekablePositions);
	PlaybackState.SetTimelineRange(CurrentTimeline->GetTotalTimeRange());
	PlaybackState.SetDuration(CurrentTimeline->GetDuration());

	TArray<FStreamMetadata> VideoStreamMetadata;
	TArray<FStreamMetadata> AudioStreamMetadata;
	Manifest->GetStreamMetadata(VideoStreamMetadata, EStreamType::Video);
	Manifest->GetStreamMetadata(AudioStreamMetadata, EStreamType::Audio);
	PlaybackState.SetStreamMetadata(VideoStreamMetadata, AudioStreamMetadata);
	PlaybackState.SetHaveMetadata(true);
}

} // namespace Electra


