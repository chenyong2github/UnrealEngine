// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Containers/List.h"
#include "Player/PlaybackTimeline.h"
#include "Player/AdaptiveStreamingPlayerABR.h"
#include "Player/PlaylistReader.h"
#include "Player/PlayerSessionServices.h"
#include "StreamTypes.h"
#include "ErrorDetail.h"
#include "Parser.h"


namespace Electra
{
class IInitSegmentCacheHLS;
class ILicenseKeyCacheHLS;

struct FPlaylistLoadRequestHLS
{
	enum class ELoadType
	{
		Master,
		Initial,
		First,
		Update
	};
	FPlaylistLoadRequestHLS() : LoadType(ELoadType::Master), InternalUniqueID(0), LastUpdateCRC32(~uint32(0)) {}
	FString			URL;
	FTimeValue		RequestedAtTime;
	ELoadType		LoadType;
	uint32			InternalUniqueID;
	uint32			LastUpdateCRC32;
	FString			AdaptationSetUniqueID;
	FString			RepresentationUniqueID;
	FString			CDN;
};


struct FManifestHLSInternal
{
	struct FMediaStream
	{
		enum class EPlaylistType
		{
			Live,
			Event,
			VOD
		};

		struct FByteRange
		{
			FByteRange() : Start(-1), End(-1)
			{
			}
			int64		Start;
			int64		End;
			bool IsSet() const
			{
				return Start >=0 && End >= 0;
			}
			int64 GetStart() const
			{
				return Start;
			}
			int64 GetEnd() const
			{
				return End;
			}
			int64 GetNumBytes() const
			{
				return IsSet() ? End - Start + 1 : 0;
			}
		};

		struct FDRMKeyInfo
		{
			enum class EMethod
			{
				None,
				AES128,
				SampleAES
			};
			FDRMKeyInfo() : Method(EMethod::None) {}
			EMethod				Method;
			FString				URI;
			FString				IV;
			//FString		Keyformat;
			//FString		KeyformatVersions;
		};

		struct FInitSegmentInfo
		{
			FString											URI;
			FByteRange										ByteRange;
			TSharedPtr<FDRMKeyInfo, ESPMode::ThreadSafe>	DRMKeyInfo;			//!< If set the init segment is encrypted with the specified parameters.
		};

		struct FMediaSegment
		{
			FString											URI;
			FByteRange										ByteRange;
			FTimeValue										Duration;
			FTimeValue										RelativeStartTime;	//!< Accumulated durations to give a total start time of this segment relative to the beginning of the playlist.
			FTimeValue										AbsoluteDateTime;	//!< If valid this gives the absolute time of the first sample
			TSharedPtr<FDRMKeyInfo, ESPMode::ThreadSafe>	DRMKeyInfo;			//!< If not set the segment is not encrypted. Otherwise it is with the specified parameters.
			TSharedPtrTS<FInitSegmentInfo>					InitSegmentInfo;	//!< If not set the segment contains the necessary init data. Otherwise this points to the init segment.
			int64											SequenceNumber;
			int64											DiscontinuityCount;
			bool											bIsPrefetch;		//!< Whether or not this is an EXT-X-PREFETCH segment
		};

		struct FStartTime
		{
			FStartTime() : bPrecise(false) {}
			FTimeValue			Offset;
			bool				bPrecise;
		};

		FMediaStream()
			: PlaylistType(EPlaylistType::Live)
			, MediaSequence(0)
			, DiscontinuitySequence(0)
			, bHasListEnd(false)
			, bIsIFramesOnly(false)
			, bHasIndependentSegments(false)
			, bHasEncryptedSegments(false)
		{
		}

	// TODO: replace with a scattered vector!!!!
		TArray<FMediaSegment>			SegmentList;
		FStartTime						StartTime;
		EPlaylistType					PlaylistType;
		FTimeValue						TargetDuration;
		int64							MediaSequence;
		int64							DiscontinuitySequence;
		bool							bHasListEnd;
		bool							bIsIFramesOnly;
		bool							bHasIndependentSegments;
		bool							bHasEncryptedSegments;

		// Internal use
		FTimeRange						SeekableRange;						//!< Current seekable range within the stream.
		FTimeRange						TimelineRange;						//!< Current timeline range of the stream.
		FTimeValue						TotalAccumulatedSegmentDuration;	//!< Accumulated duration of all segments.
		TArray<FTimespan>				SeekablePositions;					//!< List of segment absolute start times
	};


	struct FBlacklist
	{
		TSharedPtrTS<HTTP::FRetryInfo>					PreviousAttempts;
		FTimeValue										BecomesAvailableAgainAtUTC;
		IAdaptiveStreamSelector::FBlacklistedStream		AssetIDs;
	};


	struct FPlaylistBase
	{
		FPlaylistBase() = default;
		virtual ~FPlaylistBase() = default;

		virtual bool IsVariantStream() const = 0;

		virtual const FString& GetURL() const = 0;

		virtual int32 GetBitrate() const = 0;

		// Internal use.
		struct FInternal
		{
			enum class ELoadState
			{
				NotLoaded,
				Pending,
				Loaded
			};
			FInternal() : LoadState(ELoadState::NotLoaded), UniqueID(0), bReloadTriggered(false), bNewlySelected(true), bHasVideo(false), bHasAudio(false) {}
			FPlaylistLoadRequestHLS			PlaylistLoadRequest;
			TSharedPtrTS<FMediaStream>		MediaStream;
			TSharedPtrTS<FBlacklist>		Blacklisted;
			FTimeValue						ExpiresAtTime;							//!< Synchronized UTC time (from session service GetSynchronizedUTCTime()) at which this list expires. Set to infinite if it does not.
			ELoadState						LoadState;
			uint32							UniqueID;
			bool							bReloadTriggered;
			bool							bNewlySelected;
			FString							AdaptationSetUniqueID;
			FString							RepresentationUniqueID;
			FString							CDN;
			bool							bHasVideo;
			bool							bHasAudio;
		};
		FInternal			Internal;
	};

	// 4.3.4.1. EXT-X-MEDIA
	struct FRendition : public FPlaylistBase
	{
		FRendition()
			: FPlaylistBase(), bDefault(false), bAutoSelect(false), bForced(false)
		{
		}
		virtual bool IsVariantStream() const override
		{
			return false;
		}
		virtual const FString& GetURL() const override
		{
			return URI;
		}
		virtual int32 GetBitrate() const override
		{
			return 0;
		}

		FString		Type;
		FString		GroupID;
		FString		URI;
		FString		Language;
		FString		AssocLanguage;
		FString		Name;
		FString		InStreamID;
		FString		Characteristics;
		FString		Channels;
		bool		bDefault;
		bool		bAutoSelect;
		bool		bForced;
	};

	// 4.3.4.2. EXT-X-STREAM-INF
	struct FVariantStream : public FPlaylistBase
	{
		FVariantStream()
			: FPlaylistBase(), Bandwidth(0), AverageBandwidth(0), HDCPLevel(0)
		{
		}
		virtual bool IsVariantStream() const override
		{
			return true;
		}
		virtual const FString& GetURL() const override
		{
			return URI;
		}
		virtual int32 GetBitrate() const override
		{
			return Bandwidth;
		}

		TArray<FStreamCodecInformation>			StreamCodecInformationList;
		FString									URI;
		FString									VideoGroupID;
		FString									AudioGroupID;
		FString									SubtitleGroupID;
		FString									ClosedCaptionGroupID;
		int32									Bandwidth;
		int32									AverageBandwidth;
		int32									HDCPLevel;
	};


	// Internal use
	struct FMasterPlaylistVars
	{
		FMasterPlaylistVars() : PresentationType(IManifest::EType::Live) {}
		FPlaylistLoadRequestHLS								PlaylistLoadRequest;				//!< The master playlist load request. This holds the URL for resolving child playlists.
		IManifest::EType									PresentationType;					//!< Type of the presentation (Live or VOD).
		FTimeValue											PresentationDuration;				//!< Current duration of the presentation. Will be reset when a playlist is refreshed.
		FTimeRange											SeekableRange;						//!< Current seekable range within the presentation. Will be updated when a playlist is refreshed.
		FTimeRange											TimelineRange;						//!< Current media timeline range of the presentation. Will be updated when a playlist is refreshed.
		TArray<FTimespan>									SeekablePositions;					//!< Segment start times.
	};

	FManifestHLSInternal()
		: bHasIndependentSegments(false)
	{
	}


	TMultiMap<FString, TSharedPtrTS<FRendition>>			VideoRenditions;
	TMultiMap<FString, TSharedPtrTS<FRendition>>			AudioRenditions;
	TMultiMap<FString, TSharedPtrTS<FRendition>>			SubtitleRenditions;
	TMultiMap<FString, TSharedPtrTS<FRendition>>			ClosedCaptionRenditions;
	// TODO: if provided we can put the EXT-X-START here
	// TODO: For future DRM support we can put the EXT-X-SESSION-KEY here.

	TArray<TSharedPtrTS<FVariantStream>>							VariantStreams;
	TArray<TSharedPtrTS<FVariantStream>>							AudioOnlyStreams;				//!< Variants that are audio-only (have only audio codec specified in master playlist)
	TArray<TSharedPtrTS<FVariantStream>>							IFrameOnlyStreams;

	TMap<uint32, TWeakPtrTS<FPlaylistBase>>							PlaylistIDMap;
	TMap<int32, int32>												BandwidthToQualityIndex;

	bool															bHasIndependentSegments;

	FMasterPlaylistVars												MasterPlaylistVars;


	FMediaCriticalSection											VariantPlaylistAccessMutex;			//!< This mutex is used to access any(!) of the variant and rendition playlists. The master playlist is immutable.
	TSet<uint32>													ActivelyReferencedStreamIDs;		//!< Unique IDs of streams from which segments are being fetched.
	TSharedPtrTS<IInitSegmentCacheHLS>								InitSegmentCache;
	TSharedPtrTS<ILicenseKeyCacheHLS>								LicenseKeyCache;

	TArray<FStreamMetadata>											StreamMetadataVideo;
	TArray<FStreamMetadata>											StreamMetadataAudio;

	TSharedPtrTS<IPlaybackAssetTimeline>							PlaybackTimeline;


	TSharedPtrTS<FPlaylistBase> GetPlaylistForUniqueID(uint32 UniqueID) const
	{
		const TWeakPtrTS<FPlaylistBase>* PlaylistID = PlaylistIDMap.Find(UniqueID);
		if (PlaylistID != nullptr)
		{
			return PlaylistID->Pin();
		}
		return TSharedPtrTS<FManifestHLSInternal::FPlaylistBase>();
	}


	void SelectActiveStreamID(uint32 NewActiveStreamID, uint32 NowInactiveStreamID)
	{
		VariantPlaylistAccessMutex.Lock();
		ActivelyReferencedStreamIDs.Remove(NowInactiveStreamID);

		// Add to selected list only if not deselected (the new stream ID is not 0)
		if (NewActiveStreamID)
		{
			ActivelyReferencedStreamIDs.Add(NewActiveStreamID);

			TSharedPtrTS<FPlaylistBase> Playlist = PlaylistIDMap[NewActiveStreamID].Pin();
			if (Playlist.IsValid())
			{
				Playlist->Internal.bNewlySelected = true;
			}
		}
		VariantPlaylistAccessMutex.Unlock();
	}

	void GetActiveStreams(TArray<TSharedPtrTS<FPlaylistBase>>& OutActiveStreams)
	{
		VariantPlaylistAccessMutex.Lock();
		for(TSet<uint32>::TConstIterator StreamID=ActivelyReferencedStreamIDs.CreateConstIterator(); StreamID; ++StreamID)
		{
			TSharedPtrTS<FPlaylistBase> Playlist = PlaylistIDMap[*StreamID].Pin();
			if (Playlist.IsValid())
			{
				OutActiveStreams.Push(Playlist);
			}
		}
		VariantPlaylistAccessMutex.Unlock();
	}



	void LockPlaylists()
	{
		VariantPlaylistAccessMutex.Lock();
	}
	void UnlockPlaylists()
	{
		VariantPlaylistAccessMutex.Unlock();
	}
	struct ScopedLockPlaylists
	{
		ScopedLockPlaylists(TSharedPtrTS<FManifestHLSInternal> InManifest)
			: Manifest(InManifest)
		{
			if (Manifest.IsValid())
			{
				Manifest->LockPlaylists();
			}
		}
		~ScopedLockPlaylists()
		{
			if (Manifest.IsValid())
			{
				Manifest->UnlockPlaylists();
			}
		}
	private:
		TSharedPtrTS<FManifestHLSInternal> Manifest;
	};
};






class IManifestBuilderHLS
{
public:
	static IManifestBuilderHLS* Create(IPlayerSessionServices* PlayerSessionServices);

	virtual ~IManifestBuilderHLS() = default;


	/**
	 * Builds a new internal manifest from a HLS master playlist.
	 *
	 * @param OutHLSPlaylist
	 * @param Playlist
	 * @param SourceRequest
	 * @param ConnectionInfo
	 * @param Preferences
	 * @param Options
	 *
	 * @return
	 */
	virtual FErrorDetail BuildFromMasterPlaylist(TSharedPtrTS<FManifestHLSInternal>& OutHLSPlaylist, const HLSPlaylistParser::FPlaylist& Playlist, const FPlaylistLoadRequestHLS& SourceRequest, const HTTP::FConnectionInfo* ConnectionInfo, const FStreamPreferences& Preferences, const FParamDict& Options) = 0;


	/**
	 * Returns the list of variant and rendition playlists that must be fetched on play start.
	 *
	 * @param OutRequests
	 * @param Manifest
	 * @param Preferences
	 * @param Options
	 *
	 * @return
	 */
	virtual FErrorDetail GetInitialPlaylistLoadRequests(TArray<FPlaylistLoadRequestHLS>& OutRequests, TSharedPtrTS<FManifestHLSInternal> Manifest, const FStreamPreferences& Preferences, const FParamDict& Options) = 0;

	/**
	 * Updates an initial playlist load request that failed to load or parse with an
	 * alternative request from a lower bitrate if possible.
	 *
	 * @param InOutFailedRequest
	 *                 Failed request for which an alterative shall be returned.
	 * @param ConnectionInfo
	 * @param PreviousAttempts
	 * @param BlacklistUntilUTC
	 * @param Manifest
	 * @param Preferences
	 * @param Options
	 *
	 * @return UEMEDIA_ERROR_OK if an alternative was found or UEMEDIA_ERROR_END_OF_STREAM if no further alternatives exist.
	 */
	virtual UEMediaError UpdateFailedInitialPlaylistLoadRequest(FPlaylistLoadRequestHLS& InOutFailedRequest, const HTTP::FConnectionInfo* ConnectionInfo, TSharedPtrTS<HTTP::FRetryInfo> PreviousAttempts, const FTimeValue& BlacklistUntilUTC, TSharedPtrTS<FManifestHLSInternal> Manifest, const FStreamPreferences& Preferences, const FParamDict& Options) = 0;


	/**
	 * Updates the internal manifest with a new/refreshed variant playlist.
	 *
	 * @param InOutHLSPlaylist
	 * @param VariantPlaylist
	 * @param SourceRequest
	 * @param ConnectionInfo
	 * @param ResponseCRC
	 * @param Preferences
	 * @param Options
	 *
	 * @return
	 */
	virtual FErrorDetail UpdateFromVariantPlaylist(TSharedPtrTS<FManifestHLSInternal> InOutHLSPlaylist, const HLSPlaylistParser::FPlaylist& VariantPlaylist, const FPlaylistLoadRequestHLS& SourceRequest, const HTTP::FConnectionInfo* ConnectionInfo, uint32 ResponseCRC, const FStreamPreferences& Preferences, const FParamDict& Options) = 0;


	/**
	 * Marks a variant as failed.
	 *
	 * @param InHLSPlaylist
	 * @param SourceRequest
	 * @param ConnectionInfo
	 * @param PreviousAttempts
	 * @param BlacklistUntilUTC
	 */
	virtual void SetVariantPlaylistFailure(TSharedPtrTS<FManifestHLSInternal> InHLSPlaylist, const FPlaylistLoadRequestHLS& SourceRequest, const HTTP::FConnectionInfo* ConnectionInfo, TSharedPtrTS<HTTP::FRetryInfo> PreviousAttempts, const FTimeValue& BlacklistUntilUTC) = 0;
};


} // namespace Electra




