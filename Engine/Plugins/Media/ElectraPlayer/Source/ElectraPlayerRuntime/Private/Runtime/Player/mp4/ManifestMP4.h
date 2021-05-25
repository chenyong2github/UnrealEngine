// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Player/Manifest.h"
#include "Player/PlaybackTimeline.h"
#include "HTTP/HTTPManager.h"
#include "Demuxer/ParserISO14496-12.h"
#include "SynchronizedClock.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"
#include "Player/PlayerStreamReader.h"



namespace Electra
{

/**
 * This class represents the internal "manifest" or "playlist" of an mp4 file.
 * All metadata of the tracks it is composed of are maintained by this class.
 */
class FManifestMP4Internal : public IManifest, public TSharedFromThis<FManifestMP4Internal, ESPMode::ThreadSafe>
{
public:
	FManifestMP4Internal(IPlayerSessionServices* InPlayerSessionServices);
	virtual ~FManifestMP4Internal();

	FErrorDetail Build(TSharedPtrTS<IParserISO14496_12> MP4Parser, const FString& URL, const HTTP::FConnectionInfo& InConnectionInfo);

	virtual EType GetPresentationType() const override;
	virtual FTimeValue GetAnchorTime() const override
	{ return FTimeValue::GetZero(); }
	virtual FTimeRange GetTotalTimeRange() const override
	{ return MediaAsset.IsValid() ? MediaAsset->GetTimeRange() : FTimeRange(); }
	virtual FTimeRange GetSeekableTimeRange() const override
	{
		FTimeRange tr = GetTotalTimeRange();
		// FIXME: This would need to be the time of the last sync frame of video (if it exists) or audio.
		//        For now it does not need to be precise since the nearest sync frame is searched for when seeking.
		//        It doesn't seem to make a lot of sense to seek very close to the end and play from there so we
		//        allow only to go as close to the end as we deem feasible.
		tr.End -= FTimeValue().SetFromSeconds(2.0);
		if (tr.End < FTimeValue::GetZero())
		{
			tr.End = FTimeValue::GetZero();
		}
		return tr;
	}
	virtual void GetSeekablePositions(TArray<FTimespan>& OutPositions) const override
	{
		// For the time being we do not return anything here as that would require to iterate the tracks.
	}
	virtual FTimeValue GetDuration() const override
	{ return MediaAsset.IsValid() ? MediaAsset->GetDuration() : FTimeValue(); }
	virtual FTimeValue GetDefaultStartTime() const override
	{ return FTimeValue::GetInvalid(); }
	virtual void ClearDefaultStartTime() override
	{ }
	virtual void GetTrackMetadata(TArray<FTrackMetadata>& OutMetadata, EStreamType StreamType) const override;
	virtual FTimeValue GetMinBufferTime() const override;
	virtual void UpdateDynamicRefetchCounter() override;

	virtual IStreamReader* CreateStreamReaderHandler() override;
	virtual FResult FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;
	virtual FResult FindNextPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, TSharedPtrTS<const IStreamSegment> CurrentSegment) override;


	class FRepresentationMP4 : public IPlaybackAssetRepresentation
	{
	public:
		virtual ~FRepresentationMP4()
		{ }

		FErrorDetail CreateFrom(const IParserISO14496_12::ITrack* InTrack, const FString& URL);

		virtual FString GetUniqueIdentifier() const override
		{ return UniqueIdentifier; }
		virtual const FStreamCodecInformation& GetCodecInformation() const override
		{ return CodecInformation; }
		virtual int32 GetBitrate() const override
		{ return Bitrate; }
		virtual int32 GetQualityIndex() const override
		{ return 0; }
		virtual bool CanBePlayed() const override
		{ return true; }

		const FString& GetName() const
		{ return Name; }
	private:
		FStreamCodecInformation		CodecInformation;
		FString						UniqueIdentifier;
		FString						Name;
		int32						Bitrate;

		TArray<uint8>				CodecSpecificData;
		TArray<uint8>				CodecSpecificDataRAW;
	};

	class FAdaptationSetMP4 : public IPlaybackAssetAdaptationSet
	{
	public:
		virtual ~FAdaptationSetMP4()
		{ }

		FErrorDetail CreateFrom(const IParserISO14496_12::ITrack* InTrack, const FString& URL);

		virtual FString GetUniqueIdentifier() const override
		{ return UniqueIdentifier; }
		virtual FString GetListOfCodecs() const override
		{ return CodecRFC6381; }
		virtual FString GetLanguage() const override
		{ return Language; }
		virtual int32 GetNumberOfRepresentations() const override
		{ return Representation.IsValid() ? 1 : 0; }
		virtual TSharedPtrTS<IPlaybackAssetRepresentation> GetRepresentationByIndex(int32 RepresentationIndex) const override
		{ return RepresentationIndex == 0 ? Representation : TSharedPtrTS<IPlaybackAssetRepresentation>(); }
		virtual TSharedPtrTS<IPlaybackAssetRepresentation> GetRepresentationByUniqueIdentifier(const FString& InUniqueIdentifier) const override
		{ return Representation.IsValid() && Representation->GetUniqueIdentifier() == InUniqueIdentifier ? Representation : TSharedPtrTS<IPlaybackAssetRepresentation>(); }
	private:
		TSharedPtrTS<FRepresentationMP4>		Representation;
		FString									Language;
		FString									CodecRFC6381;
		FString									UniqueIdentifier;
	};

	class FTimelineAssetMP4 : public ITimelineMediaAsset, public TSharedFromThis<FTimelineAssetMP4, ESPMode::ThreadSafe>
	{
	public:
		FTimelineAssetMP4()
			: PlayerSessionServices(nullptr)
		{ }

		virtual ~FTimelineAssetMP4()
		{ }

		FErrorDetail Build(IPlayerSessionServices* InPlayerSessionServices, TSharedPtrTS<IParserISO14496_12> MP4Parser, const FString& URL);

		virtual FTimeRange GetTimeRange() const override
		{
			FTimeRange tr;
			tr.Start.SetToZero();
			tr.End = GetDuration();
			return tr;
		}

		virtual FTimeValue GetDuration() const override
		{
			if (MoovBoxParser.IsValid())
			{
				TMediaOptionalValue<FTimeFraction> movieDuration = MoovBoxParser->GetMovieDuration();
				if (movieDuration.IsSet())
				{
					FTimeValue dur;
					dur.SetFromTimeFraction(movieDuration.Value());
					return dur;
				}
			}
			return FTimeValue();
		}

		virtual FString GetAssetIdentifier() const override
		{ return FString("mp4-asset.0"); }
		virtual FString GetUniqueIdentifier() const override
		{ return FString("mp4-media.0"); }
		virtual int32 GetNumberOfAdaptationSets(EStreamType OfStreamType) const override
		{
			switch(OfStreamType)
			{
				case EStreamType::Video:
					return VideoAdaptationSets.Num();
				case EStreamType::Audio:
					return AudioAdaptationSets.Num();
				default:
					return 0;
			}
		}
		virtual TSharedPtrTS<IPlaybackAssetAdaptationSet> GetAdaptationSetByTypeAndIndex(EStreamType OfStreamType, int32 AdaptationSetIndex) const override
		{
			switch(OfStreamType)
			{
				case EStreamType::Video:
					return AdaptationSetIndex < VideoAdaptationSets.Num() ? VideoAdaptationSets[AdaptationSetIndex] : TSharedPtrTS<IPlaybackAssetAdaptationSet>();
				case EStreamType::Audio:
					return AdaptationSetIndex < AudioAdaptationSets.Num() ? AudioAdaptationSets[AdaptationSetIndex] : TSharedPtrTS<IPlaybackAssetAdaptationSet>();
				default:
					return 0;
			}
			return TSharedPtrTS<IPlaybackAssetAdaptationSet>();
		}

		virtual void GetMetaData(TArray<FTrackMetadata>& OutMetadata, EStreamType StreamType) const override
		{
			for(int32 i=0, iMax=GetNumberOfAdaptationSets(StreamType); i<iMax; ++i)
			{
				TSharedPtrTS<FAdaptationSetMP4> AdaptSet = StaticCastSharedPtr<FAdaptationSetMP4>(GetAdaptationSetByTypeAndIndex(StreamType, i));
				if (AdaptSet.IsValid())
				{
					FTrackMetadata tm;
					tm.ID = AdaptSet->GetUniqueIdentifier();
					tm.Language = AdaptSet->GetLanguage();
					tm.Kind = i==0 ? TEXT("main") : TEXT("translation");

					for(int32 j=0, jMax=AdaptSet->GetNumberOfRepresentations(); j<jMax; ++j)
					{
						TSharedPtrTS<FRepresentationMP4> Repr = StaticCastSharedPtr<FRepresentationMP4>(AdaptSet->GetRepresentationByIndex(j));
						if (Repr.IsValid())
						{
							FStreamMetadata sd;
							sd.Bandwidth = Repr->GetBitrate();
							sd.CodecInformation = Repr->GetCodecInformation();
							sd.ID = Repr->GetUniqueIdentifier();
							// There is only 1 "stream" per "track" so we can set the highest bitrate and codec info the same as the track.
							tm.HighestBandwidth = sd.Bandwidth;
							tm.HighestBandwidthCodec = sd.CodecInformation;

							tm.Label = Repr->GetName();

							tm.StreamDetails.Emplace(MoveTemp(sd));
						}
					}
					OutMetadata.Emplace(MoveTemp(tm));
				}
			}
		}


		void LimitSegmentDownloadSize(TSharedPtrTS<IStreamSegment>& InOutSegment, TSharedPtr<IParserISO14496_12::IAllTrackIterator, ESPMode::ThreadSafe> AllTrackIterator);

		FResult GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, ESearchType SearchType, int64 AtAbsoluteFilePos);
		FResult GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment);
		FResult GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, bool bReplaceWithFillerData);
		FResult GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, ESearchType SearchType);

		void GetSegmentInformation(TArray<IManifest::IPlayPeriod::FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const FString& AdaptationSetID, const FString& RepresentationID);
		TSharedPtrTS<IParserISO14496_12>	GetMoovBoxParser();
		const FString& GetMediaURL() const
		{
			return MediaURL;
		}

	private:
		void LogMessage(IInfoLog::ELevel Level, const FString& Message);
		IPlayerSessionServices* 						PlayerSessionServices;
		FString											MediaURL;
		TSharedPtrTS<IParserISO14496_12>				MoovBoxParser;
		TArray<TSharedPtrTS<FAdaptationSetMP4>>			VideoAdaptationSets;
		TArray<TSharedPtrTS<FAdaptationSetMP4>>			AudioAdaptationSets;
	};


	class FPlayPeriodMP4 : public IManifest::IPlayPeriod
	{
	public:
		FPlayPeriodMP4(TSharedPtrTS<FTimelineAssetMP4> InMediaAsset);
		virtual ~FPlayPeriodMP4();
		virtual void SetStreamPreferences(EStreamType ForStreamType, const FStreamSelectionAttributes& StreamAttributes) override;
		virtual EReadyState GetReadyState() override;
		virtual void Load() override;
		virtual void PrepareForPlay() override;
		virtual int64 GetDefaultStartingBitrate() const override;
		virtual TSharedPtrTS<FBufferSourceInfo> GetSelectedStreamBufferSourceInfo(EStreamType StreamType) override;
		virtual FString GetSelectedAdaptationSetID(EStreamType StreamType) override;
		virtual ETrackChangeResult ChangeTrackStreamPreference(EStreamType ForStreamType, const FStreamSelectionAttributes& StreamAttributes) override;
		virtual TSharedPtrTS<ITimelineMediaAsset> GetMediaAsset() const override;
		virtual void SelectStream(const FString& AdaptationSetID, const FString& RepresentationID) override;
		virtual FResult GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;
		virtual FResult GetContinuationSegment(TSharedPtrTS<IStreamSegment>& OutSegment, EStreamType StreamType, const FPlayerLoopState& LoopState, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;
		virtual FResult GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment) override;
		virtual FResult GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, bool bReplaceWithFillerData) override;
		virtual FResult GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, ESearchType SearchType) override;
		virtual void IncreaseSegmentFetchDelay(const FTimeValue& IncreaseAmount) override;
		virtual void GetSegmentInformation(TArray<FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const FString& AdaptationSetID, const FString& RepresentationID) override;

	private:
		void SelectInitialStream(EStreamType StreamType);
		TSharedPtrTS<FTrackMetadata> SelectMetadataForAttributes(EStreamType StreamType, const FStreamSelectionAttributes& InAttributes);
		void MakeBufferSourceInfoFromMetadata(EStreamType StreamType, TSharedPtrTS<FBufferSourceInfo>& OutBufferSourceInfo, TSharedPtrTS<FTrackMetadata> InMetadata);

		TWeakPtrTS<FTimelineAssetMP4>			MediaAsset;
		FStreamSelectionAttributes				VideoPreferences;
		FStreamSelectionAttributes				AudioPreferences;
		FStreamSelectionAttributes				SubtitlePreferences;
		TSharedPtrTS<FTrackMetadata>			SelectedVideoMetadata;
		TSharedPtrTS<FTrackMetadata>			SelectedAudioMetadata;
		TSharedPtrTS<FTrackMetadata>			SelectedSubtitleMetadata;
		TSharedPtrTS<FBufferSourceInfo>			VideoBufferSourceInfo;
		TSharedPtrTS<FBufferSourceInfo>			AudioBufferSourceInfo;
		TSharedPtrTS<FBufferSourceInfo>			SubtitleBufferSourceInfo;
		EReadyState								CurrentReadyState;
	};

	void LogMessage(IInfoLog::ELevel Level, const FString& Message);

	IPlayerSessionServices* 			PlayerSessionServices;
	TSharedPtrTS<FTimelineAssetMP4>		MediaAsset;
	HTTP::FConnectionInfo				ConnectionInfo;
};


} // namespace Electra


