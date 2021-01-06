// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "PlayerCore.h"
#include "PlayerTime.h"
#include "StreamTypes.h"


namespace Electra
{
	class IPlaybackAssetRepresentation
	{
	public:
		virtual ~IPlaybackAssetRepresentation() = default;

		virtual FString GetUniqueIdentifier() const = 0;

		virtual const FStreamCodecInformation& GetCodecInformation() const = 0;

		virtual int32 GetBitrate() const = 0;

		virtual FString GetCDN() const = 0;
	};

	class IPlaybackAssetAdaptationSet
	{
	public:
		virtual ~IPlaybackAssetAdaptationSet() = default;

		/**
		 * Returns a unique identifier for this adaptation set.
		 * This may be a value from the manifest or an internally generated one.
		 * NOTE: The identifier is unique only within the owning media asset!
		 *
		 * @return Unique identifier for this adaptation set with the owning media asset.
		 */
		virtual FString GetUniqueIdentifier() const = 0;

		virtual FString GetListOfCodecs() const = 0;

		virtual FString GetLanguage() const = 0;

		virtual int32 GetNumberOfRepresentations() const = 0;

		virtual TSharedPtrTS<IPlaybackAssetRepresentation> GetRepresentationByIndex(int32 RepresentationIndex) const = 0;

		virtual TSharedPtrTS<IPlaybackAssetRepresentation> GetRepresentationByUniqueIdentifier(const FString& UniqueIdentifier) const = 0;
	};


	class ITimelineMediaAsset
	{
	public:
		virtual ~ITimelineMediaAsset() = default;

		/**
		 * Returns the time range of this asset on the playback timeline.
		 * The timeline anchor time is included in this range.
		 *
		 * @return Time range of this asset on the playback timeline.
		 */
		virtual FTimeRange GetTimeRange() const = 0;

		/**
		 * Returns the duration of this asset, which is typically the difference between
		 * the end and start value of GetTimeRange() unless it is last asset of a Live
		 * presentation timeline for which the duration is infinite.
		 *
		 * @return Duration of this asset.
		 */
		virtual FTimeValue GetDuration() const = 0;

		/**
		 * Returns the asset identifier, if present in the manifest, for this asset.
		 *
		 * @return Asset identifier as it appears in the manifest.
		 */
		virtual FString GetAssetIdentifier() const = 0;


		/**
		 * Returns a unique identifier for this asset.
		 * This may be a value from the manifest or an internally generated one.
		 *
		 * @return Unique identifier for this asset.
		 */
		virtual FString GetUniqueIdentifier() const = 0;


		/**
		 * Returns the number of "adaptation sets" for a particular type of stream.
		 * An adaptation set is defined as a group of streams representing the same content at different
		 * quality levels to dynamically switch between.
		 * Streams are further grouped by language and codec (there will not be a mix of either in a
		 * single adaptation set).
		 *
		 * @param OfStreamType
		 *               Type of elementary stream for which to the the number of available adaptation sets.
		 *
		 * @return Number of adaptation sets for the specified type.
		 */
		virtual int32 GetNumberOfAdaptationSets(EStreamType OfStreamType) const = 0;

		/**
		 * Returns the media asset's adaptation set by index.
		 *
		 * @param OfStreamType
		 *               Type of elementary stream for which to get the adaptation set.
		 * @param AdaptationSetIndex
		 *               Index of the adaptation set to return.
		 *
		 * @return Shared pointer to the requested adaptation set.
		 */
		virtual TSharedPtrTS<IPlaybackAssetAdaptationSet> GetAdaptationSetByTypeAndIndex(EStreamType OfStreamType, int32 AdaptationSetIndex) const = 0;
	};


	/**
	 * Interface class to the media playback asset timeline.
	 * This is a snapshot of the current timeline which may change periodically.
	 */
	class IPlaybackAssetTimeline
	{
	public:
		virtual ~IPlaybackAssetTimeline() = default;

		/**
		 * Returns the time value the timeline is anchored at.
		 * Usually this is a UTC timestamp at which the presentation began but it could
		 * literally be anything.
		 * All time values are absolute and have this anchor added in.
		 *
		 * @return The time this presentation timeline is anchored at.
		 */
		virtual FTimeValue GetAnchorTime() const = 0;

		/**
		 * Returns the total time range of the timeline, including the end time of the last sample
		 * to which the player will not be able to seek to.
		 *
		 * @return Time range of assets on the timeline.
		 */
		virtual FTimeRange GetTotalTimeRange() const = 0;


		/**
		 * Returns the seekable range of the timeline, which is a subset of the total
		 * time range. Playback can start at any point in the seekable range with
		 * additional constraints imposed by the format.
		 *
		 * @return Time range in which playback can start.
		 */
		virtual FTimeRange GetSeekableTimeRange() const = 0;

		/**
		 * Returns the timestamps of the segments from the video or audio track (if no video is present).
		 * Segments are required to start with a keyframe and can thus be used to start playback with.
		 */
		virtual void GetSeekablePositions(TArray<FTimespan>& OutPositions) const = 0;

		/**
		 * Returns the duration of the assets on the timeline.
		 * Typically this is the difference of the end and start values of the total time range
		 * unless this is a Live presentation for which the duration will be set to infinite.
		 *
		 * @return Duration of the timeline.
		 */
		virtual FTimeValue GetDuration() const = 0;

		/**
		 * Returns the number of media assets on the playback timeline.
		 *
		 * @return Number of media assets on the playback timeline.
		 */
		virtual int32 GetNumberOfMediaAssets() const = 0;

		/**
		 * Returns a media asset from the timeline by its index.
		 * Please note that media assets are not sorted by their time range on the timeline.
		 * This is done on purpose to keep the order of assets as they appear in the manifest.
		 *
		 * @param MediaAssetIndex
		 *               Index (0 to GetNumberOfMediaAssets()-1) of the asset to get.
		 *
		 * @return Shared pointer to the requested media asset.
		 */
		virtual TSharedPtrTS<ITimelineMediaAsset> GetMediaAssetByIndex(int32 MediaAssetIndex) const = 0;
	};



} // namespace Electra


