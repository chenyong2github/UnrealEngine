// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "PlayerCore.h"
#include "PlayerTime.h"
#include "StreamTypes.h"
#include "HTTP/HTTPManager.h"
#include "Player/PlaybackTimeline.h"
#include "Player/AdaptiveStreamingPlayerMetrics.h"
#include "Player/PlayerSessionServices.h"
#include "ParameterDictionary.h"



//
//
//


namespace Electra
{

	class IStreamReader;


	struct FPlayStartPosition
	{
		FTimeValue		Time;
	};


	class IStreamSegment : private TMediaNoncopyable<IStreamSegment>, public TSharedFromThis<IStreamSegment, ESPMode::ThreadSafe>
	{
	public:
		virtual ~IStreamSegment() = default;

		/**
		 * Sets a playback sequence ID. This is useful to check the ID later in asynchronously received messages
		 * to validate the request still being valid or having become outdated.
		 */
		virtual void SetPlaybackSequenceID(uint32 PlaybackSequenceID) = 0;
		/**
		 * Returns the playback sequence ID. See SetPlaybackSequenceID()
		 */
		virtual uint32 GetPlaybackSequenceID() const = 0;


		virtual EStreamType GetType() const = 0;

		struct FDependentStreams
		{
			EStreamType		StreamType;
		};
		/**
		 * Returns a list of dependent stream types.
		 */
		virtual void GetDependentStreams(TArray<FDependentStreams>& OutDependentStreams) const = 0;

		/**
		 * Returns a list of streams, primary or dependent, that are already at EOS in this request.
		 */
		virtual void GetEndedStreams(TArray<TSharedPtrTS<IStreamSegment>>& OutAlreadyEndedStreams) = 0;

		//! Returns the first PTS value as indicated by the media timeline. This should correspond to the actual absolute PTS of the sample.
		virtual FTimeValue GetFirstPTS() const = 0;

		virtual int32 GetQualityIndex() const = 0;
		virtual int32 GetBitrate() const = 0;

		virtual void GetDownloadStats(Metrics::FSegmentDownloadStats& OutStats) const = 0;
	};



	class IManifest : public TMediaNoncopyable<IManifest>
	{
	public:
		enum class EType
		{
			OnDemand,						//!< An on-demand presentation
			Live,							//!< A live presentation
		};

		enum class ESearchType
		{
			Closest,				//!< Find closest match
			After,					//!< Find match only for fragment times >= target time
			Before,					//!< Find match only for fragment times <= target time
			StrictlyAfter,			//!< Match must be strictly after (>). Used to locate the next segment.
			StrictlyBefore,			//!< Match must be strictly before (<). Used to locate the previous segment.
			Same,					//!< Match must be for the same fragment
		};

		class FResult
		{
		public:
			enum class EType
			{
				Found,				//!< Found
				NotFound,			//!< Not found
				PastEOS,			//!< Time is beyond the duration
				BeforeStart,		//!< Time is before the start time
				TryAgainLater,		//!< Not found at the moment. Playlist load may be pending
				NotLoaded,			//!< Not loaded (playlist has not been requested)
			};
			FResult(EType InType = EType::NotFound)
				: Type(InType)
			{
			}
			FResult& RetryAfterMilliseconds(int32 Milliseconds)
			{
				Type = EType::TryAgainLater;
				RetryAgainAtTime = MEDIAutcTime::Current() + FTimeValue().SetFromMilliseconds(Milliseconds);
				return *this;
			}
			FResult& SetErrorDetail(const FErrorDetail& InErrorDetail)
			{
				ErrorDetail = InErrorDetail;
				return *this;
			}
			EType GetType() const
			{
				return Type;
			}
			bool IsSuccess() const
			{
				return GetType() == EType::Found;
			}
			const FTimeValue& GetRetryAgainAtTime() const
			{
				return RetryAgainAtTime;
			}
			const FErrorDetail& GetErrorDetail() const
			{
				return ErrorDetail;
			}
			static const TCHAR* GetTypeName(EType s)
			{
				switch (s)
				{
					case EType::Found:
						return TEXT("Found");
					case EType::NotFound:
						return TEXT("Not found");
					case EType::PastEOS:
						return TEXT("Behind EOF");
					case EType::BeforeStart:
						return TEXT("Before start");
					case EType::TryAgainLater:
						return TEXT("Try again later");
					case EType::NotLoaded:
						return TEXT("Not loaded");
					default:
						return TEXT("undefined");
				}
			}
		protected:
			EType			Type;
			FTimeValue		RetryAgainAtTime;
			FErrorDetail	ErrorDetail;
		};

		//!
		virtual ~IManifest() = default;



		//-------------------------------------------------------------------------
		// Presentation related functions
		//
		//! Returns the type of this presentation, either on-demand or live.
		virtual EType GetPresentationType() const = 0;



		//-------------------------------------------------------------------------
		// Presentation and play time related functions.
		//
		//! Returns the current timeline object.
		virtual TSharedPtrTS<IPlaybackAssetTimeline> GetTimeline() const = 0;

		//! Returns the bitrate of the default stream (usually the first one specified).
		virtual int64 GetDefaultStartingBitrate() const = 0;

		//! Returns stream metadata. For period based presentations the streams can be different per period in which case the metadata of the first period is returned.
		virtual void GetStreamMetadata(TArray<FStreamMetadata>& OutMetadata, EStreamType StreamType) const = 0;


		//
		virtual FTimeValue GetMinBufferTime() const = 0;



		//-------------------------------------------------------------------------
		// Stream fragment reader
		//
		//! Create a stream reader handler suitable for streams from this manifest.
		virtual IStreamReader* CreateStreamReaderHandler() = 0;



		/**
		 * Interface to a playback period.
		 */
		class IPlayPeriod : private TMediaNoncopyable<IPlayPeriod>
		{
		public:
			virtual ~IPlayPeriod() = default;

			virtual void SetStreamPreferences(const FStreamPreferences& Preferences) = 0;

			enum class EReadyState
			{
				NotReady,
				Preparing,
				IsReady,
				MustReselect						//!< Player must forget this Play Period and reselect it through FindPlayPeriod(). Needed if the timeline got altered.
			};
			virtual EReadyState GetReadyState() = 0;
			virtual void PrepareForPlay(const FParamDict& Options) = 0;

			/**
			 * Returns the media asset for this play period.
			 *
			 * @return Pointer to the media asset.
			 */
			virtual TSharedPtrTS<ITimelineMediaAsset> GetMediaAsset() const = 0;

			/**
			 * Selects a specific stream to be used for all next segment downloads.
			 *
			 * @param AdaptationSet
			 * @param Representation
			 * @param PreferredCDN
			 *               Preferred CDN to get the segment from (retrieve through GetMediaAsset() for now)
			 */
			virtual void SelectStream(const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation, const FString& PreferredCDN) = 0;

			/**
			 * Sets up a starting segment request to begin playback at the specified time.
			 * The streams selected through SelectStream() will be used.
			 *
			 * @param OutSegment
			 * @param StartPosition
			 * @param SearchType
			 *
			 * @return
			 */
			virtual FResult GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, ESearchType SearchType) = 0;


			/**
			 * Sets up a starting segment request to loop playback to.
			 * The streams selected through SelectStream() will be used.
			 *
			 * @param OutSegment
			 * @param InOutLoopState
			 * @param InFinishedSegments
			 * @param StartPosition
			 * @param SearchType
			 *
			 * @return
			 */
			virtual FResult GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, ESearchType SearchType) = 0;

			/**
			 * Gets the segment request for the segment following the specified earlier request.
			 *
			 * @param OutSegment
			 * @param CurrentSegment
			 * @param Options
			 *
			 * @return
			 */
			virtual FResult GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FParamDict& Options) = 0;

			/**
			 * Gets the segment request for the same segment on a different quality level or CDN.
			 *
			 * @param OutSegment
			 * @param CurrentSegment
			 * @param Options
			 *
			 * @return
			 */
			virtual FResult GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FParamDict& Options) = 0;

			struct FSegmentInformation
			{
				FTimeValue	Duration;
				int64		ByteSize;
			};

			/**
			 * Obtains information on the stream segmentation of a particular stream starting at a given current reference segment (optional, if not given returns suitable default values).
			 *
			 * @param OutSegmentInformation
			 * @param OutAverageSegmentDuration
			 * @param CurrentSegment
			 * @param LookAheadTime
			 * @param AdaptationSet
			 * @param Representation
			 */
			virtual void GetSegmentInformation(TArray<FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation) = 0;
		};

		virtual FResult FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType) = 0;
	};



} // namespace Electra


