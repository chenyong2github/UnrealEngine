// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "ManifestDASH.h"
#include "ManifestBuilderDASH.h"
#include "PlaylistReaderDASH.h"
#include "StreamReaderFMP4DASH.h"
#include "Demuxer/ParserISO14496-12.h"
#include "Demuxer/ParserISO14496-12_Utils.h"
#include "Player/PlayerSessionServices.h"
#include "SynchronizedClock.h"
#include "Utilities/Utilities.h"
#include "Utilities/StringHelpers.h"
#include "Utilities/URLParser.h"
#include "Player/DASH/OptionKeynamesDASH.h"
#include "Player/PlayerEntityCache.h"

namespace Electra
{

#define ERRCODE_DASH_MPD_INTERNAL						1
#define ERRCODE_DASH_MPD_BAD_REPRESENTATION				1000


namespace DashUtils
{
	#define GETPLAYEROPTION(Type, Getter)																						\
		bool GetPlayerOption(IPlayerSessionServices* InPlayerSessionServices, Type& OutValue, const TCHAR* Key, Type Default)	\
		{																														\
			if (InPlayerSessionServices->GetOptions().HaveKey(Key))																\
			{																													\
				OutValue = InPlayerSessionServices->GetOptions().GetValue(Key).Getter(Default);									\
				return true;																									\
			}																													\
			OutValue = Default;																									\
			return false;																										\
		}
	GETPLAYEROPTION(FString, SafeGetFString);
	GETPLAYEROPTION(double, SafeGetDouble);
	GETPLAYEROPTION(int64, SafeGetInt64);
	GETPLAYEROPTION(bool, SafeGetBool);
	GETPLAYEROPTION(FTimeValue, SafeGetTimeValue);
	#undef GETPLAYEROPTION


	/**
	 * Helper class to parse a segment index (sidx box) from an ISO/IEC-14496:12 file.
	 */
	class FMP4SidxBoxReader : public FMP4StaticDataReader, public IParserISO14496_12::IBoxCallback
	{
	public:
		FMP4SidxBoxReader() = default;
		virtual ~FMP4SidxBoxReader() = default;
	private:
		//----------------------------------------------------------------------
		// Methods from IParserISO14496_12::IBoxCallback
		//
		virtual EParseContinuation OnFoundBox(IParserISO14496_12::FBoxType Box, int64 BoxSizeInBytes, int64 FileDataOffset, int64 BoxDataOffset) override
		{
			if (bHaveSIDX)
			{
				return IParserISO14496_12::IBoxCallback::EParseContinuation::Stop;
			}
			if (Box == IParserISO14496_12::BoxType_sidx)
			{
				bHaveSIDX = true;
			}
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
		}
		bool bHaveSIDX = false;
	};

}



class FDASHTimeline : public IPlaybackAssetTimeline
{
public:
	FDASHTimeline(TSharedPtrTS<FManifestDASHInternal> InInternalManifest);

	virtual ~FDASHTimeline();
	virtual FTimeValue GetAnchorTime() const override;
	virtual FTimeRange GetTotalTimeRange() const override;
	virtual FTimeRange GetSeekableTimeRange() const override;
	virtual void GetSeekablePositions(TArray<FTimespan>& OutPositions) const override;
	virtual FTimeValue GetDuration() const override;
	virtual int32 GetNumberOfMediaAssets() const override;
	virtual TSharedPtrTS<ITimelineMediaAsset> GetMediaAssetByIndex(int32 MediaAssetIndex) const override;

	TSharedPtrTS<FManifestDASHInternal> GetManifest()
	{
		return Manifest;
	}
private:
	TSharedPtrTS<FManifestDASHInternal> Manifest;
	mutable FTimeValue					AnchorTime;
	mutable FTimeRange					TotalTimeRange;
	mutable FTimeRange					SeekableTimeRange;
};


FDASHTimeline::FDASHTimeline(TSharedPtrTS<FManifestDASHInternal> InInternalManifest)
	: Manifest(InInternalManifest)
{
}

FDASHTimeline::~FDASHTimeline()
{
}

FTimeValue FDASHTimeline::GetAnchorTime() const
{
	if (!AnchorTime.IsValid())
	{
		TSharedPtrTS<const FDashMPD_MPDType> MPDRoot = Manifest->GetMPDRoot();
		AnchorTime = MPDRoot->GetAvailabilityStartTime().IsValid() ? MPDRoot->GetAvailabilityStartTime() : FTimeValue::GetZero();
	}
	return AnchorTime;
}

FTimeRange FDASHTimeline::GetTotalTimeRange() const
{
	if (!TotalTimeRange.IsValid())
	{
		if (Manifest->GetPresentationType() == FManifestDASHInternal::EPresentationType::Static)
		{
			FTimeValue Anchor = GetAnchorTime();
			TotalTimeRange.Start = Anchor + Manifest->GetPeriods()[0]->GetStart();
			TotalTimeRange.End = TotalTimeRange.Start + GetDuration();
		}
		else
		{
// TODO:
			check(!"TODO");
		}
	}
	return TotalTimeRange;
}


FTimeRange FDASHTimeline::GetSeekableTimeRange() const
{
	if (!SeekableTimeRange.IsValid())
	{
		if (Manifest->GetPresentationType() == FManifestDASHInternal::EPresentationType::Static)
		{
			FTimeValue Anchor = GetAnchorTime();
			SeekableTimeRange.Start = Anchor + Manifest->GetPeriods()[0]->GetStart();
			// FIXME: 10 seconds is an arbitrary value. We do not know the actual segment duration of the very last segment
			//        so the only recourse right now is to only allow seeking up to some sensible point before the end.
			SeekableTimeRange.End = Anchor + Manifest->GetPeriods().Last()->GetEnd() - FTimeValue().SetFromSeconds(10.0);
			if (SeekableTimeRange.End < SeekableTimeRange.Start)
			{
				SeekableTimeRange.End = SeekableTimeRange.Start;
			}
		}
		else
		{
// TODO:
			check(!"TODO");
		}
	}
	return SeekableTimeRange;
}

void FDASHTimeline::GetSeekablePositions(TArray<FTimespan>& OutPositions) const
{
	auto Periods = Manifest->GetPeriods();
	FTimeValue Anchor = GetAnchorTime();
	for(int32 i=0; i<Periods.Num(); ++i)
	{
		if (!Periods[i]->GetIsEarlyPeriod())
		{
			if (Manifest->GetPresentationType() == FManifestDASHInternal::EPresentationType::Static)
			{
				// The beginning of a period is a seekable position.
				OutPositions.Emplace(FTimespan((Periods[i]->GetStart() + Anchor).GetAsHNS()));
			}
			else
			{
// TODO:
				/*
					In a dynamic presentation we need to consider the timeshiftBufferDepth and suggestedPresentationDelay
					with regards to the MPD fetch time.
				*/
				check(!"TODO");
			}
		}
	}
}

FTimeValue FDASHTimeline::GetDuration() const
{
	if (Manifest->GetPresentationType() == FManifestDASHInternal::EPresentationType::Static)
	{
		if (Manifest->GetMPDRoot()->GetMediaPresentationDuration().IsValid())
		{
			return Manifest->GetMPDRoot()->GetMediaPresentationDuration();
		}
		else
		{
			auto Periods = Manifest->GetPeriods();
			for(int32 i=Periods.Num()-1; i>=0; --i)
			{
				if (!Periods[i]->GetIsEarlyPeriod())
				{
					return Periods[i]->GetEnd() - Periods[0]->GetStart();
				}
			}
			return FTimeValue::GetInvalid();
		}
	}
	else
	{
		// This is not necessarily infinity. A dynamic presentation can still have a predefined duration
		// of pre-existing content that is made available in a dynamic way.
// TODO:
		check(!"TODO");
		return FTimeValue::GetPositiveInfinity();
	}
}


/**
	* Returns the number of media assets on the playback timeline.
	*
	* @return Number of media assets on the playback timeline.
	*/
int32 FDASHTimeline::GetNumberOfMediaAssets() const
{
	check(!"TODO");
	return 0;
}

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
TSharedPtrTS<ITimelineMediaAsset> FDASHTimeline::GetMediaAssetByIndex(int32 MediaAssetIndex) const
{
	check(!"TODO");
	return nullptr;
}


/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

class FDASHPlayPeriod : public IManifest::IPlayPeriod
{
public:
	FDASHPlayPeriod(IPlayerSessionServices* InPlayerSessionServices, TSharedPtrTS<FManifestDASHInternal::FPeriod> SelectedPeriod, TSharedPtrTS<FDASHTimeline> InTimeline)
		: PlayerSessionServices(InPlayerSessionServices)
		, InternalPeriod(MoveTemp(SelectedPeriod))
		, Timeline(MoveTemp(InTimeline))
	{
	}

	virtual ~FDASHPlayPeriod()
	{
	}

	//----------------------------------------------
	// Methods from IManifest::IPlayPeriod
	//
	virtual void SetStreamPreferences(const FStreamPreferences& Preferences) override;
	virtual EReadyState GetReadyState() override;
	virtual void PrepareForPlay(const FParamDict& Options) override;
	virtual TSharedPtrTS<ITimelineMediaAsset> GetMediaAsset() const override;
	virtual void SelectStream(const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation) override;
	virtual IManifest::FResult GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, IManifest::ESearchType SearchType) override;
	virtual IManifest::FResult GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, IManifest::ESearchType SearchType) override;
	virtual IManifest::FResult GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment) override;
	virtual IManifest::FResult GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> CurrentSegment, bool bReplaceWithFillerData) override;
	virtual void GetSegmentInformation(TArray<FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation) override;

private:
	ELECTRA_IMPL_DEFAULT_ERROR_METHODS(DASHManifest);

	TSharedPtrTS<IPlaybackAssetRepresentation> GetRepresentationFromAdaptationByMaxBandwidth(TWeakPtrTS<IPlaybackAssetAdaptationSet> InAdaptationSet, int32 NotExceedingBandwidth);
	IManifest::FResult GetNextOrRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> InCurrentSegment, bool bForRetry);


	IPlayerSessionServices* PlayerSessionServices = nullptr;
	TWeakPtrTS<FManifestDASHInternal::FPeriod> InternalPeriod;
	TWeakPtrTS<FDASHTimeline> Timeline;
	EReadyState ReadyState = EReadyState::NotReady;
	FStreamPreferences StreamPreferences;

	TWeakPtrTS<IPlaybackAssetAdaptationSet> ActiveVideoAdaptationSet;
	TWeakPtrTS<IPlaybackAssetAdaptationSet> ActiveAudioAdaptationSet;

	TWeakPtrTS<IPlaybackAssetRepresentation> ActiveVideoRepresentation;
	TWeakPtrTS<IPlaybackAssetRepresentation> ActiveAudioRepresentation;
};

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/


TSharedPtrTS<FManifestDASH> FManifestDASH::Create(IPlayerSessionServices* SessionServices, const FParamDict& Options, TWeakPtrTS<FPlaylistReaderDASH> PlaylistReader, TSharedPtrTS<FManifestDASHInternal> Manifest)
{
	FManifestDASH* m = new FManifestDASH(SessionServices, Options, PlaylistReader, Manifest);
	return MakeShareable<FManifestDASH>(m);
}

FManifestDASH::FManifestDASH(IPlayerSessionServices* InSessionServices, const FParamDict& InOptions, TWeakPtrTS<FPlaylistReaderDASH> InPlaylistReader, TSharedPtrTS<FManifestDASHInternal> InManifest)
	: Options(InOptions)
	, InternalManifest(InManifest)
	, PlayerSessionServices(InSessionServices)
	, PlaylistReader(InPlaylistReader)
{
	bHaveCurrentMetadata = false;
}


FManifestDASH::~FManifestDASH()
{
}

void FManifestDASH::UpdateTimeline()
{
	TSharedPtrTS<FManifestDASHInternal> m = InternalManifest.Pin();
	if (m.IsValid())
	{
		CurrentTimeline = MakeSharedTS<FDASHTimeline>(m);
	}
	else
	{
		CurrentTimeline.Reset();
	}
}


IManifest::EType FManifestDASH::GetPresentationType() const
{
	TSharedPtrTS<FManifestDASHInternal> Manifest(InternalManifest.Pin());
	if (Manifest.IsValid())
	{
		return Manifest->GetPresentationType() == FManifestDASHInternal::EPresentationType::Static ? IManifest::EType::OnDemand : IManifest::EType::Live;
	}
	return IManifest::EType::OnDemand;
}

TSharedPtrTS<IPlaybackAssetTimeline> FManifestDASH::GetTimeline() const
{
	return CurrentTimeline;
}

int64 FManifestDASH::GetDefaultStartingBitrate() const
{
	return 2 * 1000 * 1000;
}

FTimeValue FManifestDASH::GetMinBufferTime() const
{
	TSharedPtrTS<FManifestDASHInternal> Manifest(InternalManifest.Pin());
	if (Manifest.IsValid())
	{
		TSharedPtrTS<const FDashMPD_MPDType> MPDRoot = Manifest->GetMPDRoot();
		return MPDRoot->GetMinBufferTime();
	}
	return FTimeValue();
}

void FManifestDASH::GetStreamMetadata(TArray<FStreamMetadata>& OutMetadata, EStreamType StreamType) const
{
	if (CurrentTimeline.IsValid())
	{
		if (!bHaveCurrentMetadata)
		{
			bHaveCurrentMetadata = true;
			TSharedPtrTS<FManifestDASHInternal> Manifest = CurrentTimeline->GetManifest();
			check(Manifest.IsValid());
			if (Manifest->GetPeriods().Num())
			{
				Manifest->PreparePeriodAdaptationSets(PlayerSessionServices, Manifest->GetPeriods()[0], false);
				const TArray<TSharedPtrTS<FManifestDASHInternal::FAdaptationSet>>& AdaptationSets = Manifest->GetPeriods()[0]->GetAdaptationSets();
				for(int32 nA=0; nA<AdaptationSets.Num(); ++nA)
				{
					FStreamMetadata sm;
					sm.CodecInformation = AdaptationSets[nA]->GetCodec();
					sm.PlaylistID = Manifest->GetPeriods()[0]->GetID();
					sm.Bandwidth = AdaptationSets[nA]->GetMaxBandwidth();
					sm.StreamUniqueID = 0;
					sm.LanguageCode = AdaptationSets[nA]->GetLanguage();
					switch(AdaptationSets[nA]->GetCodec().GetStreamType())
					{
						case EStreamType::Video:
						{
							CurrentMetadataVideo.Emplace(MoveTemp(sm));
							break;
						}
						case EStreamType::Audio:
						{
							CurrentMetadataAudio.Emplace(MoveTemp(sm));
							break;
						}
						case EStreamType::Subtitle:
						{
							CurrentMetadataSubtitle.Emplace(MoveTemp(sm));
							break;
						}
					}
				}
			}
		}

		// At present we return metadata from the first period only as every period can have totally different
		// number of streams and even codecs. There is no commonality between periods.
		if (StreamType == EStreamType::Video)
		{
			OutMetadata = CurrentMetadataVideo;
		}
		else if (StreamType == EStreamType::Audio)
		{
			OutMetadata = CurrentMetadataAudio;
		}
		else if (StreamType == EStreamType::Subtitle)
		{
			OutMetadata = CurrentMetadataSubtitle;
		}
	}
}

void FManifestDASH::UpdateDynamicRefetchCounter()
{
	++CurrentPeriodAndAdaptationXLinkResolveID;
}


IStreamReader* FManifestDASH::CreateStreamReaderHandler()
{
	return new FStreamReaderFMP4DASH;
}

IManifest::FResult FManifestDASH::FindPlayPeriod(TSharedPtrTS<IPlayPeriod>& OutPlayPeriod, const FPlayStartPosition& StartPosition, ESearchType SearchType)
{
	TSharedPtrTS<FDASHTimeline> Timeline = CurrentTimeline;
	// When there is no current timeline we return NotFound.
	if (!Timeline.IsValid())
	{
		return IManifest::FResult();
	}

	TSharedPtrTS<FManifestDASHInternal> Manifest = Timeline->GetManifest();
	FTimeValue StartTime = StartPosition.Time;

	// All time values we communicate to the outside - and therefor get from the outside - are offset by the availabilityStartTime.
	StartTime -= Timeline->GetAnchorTime();

	// Find the period into which the start time falls.
	TSharedPtrTS<FManifestDASHInternal::FPeriod> SelectedPeriod;
	const TArray<TSharedPtrTS<FManifestDASHInternal::FPeriod>>& Periods = Manifest->GetPeriods();
	for(int32 nPeriod=0; !SelectedPeriod.IsValid() && nPeriod<Periods.Num(); ++nPeriod)
	{
		if (!Periods[nPeriod]->GetIsEarlyPeriod())
		{
			if (StartTime >= Periods[nPeriod]->GetStart())
			{
				FTimeValue PeriodEndTime = Periods[nPeriod]->GetEnd();
				// When the period end time is not valid it must be the last period of a Live presentation
				if (!PeriodEndTime.IsValid())
				{
					PeriodEndTime.SetToPositiveInfinity();
				}
				// Does the time fall into this period?
				if (StartTime < PeriodEndTime)
				{
					FTimeValue DiffToNextPeriod = nPeriod + 1 < Periods.Num() && !Periods[nPeriod+1]->GetIsEarlyPeriod() ? Periods[nPeriod + 1]->GetStart() - StartTime : FTimeValue::GetPositiveInfinity();
					//FTimeValue DiffToPrevPeriod = nPeriod ? StartTime - Periods[nPeriod - 1]->GetEnd() : FTimeValue::GetPositiveInfinity();
					//FTimeValue DiffToStart = StartTime - Periods[nPeriod]->GetStart();
					FTimeValue DiffToEnd = PeriodEndTime - StartTime;
					switch(SearchType)
					{
						case ESearchType::Closest:
						{
							// Closest only looks at the distance to the following period. It will never take us to the preceeding period since it is unlikely
							// the intention (for closest) is to start playback at the end of a period only to transition into this one.
							if (DiffToEnd.IsValid() && DiffToNextPeriod <= DiffToEnd)
							{
								SelectedPeriod = Periods[nPeriod + 1];
							}
							else
							{
								SelectedPeriod = Periods[nPeriod];
							}
							break;
						}
						case ESearchType::Before:
						case ESearchType::Same:
						case ESearchType::After:
						{
							// Before, Same and After have no meaning when looking for a period. The period the start time falls into is the one to use.
							SelectedPeriod = Periods[nPeriod];
							break;
						}
						case ESearchType::StrictlyAfter:
						{
							if (DiffToNextPeriod.IsValid() && !DiffToNextPeriod.IsInfinity())
							{
								SelectedPeriod = Periods[nPeriod + 1];
							}
							break;
						}
						case ESearchType::StrictlyBefore:
						{
							if (nPeriod)
							{
								SelectedPeriod = Periods[nPeriod - 1];
							}
							break;
						}
					}
					// Time fell into this period. We have either found a candidate or not. We're done either way.
					break;
				}
			}
		}
	}

	if (SelectedPeriod.IsValid())
	{
		// Is the original period still there?
		TSharedPtrTS<FDashMPD_PeriodType> MPDPeriod = SelectedPeriod->GetMPDPeriod();
		if (MPDPeriod.IsValid())
		{
			// Does this period require onRequest xlink resolving?
			if (MPDPeriod->GetXLink().IsSet())
			{
				// Does the period require (re-)resolving?
				if (MPDPeriod->GetXLink().LastResolveID < CurrentPeriodAndAdaptationXLinkResolveID && !MPDPeriod->GetXLink().LoadRequest.IsValid())
				{
					// Need to resolve the xlink now.
					check(!"TODO");
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Triggering period xlink resolve")));
					return IManifest::FResult(IManifest::FResult::EType::TryAgainLater).RetryAfterMilliseconds(100);
				}
			}
			// Wrap the period in an externally accessible interface.
			TSharedPtrTS<FDASHPlayPeriod> PlayPeriod = MakeSharedTS<FDASHPlayPeriod>(PlayerSessionServices, SelectedPeriod, Timeline);
			OutPlayPeriod = PlayPeriod;
			return IManifest::FResult(IManifest::FResult::EType::Found);
		}
		else
		{
			// The period has disappeared. This may happen with an MPD update and means
			// we have to try this all over with the updated one.
			LogMessage(PlayerSessionServices, IInfoLog::ELevel::Verbose, FString::Printf(TEXT("Original MPD period not available, trying again.")));
			return IManifest::FResult(IManifest::FResult::EType::TryAgainLater).RetryAfterMilliseconds(100);
		}
	}
	return IManifest::FResult();
}


/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

void FDASHPlayPeriod::SetStreamPreferences(const FStreamPreferences& Preferences)
{
	StreamPreferences = Preferences;
}

IManifest::IPlayPeriod::EReadyState FDASHPlayPeriod::GetReadyState()
{
	return ReadyState;
}

void FDASHPlayPeriod::PrepareForPlay(const FParamDict& Options)
{
	TSharedPtrTS<FManifestDASHInternal::FPeriod> Period = InternalPeriod.Pin();
	if (!Period.IsValid())
	{
		LogMessage(PlayerSessionServices, IInfoLog::ELevel::Verbose, FString::Printf(TEXT("Period in PrepareToPlay() is no longer valid. Must reselect.")));
		ReadyState = EReadyState::MustReselect;
		return;
	}

	// We need to select one adaptation set per stream type we wish to play.
	int32 NumVideoAdaptationSets = Period->GetNumberOfAdaptationSets(EStreamType::Video);
	if (NumVideoAdaptationSets > 0)
	{
		// For now pick the first one!
		TSharedPtrTS<IPlaybackAssetAdaptationSet> VideoAS = Period->GetAdaptationSetByTypeAndIndex(EStreamType::Video, 0);
		ActiveVideoAdaptationSet = VideoAS;

		TSharedPtrTS<IPlaybackAssetRepresentation> VideoRepr = GetRepresentationFromAdaptationByMaxBandwidth(VideoAS, 2*1000*1000);
		ActiveVideoRepresentation = VideoRepr;
	}

	int32 NumAudioAdaptationSets = Period->GetNumberOfAdaptationSets(EStreamType::Audio);
	if (NumAudioAdaptationSets > 0)
	{
		// For now pick the first one!
		TSharedPtrTS<IPlaybackAssetAdaptationSet> AudioAS = Period->GetAdaptationSetByTypeAndIndex(EStreamType::Audio, 0);
		ActiveAudioAdaptationSet = AudioAS;

		TSharedPtrTS<IPlaybackAssetRepresentation> AudioRepr = GetRepresentationFromAdaptationByMaxBandwidth(AudioAS, 128 * 1000);
		ActiveAudioRepresentation = AudioRepr;
	}

//	ReadyState = EReadyState::Preparing;
	ReadyState = EReadyState::IsReady;
}

TSharedPtrTS<ITimelineMediaAsset> FDASHPlayPeriod::GetMediaAsset() const
{
	return InternalPeriod.Pin();
}

void FDASHPlayPeriod::SelectStream(const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation)
{
	// The ABR must not try to switch adaptation sets at the moment. As such the adaptation set passed in must be one of the already active ones.
	if (AdaptationSet == ActiveVideoAdaptationSet)
	{
		ActiveVideoRepresentation = Representation;
	}
	else if (AdaptationSet == ActiveAudioAdaptationSet)
	{
		ActiveAudioRepresentation = Representation;
	}
	else
	{
		LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("ABR tried to activate a stream from an inactive AdaptationSet!")));
	}
}

IManifest::FResult FDASHPlayPeriod::GetStartingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, const FPlayStartPosition& StartPosition, IManifest::ESearchType SearchType)
{
	TSharedPtrTS<FManifestDASHInternal::FPeriod> Period = InternalPeriod.Pin();
	if (!Period.IsValid())
	{
		return IManifest::FResult(IManifest::FResult::EType::NotLoaded).SetErrorDetail(FErrorDetail().SetMessage("Period to locate start segment in has disappeared"));
	}
	TSharedPtrTS<FDASHTimeline> TL = Timeline.Pin();
	if (!TL.IsValid())
	{
		return IManifest::FResult(IManifest::FResult::EType::NotLoaded).SetErrorDetail(FErrorDetail().SetMessage("Timeline the period to locate start segment in has disappeared"));
	}

	FTimeValue StartTime = StartPosition.Time;
	// All time values we communicate to the outside - and therefor get from the outside - are offset by the availabilityStartTime.
	StartTime -= TL->GetAnchorTime();

	// Due to the way we have been searching for the period it is possible for the start time to fall (slightly) outside the actual times.
	if (StartTime < Period->GetStart())
	{
		StartTime = Period->GetStart();
	}
	else if (StartTime >= Period->GetEnd())
	{
		StartTime = Period->GetEnd();
	}
	// We are searching for a time local to the period so we need to subtract the period start time.
	StartTime -= Period->GetStart();


	// Create a segment request to which the individual stream segment requests will add themselves as
	// dependent streams. This is a special case for playback start.
	TSharedPtrTS<FStreamSegmentRequestFMP4DASH> StartSegmentRequest = MakeSharedTS<FStreamSegmentRequestFMP4DASH>();
	StartSegmentRequest->bIsInitialStartRequest = true;


	struct FSelectedStream
	{
		EStreamType										StreamType;
		TSharedPtrTS<IPlaybackAssetRepresentation>		Representation;
		TSharedPtrTS<IPlaybackAssetAdaptationSet>		AdaptationSet;
	};
	TArray<FSelectedStream> ActiveSelection;
	if (ActiveVideoRepresentation.IsValid())
	{
		ActiveSelection.Emplace(FSelectedStream({EStreamType::Video, ActiveVideoRepresentation.Pin(), ActiveVideoAdaptationSet.Pin()}));
	}
	if (ActiveAudioRepresentation.IsValid())
	{
		ActiveSelection.Emplace(FSelectedStream({EStreamType::Audio, ActiveAudioRepresentation.Pin(), ActiveAudioAdaptationSet.Pin()}));
	}


	bool bDidAdjustStartTime = false;
	bool bTryAgainLater = false;
	bool bAnyStreamAtEOS = false;
	bool bAllStreamsAtEOS = true;
	for(int32 i=0; i<ActiveSelection.Num(); ++i)
	{
		if (ActiveSelection[i].AdaptationSet.IsValid() && ActiveSelection[i].Representation.IsValid())
		{
			FManifestDASHInternal::FRepresentation* Repr = static_cast<FManifestDASHInternal::FRepresentation*>(ActiveSelection[i].Representation.Get());

			FManifestDASHInternal::FSegmentInformation SegmentInfo;
			FManifestDASHInternal::FSegmentSearchOption SearchOpt;
			TArray<TWeakPtrTS<FMPDLoadRequestDASH>> RemoteElementLoadRequests;
			SearchOpt.PeriodLocalTime = StartTime;
			SearchOpt.PeriodDuration = Period->GetDuration();
			SearchOpt.SearchType = SearchType;
			FManifestDASHInternal::FRepresentation::ESearchResult SearchResult = Repr->FindSegment(PlayerSessionServices, SegmentInfo, RemoteElementLoadRequests, SearchOpt);
			if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::NeedElement)
			{
				TSharedPtrTS<IPlaylistReader> ManifestReader = PlayerSessionServices->GetManifestReader();
				if (!ManifestReader.IsValid())
				{
					return IManifest::FResult(IManifest::FResult::EType::NotLoaded).SetErrorDetail(FErrorDetail().SetMessage("Entity loader disappeared"));
				}
				check(ManifestReader->GetPlaylistType().Equals(TEXT("dash")));
				IPlaylistReaderDASH* Reader = static_cast<IPlaylistReaderDASH*>(ManifestReader.Get());
				Reader->AddElementLoadRequests(RemoteElementLoadRequests);
				bTryAgainLater = true;
			}
			else if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::PastEOS)
			{
				TSharedPtrTS<FStreamSegmentRequestFMP4DASH> SegmentRequest = MakeSharedTS<FStreamSegmentRequestFMP4DASH>();
				SegmentRequest->StreamType = ActiveSelection[i].StreamType;
				SegmentRequest->Representation = ActiveSelection[i].Representation;
				SegmentRequest->AdaptationSet = ActiveSelection[i].AdaptationSet;
				SegmentRequest->Period = Period;
				SegmentRequest->PeriodStart = Period->GetStart();
				SegmentRequest->AvailabilityStartTime = TL->GetAnchorTime();
				SegmentRequest->Segment = MoveTemp(SegmentInfo);
				SegmentRequest->bIsEOSSegment = true;
				StartSegmentRequest->DependentStreams.Emplace(MoveTemp(SegmentRequest));
				bAnyStreamAtEOS = true;
			}
			else if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::Gone)
			{
				// This should only be intermittent during a playlist refresh. Try again shortly.
				bTryAgainLater = true;
			}
			else if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::BadType)
			{
				// This representation has now been disabled. Try again as soon as possible, which should pick a different representation then
				// unless the problem was that fatal that an error has been posted.
				return IManifest::FResult().RetryAfterMilliseconds(0);
			}
			else if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::Found)
			{
				// The search result will have returned a media local time of the segment to start with.
				// In order to find the best matching audio and subtitle (or other) segments we adjust
				// the search time for these now.
				// The reasoning being that these types of streams should have only SAP types 1 and
				// can begin decoding on any segment and access unit.
				if (ActiveSelection[i].StreamType == EStreamType::Video && !bDidAdjustStartTime)
				{
					bDidAdjustStartTime = true;

					// At the moment we need to start at the beginning of the segment where the IDR frame is located.
					// Frame accuracy is a problem because we need to start decoding all the frames from the start of the segment
					// anyway - and then discard them - in order to get to the frame of interest.
					// This is wasteful and prevents fast startup, so we set the start time to the beginning of the segment.
					SegmentInfo.MediaLocalFirstAUTime = SegmentInfo.Time;

					StartTime = Period->GetStart() + FTimeValue().SetFromND(SegmentInfo.Time - SegmentInfo.PTO, SegmentInfo.Timescale);
					SearchType = IManifest::ESearchType::Before;
				}

				TSharedPtrTS<FStreamSegmentRequestFMP4DASH> SegmentRequest = MakeSharedTS<FStreamSegmentRequestFMP4DASH>();
				SegmentRequest->StreamType = ActiveSelection[i].StreamType;
				SegmentRequest->Representation = ActiveSelection[i].Representation;
				SegmentRequest->AdaptationSet = ActiveSelection[i].AdaptationSet;
				SegmentRequest->Period = Period;
				SegmentRequest->PeriodStart = Period->GetStart();
				SegmentRequest->AvailabilityStartTime = TL->GetAnchorTime();
				// If the segment is known to be missing we need to instead insert filler data.
				if (SegmentInfo.bIsMissing)
				{
					SegmentRequest->bInsertFillerData = true;
				}
				SegmentRequest->Segment = MoveTemp(SegmentInfo);

				// The start segment request needs to be able to return a valid first PTS which is what the player sets
				// the playback position to. If not valid yet update it with the current stream values.
				if (!StartSegmentRequest->GetFirstPTS().IsValid())
				{
					StartSegmentRequest->AvailabilityStartTime = SegmentRequest->AvailabilityStartTime;
					StartSegmentRequest->AdditionalAdjustmentTime = SegmentRequest->AdditionalAdjustmentTime;
					StartSegmentRequest->PeriodStart = SegmentRequest->PeriodStart;
					StartSegmentRequest->Segment = SegmentRequest->Segment;
				}

				StartSegmentRequest->DependentStreams.Emplace(MoveTemp(SegmentRequest));
				bAllStreamsAtEOS = false;
			}
			else
			{
				check(!"Unhandled search result!");
				return IManifest::FResult();
			}
		}
	}

	// Any waiters?
	if (bTryAgainLater)
	{
		return IManifest::FResult().RetryAfterMilliseconds(100);
	}

	// All streams already at EOS?
	if (bAnyStreamAtEOS && bAllStreamsAtEOS)
	{
		return IManifest::FResult(IManifest::FResult::EType::PastEOS);
	}

	// Done. Note that there is no 'NotFound', 'BeforeStart' or 'NotLoaded'.
	OutSegment = MoveTemp(StartSegmentRequest);
	return IManifest::FResult(IManifest::FResult::EType::Found);
}


IManifest::FResult FDASHPlayPeriod::GetNextOrRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> InCurrentSegment, bool bForRetry)
{
	// Make sure we still got the same MPD and everything.
	// These will likely become invalid when the MPD updates!
	check(Timeline.Pin());
	check(InternalPeriod.Pin());
	//
	check(InCurrentSegment.IsValid());
	TSharedPtrTS<const FStreamSegmentRequestFMP4DASH> Current = StaticCastSharedPtr<const FStreamSegmentRequestFMP4DASH>(InCurrentSegment);
	check(Current->bIsInitialStartRequest == false);

	TSharedPtrTS<IPlaybackAssetRepresentation> ActiveRepresentationByType;
	TSharedPtrTS<IPlaybackAssetAdaptationSet> ActiveAdaptationSetByType;
	switch(Current->GetType())
	{
		case EStreamType::Video:
			ActiveRepresentationByType = ActiveVideoRepresentation.Pin();
			ActiveAdaptationSetByType = ActiveVideoAdaptationSet.Pin();
			break;
		case EStreamType::Audio:
			ActiveRepresentationByType = ActiveAudioRepresentation.Pin();
			ActiveAdaptationSetByType = ActiveAudioAdaptationSet.Pin();
			break;
		default:
			break;
	}
	if (!ActiveAdaptationSetByType.IsValid() || !ActiveRepresentationByType.IsValid())
	{
		return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("No active stream found to get next segment for"));
	}

	TSharedPtrTS<FDASHTimeline> TL = Timeline.Pin();
	TSharedPtrTS<FManifestDASHInternal::FPeriod> Period = InternalPeriod.Pin();

	FManifestDASHInternal::FRepresentation* Repr = static_cast<FManifestDASHInternal::FRepresentation*>(ActiveRepresentationByType.Get());
	FManifestDASHInternal::FSegmentInformation SegmentInfo;
	FManifestDASHInternal::FSegmentSearchOption SearchOpt;
	TArray<TWeakPtrTS<FMPDLoadRequestDASH>> RemoteElementLoadRequests;
	if (!bForRetry)
	{
		// Set up the search time as the time three quarters into the current segment.
		// This is to make sure the time is sufficiently large that it won't be affected by rounding errors in timescale conversions.
		SearchOpt.PeriodLocalTime.SetFromND(Current->Segment.Time - Current->Segment.PTO + Current->Segment.Duration*3/4, Current->Segment.Timescale);
		SearchOpt.PeriodDuration = Period->GetDuration();
		SearchOpt.SearchType = IManifest::ESearchType::After;
	}
	else
	{
		// Use the same period local time for the retry representation as was used to locate the current segment.
		SearchOpt.PeriodLocalTime.SetFromND(Current->Segment.Time - Current->Segment.PTO, Current->Segment.Timescale);
		SearchOpt.PeriodDuration = Period->GetDuration();
		SearchOpt.SearchType = IManifest::ESearchType::Closest;
	}
	FManifestDASHInternal::FRepresentation::ESearchResult SearchResult = Repr->FindSegment(PlayerSessionServices, SegmentInfo, RemoteElementLoadRequests, SearchOpt);
	if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::NeedElement)
	{
		TSharedPtrTS<IPlaylistReader> ManifestReader = PlayerSessionServices->GetManifestReader();
		if (!ManifestReader.IsValid())
		{
			return IManifest::FResult(IManifest::FResult::EType::NotFound).SetErrorDetail(FErrorDetail().SetMessage("Entity loader disappeared"));
		}
		check(ManifestReader->GetPlaylistType().Equals(TEXT("dash")));
		IPlaylistReaderDASH* Reader = static_cast<IPlaylistReaderDASH*>(ManifestReader.Get());
		Reader->AddElementLoadRequests(RemoteElementLoadRequests);
		return IManifest::FResult().RetryAfterMilliseconds(100);
	}
	else if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::PastEOS)
	{
		return IManifest::FResult(IManifest::FResult::EType::PastEOS);
	}
	else if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::Gone)
	{
		// This should only be intermittent during a playlist refresh. Try again shortly.
		return IManifest::FResult().RetryAfterMilliseconds(100);
	}
	else if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::BadType)
	{
		// This representation has now been disabled. Try again as soon as possible, which should pick a different representation then
		// unless the problem was that fatal that an error has been posted.
		return IManifest::FResult().RetryAfterMilliseconds(0);
	}
	else if (SearchResult == FManifestDASHInternal::FRepresentation::ESearchResult::Found)
	{
		TSharedPtrTS<FStreamSegmentRequestFMP4DASH> SegmentRequest = MakeSharedTS<FStreamSegmentRequestFMP4DASH>();
		SegmentRequest->PlayerLoopState = Current->PlayerLoopState;
		SegmentRequest->StreamType = Current->GetType();
		SegmentRequest->Representation = ActiveRepresentationByType;
		SegmentRequest->AdaptationSet = ActiveAdaptationSetByType;
		SegmentRequest->Period = Period;
		SegmentRequest->PeriodStart = Period->GetStart();
		SegmentRequest->AvailabilityStartTime = TL->GetAnchorTime();
		// If the segment is known to be missing we need to instead insert filler data.
		if (SegmentInfo.bIsMissing)
		{
			SegmentRequest->bInsertFillerData = true;
		}
		// Because we are searching for the next segment we do not want any first access units to be truncated.
		// We keep the current media local AU time for the case where with <SegmentTemplate> addressing we get greatly varying
		// segment durations from the fixed value (up to +/- 50% variation are allowed!) and the current segment did not actually
		// have any access units we wanted to have! In that case it is possible that this new segment would also have some initial
		// access units outside the time we want. By retaining the initial value this is addressed.
		// We do need to translate the value between potentially different timescales and potentially different local media times.
		SegmentInfo.MediaLocalFirstAUTime = FTimeFraction(Current->Segment.MediaLocalFirstAUTime - Current->Segment.PTO, Current->Segment.Timescale).GetAsTimebase(SegmentInfo.Timescale) + SegmentInfo.PTO;
		SegmentRequest->Segment = MoveTemp(SegmentInfo);
		// For a retry request we have to increate the retry count to give up after n failed attempts.
		if (bForRetry)
		{
			SegmentRequest->NumOverallRetries = Current->NumOverallRetries + 1;
		}

		OutSegment = MoveTemp(SegmentRequest);
		return IManifest::FResult(IManifest::FResult::EType::Found);
	}
	else
	{
		check(!"Unhandled search result!");
		return IManifest::FResult();
	}
}


IManifest::FResult FDASHPlayPeriod::GetNextSegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> InCurrentSegment)
{
	check(InCurrentSegment.IsValid());
	// Did the stream reader see a 'lmsg' brand on this segment?
	// If so then this stream has ended and there will not be a next segment.
	const FStreamSegmentRequestFMP4DASH* CurrentRequest = static_cast<const FStreamSegmentRequestFMP4DASH*>(InCurrentSegment.Get());
	if (CurrentRequest->Segment.bSawLMSG)
	{
		return IManifest::FResult(IManifest::FResult::EType::PastEOS);
	}
	return GetNextOrRetrySegment(OutSegment, InCurrentSegment, false);
}

IManifest::FResult FDASHPlayPeriod::GetRetrySegment(TSharedPtrTS<IStreamSegment>& OutSegment, TSharedPtrTS<const IStreamSegment> InCurrentSegment, bool bReplaceWithFillerData)
{
	// To insert filler data we can use the current request over again.
	if (bReplaceWithFillerData)
	{
		const FStreamSegmentRequestFMP4DASH* CurrentRequest = static_cast<const FStreamSegmentRequestFMP4DASH*>(InCurrentSegment.Get());
		TSharedPtrTS<FStreamSegmentRequestFMP4DASH> NewRequest = MakeSharedTS<FStreamSegmentRequestFMP4DASH>();
		*NewRequest = *CurrentRequest;
		NewRequest->bInsertFillerData = true;
		// We treat replacing the segment with filler data as a retry.
		++NewRequest->NumOverallRetries;
		OutSegment = NewRequest;
		return IManifest::FResult(IManifest::FResult::EType::Found);
	}

	return GetNextOrRetrySegment(OutSegment, InCurrentSegment, true);
}


IManifest::FResult FDASHPlayPeriod::GetLoopingSegment(TSharedPtrTS<IStreamSegment>& OutSegment, FPlayerLoopState& InOutLoopState, const TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>& InFinishedSegments, const FPlayStartPosition& StartPosition, IManifest::ESearchType SearchType)
{
	check(!"TODO");
	return IManifest::FResult();
}


void FDASHPlayPeriod::GetSegmentInformation(TArray<FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet, const TSharedPtrTS<IPlaybackAssetRepresentation>& Representation)
{
	OutSegmentInformation.Empty();
	OutAverageSegmentDuration.SetToInvalid();
	if (Representation.IsValid() && AdaptationSet.IsValid())
	{
		static_cast<FManifestDASHInternal::FRepresentation*>(Representation.Get())->GetSegmentInformation(OutSegmentInformation, OutAverageSegmentDuration, CurrentSegment, LookAheadTime, AdaptationSet);
	}
}

TSharedPtrTS<IPlaybackAssetRepresentation> FDASHPlayPeriod::GetRepresentationFromAdaptationByMaxBandwidth(TWeakPtrTS<IPlaybackAssetAdaptationSet> InAdaptationSet, int32 NotExceedingBandwidth)
{
	TSharedPtrTS<IPlaybackAssetAdaptationSet> AS = InAdaptationSet.Pin();
	TSharedPtrTS<IPlaybackAssetRepresentation> BestRepr;
	TSharedPtrTS<IPlaybackAssetRepresentation> WorstRepr;
	if (AS.IsValid())
	{
		int32 BestBW = 0;
		int32 LowestBW = TNumericLimits<int32>::Max();
		int32 NumRepr = AS->GetNumberOfRepresentations();
		for(int32 i=0; i<NumRepr; ++i)
		{
			TSharedPtrTS<IPlaybackAssetRepresentation> Repr = AS->GetRepresentationByIndex(i);
			// Is the representation enabled and usable?
			if (Repr->CanBePlayed())
			{
				if (Repr->GetBitrate() < LowestBW)
				{
					LowestBW = Repr->GetBitrate();
					WorstRepr = Repr;
				}
				if (Repr->GetBitrate() <= NotExceedingBandwidth && Repr->GetBitrate() > BestBW)
				{
					BestBW = Repr->GetBitrate();
					BestRepr= Repr;
				}
			}
		}
		if (!BestRepr.IsValid())
		{
			BestRepr = WorstRepr;
		}
	}
	return BestRepr;
}


/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

namespace
{

template<typename T, typename Validate, typename GetValue>
T GetAttribute(const TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>>& Arr, GetValue Get, Validate IsValid, T Default)
{
	for(int32 i=0; i<Arr.Num(); ++i)
	{
		T v(Get(Arr[i]));
		if (IsValid(v))
		{
			return v;
		}
	}
	return Default;
}

template<typename T, typename Validate, typename GetValue>
T GetAttribute(const TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>>& Arr, GetValue Get, Validate IsValid, T Default)
{
	for(int32 i=0; i<Arr.Num(); ++i)
	{
		T v(Get(Arr[i]));
		if (IsValid(v))
		{
			return v;
		}
	}
	return Default;
}

#define GET_ATTR(Array, GetVal, IsValid, Default) GetAttribute(Array, [](const auto& e){return e->GetVal;}, [](const auto& v){return v.IsValid;}, Default)

}



FManifestDASHInternal::FRepresentation::ESearchResult FManifestDASHInternal::FRepresentation::PrepareSegmentIndex(IPlayerSessionServices* PlayerSessionServices, const TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>>& SegmentBase, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests)
{
	// If the segment index has been requested and is still pending, return right away.
	if (PendingSegmentIndexLoadRequest.IsValid())
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::NeedElement;
	}

	// Is a segment index (still) needed?
	if (!SegmentIndex.IsValid() && bNeedsSegmentIndex)
	{
		// Since this method may only be called with a still valid MPD representation we can pin again and don't need to check if it's still valid.
		TSharedPtrTS<FDashMPD_RepresentationType> MPDRepresentation = Representation.Pin();

		bNeedsSegmentIndex = false;

		// We sort of need an index to figure out segment sizes and durations.
		FString IndexRange = GET_ATTR(SegmentBase, GetIndexRange(), Len(), FString());
		// If the index range is empty check if there is a RepresentationIndex. It should not be to specify the index URL but it may be there to specify the range!
		if (IndexRange.IsEmpty())
		{
			TSharedPtrTS<FDashMPD_URLType> RepresentationIndex = GET_ATTR(SegmentBase, GetRepresentationIndex(), IsValid(), TSharedPtrTS<FDashMPD_URLType>());
			if (RepresentationIndex.IsValid())
			{
				IndexRange = RepresentationIndex->GetRange();
				if (!RepresentationIndex->GetSourceURL().IsEmpty())
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Ignoring <RepresentationIndex> element URL present for <SegmentBase> of representation \"%s\""), *MPDRepresentation->GetID()));
				}
			}
		}
		if (!IndexRange.IsEmpty())
		{
			FString PreferredServiceLocation;
			DashUtils::GetPlayerOption(PlayerSessionServices, PreferredServiceLocation, DASH::OptionKey_CurrentCDN, FString());
			// Get the relevant <BaseURL> elements from the hierarchy.
			TArray<TSharedPtrTS<const FDashMPD_BaseURLType>> OutBaseURLs;
			DASHUrlHelpers::GetAllHierarchyBaseURLs(PlayerSessionServices, OutBaseURLs, MPDRepresentation, *PreferredServiceLocation);
			if (OutBaseURLs.Num() == 0)
			{
				PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" has no <BaseURL> element on any hierarchy level!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
				bIsUsable = false;
				return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
			}
			// Generate the absolute URL
			FString DocumentURL = MPDRepresentation->GetDocumentURL();
			FString URL, RequestHeader;
			if (!DASHUrlHelpers::BuildAbsoluteElementURL(URL, DocumentURL, OutBaseURLs, FString()))
			{
				PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" failed to resolve URL to an absolute URL!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
				bIsUsable = false;
				return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
			}

			// The URL query might need to be changed. Look for the UrlQuery properties.
			TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>> UrlQueries;
			DASHUrlHelpers::GetAllHierarchyUrlQueries(UrlQueries, MPDRepresentation, DASHUrlHelpers::EUrlQueryRequestType::Segment, true);
			FErrorDetail Error = DASHUrlHelpers::ApplyUrlQueries(PlayerSessionServices, DocumentURL, URL, RequestHeader, UrlQueries);
			if (Error.IsSet())
			{
				PostError(PlayerSessionServices, Error);
				bIsUsable = false;
				return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
			}
			URL = DASHUrlHelpers::ApplyAnnexEByteRange(URL, IndexRange, OutBaseURLs);

			// Check with entity cache if the index has been retrieved before.
			IPlayerEntityCache::FCacheItem CachedItem;
			if (PlayerSessionServices->GetEntityCache()->GetCachedEntity(CachedItem, URL, IndexRange))
			{
				// Already cached. Use it.
				SegmentIndex = CachedItem.Parsed14496_12Data;
			}
			else
			{
				// Create the request.
				TSharedPtrTS<FMPDLoadRequestDASH> LoadReq = MakeSharedTS<FMPDLoadRequestDASH>();
				LoadReq->LoadType = FMPDLoadRequestDASH::ELoadType::Segment;
				LoadReq->URL = URL;
				LoadReq->Range = IndexRange;
				if (RequestHeader.Len())
				{
					LoadReq->Headers.Emplace(HTTP::FHTTPHeader({TEXT("MPEG-DASH-Param"), RequestHeader}));
				}
				LoadReq->PlayerSessionServices = PlayerSessionServices;
				LoadReq->XLinkElement = MPDRepresentation;
				LoadReq->CompleteCallback.BindThreadSafeSP(AsShared(), &FManifestDASHInternal::FRepresentation::SegmentIndexDownloadComplete);
				OutRemoteElementLoadRequests.Emplace(LoadReq);
				PendingSegmentIndexLoadRequest = MoveTemp(LoadReq);
				return FManifestDASHInternal::FRepresentation::ESearchResult::NeedElement;
			}
		}
	}
	return FManifestDASHInternal::FRepresentation::ESearchResult::Found;
}


bool FManifestDASHInternal::FRepresentation::PrepareDownloadURLs(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& InOutSegmentInfo, const TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>>& SegmentBase)
{
	TSharedPtrTS<FDashMPD_RepresentationType> MPDRepresentation = Representation.Pin();

	// Get the initialization, if any. If there is none the representation is supposed to be self-initializing.
	TSharedPtrTS<FDashMPD_URLType> Initialization = GET_ATTR(SegmentBase, GetInitialization(), IsValid(), TSharedPtrTS<FDashMPD_URLType>());
	if (Initialization.IsValid())
	{
		InOutSegmentInfo.InitializationURL.Range = Initialization->GetRange();
		if (!Initialization->GetSourceURL().IsEmpty())
		{
			LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Ignoring <Initialization> element URL present for <SegmentBase> of representation \"%s\""), *MPDRepresentation->GetID()));
		}
	}

	FString PreferredServiceLocation;
	DashUtils::GetPlayerOption(PlayerSessionServices, PreferredServiceLocation, DASH::OptionKey_CurrentCDN, FString());
	// Get the relevant <BaseURL> elements from the hierarchy.
	TArray<TSharedPtrTS<const FDashMPD_BaseURLType>> OutBaseURLs;
	DASHUrlHelpers::GetAllHierarchyBaseURLs(PlayerSessionServices, OutBaseURLs, MPDRepresentation, *PreferredServiceLocation);
	if (OutBaseURLs.Num() == 0)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" has no <BaseURL> element on any hierarchy level!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		return false;
	}
	// Generate the absolute URL
	FString DocumentURL = MPDRepresentation->GetDocumentURL();
	FString URL, RequestHeader;
	if (!DASHUrlHelpers::BuildAbsoluteElementURL(URL, DocumentURL, OutBaseURLs, FString()))
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" failed to resolve URL to an absolute URL!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		return false;
	}

	// The URL query might need to be changed. Look for the UrlQuery properties.
	TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>> UrlQueries;
	DASHUrlHelpers::GetAllHierarchyUrlQueries(UrlQueries, MPDRepresentation, DASHUrlHelpers::EUrlQueryRequestType::Segment, true);
	FErrorDetail Error = DASHUrlHelpers::ApplyUrlQueries(PlayerSessionServices, DocumentURL, URL, RequestHeader, UrlQueries);
	if (Error.IsSet())
	{
		PostError(PlayerSessionServices, Error);
		return false;
	}

	if (Initialization.IsValid())
	{
		InOutSegmentInfo.InitializationURL.URL = DASHUrlHelpers::ApplyAnnexEByteRange(URL, InOutSegmentInfo.InitializationURL.Range, OutBaseURLs);
		InOutSegmentInfo.InitializationURL.CDN = PreferredServiceLocation;
		InOutSegmentInfo.InitializationURL.CustomHeader = RequestHeader;
	}

	if (InOutSegmentInfo.FirstByteOffset && InOutSegmentInfo.NumberOfBytes)
	{
		IElectraHttpManager::FParams::FRange r;
		r.SetStart(InOutSegmentInfo.FirstByteOffset);
		r.SetEndIncluding(InOutSegmentInfo.FirstByteOffset + InOutSegmentInfo.NumberOfBytes - 1);
		InOutSegmentInfo.MediaURL.Range = r.GetString();
	}
	InOutSegmentInfo.MediaURL.URL = DASHUrlHelpers::ApplyAnnexEByteRange(URL, InOutSegmentInfo.MediaURL.Range, OutBaseURLs);
	InOutSegmentInfo.MediaURL.CDN = PreferredServiceLocation;
	InOutSegmentInfo.MediaURL.CustomHeader = RequestHeader;


	return true;
}

bool FManifestDASHInternal::FRepresentation::PrepareDownloadURLs(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& InOutSegmentInfo, const TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>>& SegmentTemplate)
{
	TSharedPtrTS<FDashMPD_RepresentationType> MPDRepresentation = Representation.Pin();

	// Get the media template string. While we allow for the initialization segment to be described by an <Initialization> element
	// there is no meaningful way to get the media segment without a template since there is more than just one.
	FString MediaTemplate = GET_ATTR(SegmentTemplate, GetMediaTemplate(), Len(), FString());
	if (MediaTemplate.Len() == 0)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" provides no media template!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		return false;
	}
	// Get the initialization template string. If this is not specified try any <Initialization> elements.
	FString InitializationTemplate = GET_ATTR(SegmentTemplate, GetInitializationTemplate(), Len(), FString());
	if (InitializationTemplate.Len() == 0)
	{
		TSharedPtrTS<FDashMPD_URLType> Initialization = GET_ATTR(SegmentTemplate, GetInitialization(), IsValid(), TSharedPtrTS<FDashMPD_URLType>());
		if (Initialization.IsValid())
		{
			InOutSegmentInfo.InitializationURL.Range = Initialization->GetRange();
			if (Initialization->GetSourceURL().IsEmpty())
			{
				PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" provides no initialization segment!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
				return false;
			}
			// Note: This URL should probably not be using any template strings but I can't find any evidence for this, so just treat it as a template string as well.
			InitializationTemplate = Initialization->GetSourceURL();
		}
	}

	// Substitute template parameters for the media and init segments.
	FString MediaTemplateURL = ApplyTemplateStrings(MediaTemplate, InOutSegmentInfo);
	FString InitTemplateURL = ApplyTemplateStrings(InitializationTemplate, InOutSegmentInfo);

	// Get the preferred CDN and the <BaseURL> and <UrlQueryInfo> elements affecting the URL assembly.
	FString PreferredServiceLocation;
	DashUtils::GetPlayerOption(PlayerSessionServices, PreferredServiceLocation, DASH::OptionKey_CurrentCDN, FString());
	// Get the relevant <BaseURL> elements from the hierarchy.
	TArray<TSharedPtrTS<const FDashMPD_BaseURLType>> OutBaseURLs;
	DASHUrlHelpers::GetAllHierarchyBaseURLs(PlayerSessionServices, OutBaseURLs, MPDRepresentation, *PreferredServiceLocation);
	// The URL query might need to be changed. Look for the UrlQuery properties.
	TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>> UrlQueries;
	DASHUrlHelpers::GetAllHierarchyUrlQueries(UrlQueries, MPDRepresentation, DASHUrlHelpers::EUrlQueryRequestType::Segment, true);

	// Generate the absolute media URL
	FString DocumentURL = MPDRepresentation->GetDocumentURL();
	FString MediaURL, MediaRequestHeader;
	if (!DASHUrlHelpers::BuildAbsoluteElementURL(MediaURL, DocumentURL, OutBaseURLs, MediaTemplateURL))
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" failed to resolve media segment URL to an absolute URL!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		return false;
	}
	FErrorDetail Error = DASHUrlHelpers::ApplyUrlQueries(PlayerSessionServices, DocumentURL, MediaURL, MediaRequestHeader, UrlQueries);
	if (Error.IsSet())
	{
		PostError(PlayerSessionServices, Error);
		return false;
	}

	// And also generate the absolute init segment URL
	FString InitURL, InitRequestHeader;
	if (!DASHUrlHelpers::BuildAbsoluteElementURL(InitURL, DocumentURL, OutBaseURLs, InitTemplateURL))
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" failed to resolve init segment URL to an absolute URL!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		return false;
	}
	Error = DASHUrlHelpers::ApplyUrlQueries(PlayerSessionServices, DocumentURL, InitURL, InitRequestHeader, UrlQueries);
	if (Error.IsSet())
	{
		PostError(PlayerSessionServices, Error);
		return false;
	}

	InOutSegmentInfo.InitializationURL.URL = DASHUrlHelpers::ApplyAnnexEByteRange(InitURL, InOutSegmentInfo.InitializationURL.Range, OutBaseURLs);
	InOutSegmentInfo.InitializationURL.CDN = PreferredServiceLocation;
	InOutSegmentInfo.InitializationURL.CustomHeader = InitRequestHeader;

	if (InOutSegmentInfo.FirstByteOffset && InOutSegmentInfo.NumberOfBytes)
	{
		IElectraHttpManager::FParams::FRange r;
		r.SetStart(InOutSegmentInfo.FirstByteOffset);
		r.SetEndIncluding(InOutSegmentInfo.FirstByteOffset + InOutSegmentInfo.NumberOfBytes - 1);
		InOutSegmentInfo.MediaURL.Range = r.GetString();
	}
	InOutSegmentInfo.MediaURL.URL = DASHUrlHelpers::ApplyAnnexEByteRange(MediaURL, InOutSegmentInfo.MediaURL.Range, OutBaseURLs);
	InOutSegmentInfo.MediaURL.CDN = PreferredServiceLocation;
	InOutSegmentInfo.MediaURL.CustomHeader = MediaRequestHeader;

	return true;
}

FString FManifestDASHInternal::FRepresentation::ApplyTemplateStrings(FString TemplateURL, const FSegmentInformation& InSegmentInfo)
{
	auto PrintWithWidth = [](int64 Value, int32 Width) -> FString
	{
		FString Out = FString::Printf(TEXT("%lld"), Value);
		while(Out.Len() < Width)
		{
			Out = TEXT("0") + Out;
		}
		return Out;
	};

	auto GetFormatWidth = [](FString In) -> int32
	{
		int32 Width = 1;
		if (In.Len() && In[0] == TCHAR('%') && In[In.Len()-1] == TCHAR('d'))
		{
			LexFromString(Width, *In.Mid(1, In.Len()-2));
		}
		return Width;
	};

	FString NewURL;
	while(!TemplateURL.IsEmpty())
	{
		int32 tokenPos = INDEX_NONE;
		if (!TemplateURL.FindChar(TCHAR('$'), tokenPos))
		{
			NewURL.Append(TemplateURL);
			break;
		}
		else
		{
			// Append everything up to the first token.
			if (tokenPos)
			{
				NewURL.Append(TemplateURL.Mid(0, tokenPos));
			}
			// Need to find another token.
			int32 token2Pos = TemplateURL.Find(TEXT("$"), ESearchCase::CaseSensitive, ESearchDir::FromStart, tokenPos+1);
			if (token2Pos != INDEX_NONE)
			{
				FString token(TemplateURL.Mid(tokenPos+1, token2Pos-tokenPos-1));
				TemplateURL.RightChopInline(token2Pos+1, false);
				// An empty token results from "$$" used to insert a single '$'.
				if (token.IsEmpty())
				{
					NewURL.AppendChar(TCHAR('$'));
				}
				// $RepresentationID$ ?
				else if (token.Equals(TEXT("RepresentationID")))
				{
					NewURL.Append(GetUniqueIdentifier());
				}
				// $Number$ ?
				else if (token.StartsWith(TEXT("Number")))
				{
					NewURL.Append(PrintWithWidth(InSegmentInfo.Number, GetFormatWidth(token.Mid(6))));
				}
				// $Bandwidth$ ?
				else if (token.StartsWith(TEXT("Bandwidth")))
				{
					NewURL.Append(PrintWithWidth(GetBitrate(), GetFormatWidth(token.Mid(9))));
				}
				// $Time$ ?
				else if (token.StartsWith(TEXT("Time")))
				{
					NewURL.Append(PrintWithWidth(InSegmentInfo.Time, GetFormatWidth(token.Mid(4))));
				}
				// $SubNumber$ ?
				else if (token.StartsWith(TEXT("SubNumber")))
				{
					NewURL.Append(PrintWithWidth(InSegmentInfo.SubIndex, GetFormatWidth(token.Mid(9))));
				}
				else
				{
					// Unknown. This representation is not to be used!
					NewURL.Empty();
					break;
				}
			}
			else
			{
				// Bad template string. This representation is not to be used!
				NewURL.Empty();
				break;
			}
		}
	}
	return NewURL;


}



FManifestDASHInternal::FRepresentation::ESearchResult FManifestDASHInternal::FRepresentation::FindSegment(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& OutSegmentInfo, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, const FSegmentSearchOption& InSearchOptions)
{
	/*
		Note: We use the DASH-IF-IOP specification and timing model. This is more strict than the general DASH standard and removes ambiguities
		      and otherwise conflicting information.
			  Please refer to https://dashif-documents.azurewebsites.net/Guidelines-TimingModel/master/Guidelines-TimingModel.html
	*/

	// As attributes may be present on any of the MPD hierarchy levels we need to get all these levels locked now.
	TSharedPtrTS<FDashMPD_RepresentationType> MPDRepresentation = Representation.Pin();
	if (!MPDRepresentation.IsValid())
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::Gone;
	}
	TSharedPtrTS<FDashMPD_AdaptationSetType> MPDAdaptation = StaticCastSharedPtr<FDashMPD_AdaptationSetType>(MPDRepresentation->GetParentElement());
	if (!MPDAdaptation.IsValid())
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::Gone;
	}
	TSharedPtrTS<FDashMPD_PeriodType> MPDPeriod = StaticCastSharedPtr<FDashMPD_PeriodType>(MPDAdaptation->GetParentElement());
	if (!MPDPeriod.IsValid())
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::Gone;
	}
	TSharedPtrTS<FDashMPD_MPDType> MPD = StaticCastSharedPtr<FDashMPD_MPDType>(MPDPeriod->GetParentElement());
	if (!MPD.IsValid())
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::Gone;
	}

	// We need to consider 4 types of addressing. <SegmentBase>, <SegmentTemplate>, <SegmentTimeline> and <SegmentList> where the latter is not supported.
	// As per 5.3.9.1:
	//		"Further, if SegmentTemplate or SegmentList is present on one level of the hierarchy, then the other one shall not be present on any lower hierarchy level."
	// implies that if there is a segment list anywhere then it's SegmentList all the way and we can return here.
	if (MPDRepresentation->GetSegmentList().IsValid() || MPDAdaptation->GetSegmentList().IsValid() || MPDPeriod->GetSegmentList().IsValid())
	{
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}

	TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>> SegmentBase({MPDRepresentation->GetSegmentBase(), MPDAdaptation->GetSegmentBase(), MPDPeriod->GetSegmentBase()});
	TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>> SegmentTemplate({MPDRepresentation->GetSegmentTemplate(), MPDAdaptation->GetSegmentTemplate(), MPDPeriod->GetSegmentTemplate()});
	// On representation level there can be at most one of the others.
	if (SegmentBase[0].IsValid() && SegmentTemplate[0].IsValid())
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" must have only one of <SegmentBase> or <SegmentTemplate>!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}
	// It is possible there is neither, which is most common with SegmentTemplate specified on the AdaptationSet.
	else if (!SegmentBase[0].IsValid() && !SegmentTemplate[0].IsValid())
	{
		// Again, there can be at most one of the others.
		if (SegmentBase[1].IsValid() && SegmentTemplate[1].IsValid())
		{
			PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" must only inherit one of <SegmentBase> or <SegmentTemplate> from enclosing AdaptationSet!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
			bIsUsable = false;
			return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
		}
		// And once more, if there is neither go to the Period.
		else if (!SegmentBase[1].IsValid() && !SegmentTemplate[1].IsValid())
		{
			// Again, there can be at most one of the others.
			if (SegmentBase[2].IsValid() && SegmentTemplate[2].IsValid())
			{
				PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" must only inherit one of <SegmentBase> or <SegmentTemplate> from enclosing Period!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
				bIsUsable = false;
				return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
			}
		}
	}
	// Remove empty hierarchy levels
	SegmentBase.Remove(nullptr);
	SegmentTemplate.Remove(nullptr);
	// Nothing? Bad MPD.
	if (SegmentBase.Num() == 0 && SegmentTemplate.Num() == 0)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" does not have one of <SegmentBase> or <SegmentTemplate> anywhere in the MPD hierarchy!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}

	if (SegmentBase.Num())
	{
		return FindSegment_Base(PlayerSessionServices, OutSegmentInfo, OutRemoteElementLoadRequests, InSearchOptions, MPDRepresentation, SegmentBase);
	}
	else
	{
		// Get the segment timeline, if one is used.
		TSharedPtrTS<FDashMPD_SegmentTimelineType> SegmentTimeline = GET_ATTR(SegmentTemplate, GetSegmentTimeline(), IsValid(), TSharedPtrTS<FDashMPD_SegmentTimelineType>());
		if (SegmentTimeline.IsValid())
		{
			return FindSegment_Timeline(PlayerSessionServices, OutSegmentInfo, OutRemoteElementLoadRequests, InSearchOptions, MPDRepresentation, SegmentTemplate, SegmentTimeline);
		}
		else
		{
			return FindSegment_Template(PlayerSessionServices, OutSegmentInfo, OutRemoteElementLoadRequests, InSearchOptions, MPDRepresentation, SegmentTemplate);
		}
	}
}


FManifestDASHInternal::FRepresentation::ESearchResult FManifestDASHInternal::FRepresentation::FindSegment_Base(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& OutSegmentInfo, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, const FSegmentSearchOption& InSearchOptions, const TSharedPtrTS<FDashMPD_RepresentationType>& MPDRepresentation, const TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>>& SegmentBase)
{
	ESearchResult SegIndexResult = PrepareSegmentIndex(PlayerSessionServices, SegmentBase, OutRemoteElementLoadRequests);
	if (SegIndexResult != ESearchResult::Found)
	{
		return SegIndexResult;
	}
	if (!SegmentIndex.IsValid())
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("A segment index is required for Representation \"%s\""), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}
	const IParserISO14496_12::ISegmentIndex* Sidx = SegmentIndex->GetSegmentIndexByIndex(0);
	check(Sidx);	// The existence was already checked for in SegmentIndexDownloadComplete(), but just in case.
	uint32 SidxTimescale = Sidx->GetTimescale();
	if (SidxTimescale == 0)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Timescale of segment index for Representation \"%s\" is invalid!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}

	// The search time is period local time, thus starts at zero. In here it is all about media local time, so we need to map
	// the search time onto the media internal timeline.
	uint64 PTO = GET_ATTR(SegmentBase, GetPresentationTimeOffset(), IsSet(), TMediaOptionalValue<uint64>(0)).Value();
	uint32 MPDTimescale = GET_ATTR(SegmentBase, GetTimescale(), IsSet(), TMediaOptionalValue<uint32>(1)).Value();
	// Since the PTO is specified in the timescale as given in the MPD the timescales of the MPD and the segment index should better match!
	if (PTO && MPDTimescale != SidxTimescale)
	{
		LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation timescale (%u) in MPD is not equal to timescale used in the segment index (%u) for Representation \"%s\"."), MPDTimescale, SidxTimescale, *MPDRepresentation->GetID()));
	}

	// Convert the local media search time to the timescale of the segment index.
	// The PTO (presentation time offset) which maps the internal media time to the zero point of the period must be included as well.
	// Depending on the time scale the conversion may unfortunately incur a small rounding error.
	int64 MediaLocalSearchTime = InSearchOptions.PeriodLocalTime.GetAsTimebase(SidxTimescale) + PTO;
	if (MediaLocalSearchTime < 0)
	{
		MediaLocalSearchTime = 0;
	}
	int64 MediaLocalPeriodEnd = InSearchOptions.PeriodDuration.IsValid() && !InSearchOptions.PeriodDuration.IsInfinity() ? InSearchOptions.PeriodDuration.GetAsTimebase(SidxTimescale) + PTO : TNumericLimits<int64>::Max();
	if (MediaLocalSearchTime >= MediaLocalPeriodEnd)
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::PastEOS;
	}
	// Note: The segment index has only segment durations. If the segments have a baseMediaDecodeTime different from 0 then that value of the first segment
	//       would need to be stored in the EPT (earliest presentation time) here.
	//       The EPT also includes the very first composition time offset, so it may not be zero.
	//       We have to remember that the segment index does not necessarily have access to an edit list as this is stored in the init segment and is not
	//       available at this point in time, so any offsets that would come from an edit list need to have been applied to the EPT here already.
	int64 EarliestPresentationTime = Sidx->GetEarliestPresentationTime();
	int64 CurrentT = EarliestPresentationTime;
	int32 CurrentN, StartNumber=0, EndNumber=Sidx->GetNumEntries();
	int32 CurrentD = 0;
	int64 PreviousT = CurrentT;
	int32 PreviousD = 0;
	int32 PreviousN = 0;
	int64 CurrentOffset = 0;
	int64 PreviousOffset = 0;
	for(CurrentN=0; CurrentN<EndNumber; ++CurrentN)
	{
		const IParserISO14496_12::ISegmentIndex::FEntry& SegmentInfo = Sidx->GetEntry(CurrentN);
		// We do not support hierarchical segment indices!
		if (SegmentInfo.IsReferenceType)
		{
			PostError(PlayerSessionServices, FString::Printf(TEXT("Segment index for Representation \"%s\" must directly reference the media, not another index!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
			bIsUsable = false;
			return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
		}
		else if (SegmentInfo.StartsWithSAP == 0)
		{
			PostError(PlayerSessionServices, FString::Printf(TEXT("Segment index for Representation \"%s\" must have starts_with_sap set!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
			bIsUsable = false;
			return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
		}
		// We require segments to begin with SAP type 1 or 2 (preferably 1)
		else if (SegmentInfo.SAPType != 1 && SegmentInfo.SAPType != 2)
		{
			PostError(PlayerSessionServices, FString::Printf(TEXT("Segment index for Representation \"%s\" must have SAP_type 1 or 2 only!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
			bIsUsable = false;
			return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
		}
		else if (SegmentInfo.SAPDeltaTime != 0)
		{
			LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Segment index for Representation \"%s\" uses SAP_delta_time. This may result in incorrect decoding."), *MPDRepresentation->GetID()));
			bIsUsable = false;
			return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
		}

		CurrentD = SegmentInfo.SubSegmentDuration;
		// Does the segment start on or after the time we're looking for?
		if (CurrentT >= MediaLocalSearchTime)
		{
			// Yes, so we have now found the segment of interest. It is either this one or the previous one.
			if (InSearchOptions.SearchType == IManifest::ESearchType::Closest)
			{
				// If there is a preceeding segment check if its start time is closer
				if (CurrentN > StartNumber)
				{
					if (MediaLocalSearchTime - PreviousT < CurrentT - MediaLocalSearchTime)
					{
						--CurrentN;
						CurrentD = PreviousD;
						CurrentT = PreviousT;
						CurrentOffset = PreviousOffset;
					}
				}
				break;
			}
			else if (InSearchOptions.SearchType == IManifest::ESearchType::After || InSearchOptions.SearchType == IManifest::ESearchType::StrictlyAfter)
			{
				// The 'after' search is used to locate the next segment. For that reason the search time has been adjusted by the caller
				// to be larger than the start time of the preceeding segment.
				// Therefor, since this segment here has a larger or equal start time than the time we are searching for this segment here
				// must be the one 'after'.
				if (CurrentT >= MediaLocalPeriodEnd)
				{
					return FManifestDASHInternal::FRepresentation::ESearchResult::PastEOS;
				}
				break;
			}
			else if (InSearchOptions.SearchType == IManifest::ESearchType::Same || InSearchOptions.SearchType == IManifest::ESearchType::Before)
			{
				// The 'before' search is used to locate the segment containing the search time, which could be
				// either this segment or the preceeding one.
				// The 'same' search is used exactly like 'before'. The segment is required that contains the search time.
				if (CurrentT > MediaLocalSearchTime && CurrentN > StartNumber)
				{
					// Not this segment, must be the preceeding one.
					--CurrentN;
					CurrentD = PreviousD;
					CurrentT = PreviousT;
					CurrentOffset = PreviousOffset;
				}
				break;
			}
			else if (InSearchOptions.SearchType == IManifest::ESearchType::StrictlyBefore)
			{
				// The 'strictlybefore' search is used to locate the segment just before the one the search time is in.
				// The caller is not expected to adjust the time to search for to do that since we are returning
				// the earlier segment if it exists. If not the same segment will be returned.
				if (CurrentN > StartNumber)
				{
					--CurrentN;
					CurrentD = PreviousD;
					CurrentT = PreviousT;
					CurrentOffset = PreviousOffset;
				}
				break;
			}
			else
			{
				PostError(PlayerSessionServices, FString::Printf(TEXT("Unsupported segment search mode!")), ERRCODE_DASH_MPD_INTERNAL);
				bIsUsable = false;
				return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
			}
		}
		PreviousT = CurrentT;
		PreviousN = CurrentN;
		PreviousD = CurrentD;
		PreviousOffset = CurrentOffset;
		CurrentT += CurrentD;
		CurrentOffset += SegmentInfo.Size;
	}

	// Did we find it?
	if (CurrentN < EndNumber && CurrentT < MediaLocalPeriodEnd)
	{
		OutSegmentInfo.Time = CurrentT;
		OutSegmentInfo.PTO = PTO;
		OutSegmentInfo.Duration = CurrentD;
		OutSegmentInfo.Number = CurrentN;
		OutSegmentInfo.NumberOfBytes = Sidx->GetEntry(CurrentN).Size;
		OutSegmentInfo.FirstByteOffset = Sidx->GetFirstOffset() + SegmentIndexRangeStart + SegmentIndexRangeSize + CurrentOffset;
		OutSegmentInfo.MediaLocalFirstAUTime = MediaLocalSearchTime;
		OutSegmentInfo.MediaLocalLastAUTime = MediaLocalPeriodEnd;
		OutSegmentInfo.Timescale = SidxTimescale;
		if (!PrepareDownloadURLs(PlayerSessionServices, OutSegmentInfo, SegmentBase))
		{
			bIsUsable = false;
			return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
		}
		else
		{
			return FManifestDASHInternal::FRepresentation::ESearchResult::Found;
		}
	}
	else
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::PastEOS;
	}

}

FManifestDASHInternal::FRepresentation::ESearchResult FManifestDASHInternal::FRepresentation::FindSegment_Template(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& OutSegmentInfo, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, const FSegmentSearchOption& InSearchOptions, const TSharedPtrTS<FDashMPD_RepresentationType>& MPDRepresentation, const TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>>& SegmentTemplate)
{
	uint64 PTO = GET_ATTR(SegmentTemplate, GetPresentationTimeOffset(), IsSet(), TMediaOptionalValue<uint64>(0)).Value();
	uint32 MPDTimescale = GET_ATTR(SegmentTemplate, GetTimescale(), IsSet(), TMediaOptionalValue<uint32>(1)).Value();
	uint32 StartNumber = GET_ATTR(SegmentTemplate, GetStartNumber(), IsSet(), TMediaOptionalValue<uint32>(1)).Value();
	uint32 EndNumber = GET_ATTR(SegmentTemplate, GetEndNumber(), IsSet(), TMediaOptionalValue<uint32>(~0U)).Value();
	TMediaOptionalValue<uint32> Duration = GET_ATTR(SegmentTemplate, GetDuration(), IsSet(), TMediaOptionalValue<uint32>());
	TMediaOptionalValue<int32> EptDelta = GET_ATTR(SegmentTemplate, GetEptDelta(), IsSet(), TMediaOptionalValue<int32>());

	// The timescale should in all likelihood not be 1. While certainly allowed an accuracy of only one second is more likely to be
	// an oversight when building the MPD.
	if (MPDTimescale == 0)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Timescale for Representation \"%s\" is invalid!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}
	else if (MPDTimescale == 1)
	{
		LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Timescale for Representation \"%s\" is given as 1. Is this intended?"), *MPDRepresentation->GetID()));
	}

	// There needs to be a segment duration here.
	if (!Duration.IsSet() || Duration.Value() == 0)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" has no valid segment duration!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}
	uint32 SegmentDuration = Duration.Value();

	// Get the period local time into media local timescale
	int64 MediaLocalSearchTime = InSearchOptions.PeriodLocalTime.GetAsTimebase(MPDTimescale);
	// If the first media segment does not fall onto the period start there will be an EPT delta that is usually negative.
	// To simplify calculation of the segment index we shift the search time such that 0 would correspond to the EPT.
	int32 EPTdelta = EptDelta.GetWithDefault(0);
	MediaLocalSearchTime -= EPTdelta;
	if (MediaLocalSearchTime < 0)
	{
		MediaLocalSearchTime = 0;
	}

	int64 MediaLocalPeriodDuration = InSearchOptions.PeriodDuration.IsValid() && !InSearchOptions.PeriodDuration.IsInfinity() ? InSearchOptions.PeriodDuration.GetAsTimebase(MPDTimescale) - EPTdelta : TNumericLimits<int64>::Max();
	uint32 MaxSegmentsInPeriod = (MediaLocalPeriodDuration + SegmentDuration - 1) / SegmentDuration;
	// Clamp against the number of segments described by EndNumber.
	// The assumption is that end number is inclusive, so @startNumber == @endNumber means there is 1 segment.
	if (MaxSegmentsInPeriod > (EndNumber - StartNumber)+1)
	{
		MaxSegmentsInPeriod = EndNumber - StartNumber + 1;
	}

	// Now we calculate the number of the segment the search time falls into.
	uint32 SegmentNum = MediaLocalSearchTime / SegmentDuration;
	uint32 SegDurRemainder = MediaLocalSearchTime - SegmentNum * SegmentDuration;

	if (InSearchOptions.SearchType == IManifest::ESearchType::Closest)
	{
		// This is different from <SegmentBase> and <SegmentTimeline> handling since here we are definitely in the segment
		// the search time is in and not possibly the segment thereafter, because we calculated the index through division
		// instead of accumulating durations.
		// Therefor the segment that might be closer to the search time can only be the next one, not the preceeding one.
		if (SegDurRemainder > SegmentDuration / 2 && SegmentNum+1 < MaxSegmentsInPeriod)
		{
			++SegmentNum;
		}
	}
	else if (InSearchOptions.SearchType == IManifest::ESearchType::After || InSearchOptions.SearchType == IManifest::ESearchType::StrictlyAfter)
	{
		// The 'after' search is used to locate the next segment. For that reason the search time has been adjusted by the caller
		// to be larger than the start time of the preceeding segment, but still within the same segment!
		// So we should actually now still be in the same segment as before due to integer truncation when calculating the index
		// through division and the index we want is the next one. However, if due to dumb luck there is no remainder we need to
		// assume the time that got added by the caller (which must not have been zero!) was such that we already landed on the
		// following segment and thus do not increase the index.
		if (SegDurRemainder)
		{
			++SegmentNum;
		}
	}
	else if (InSearchOptions.SearchType == IManifest::ESearchType::Same || InSearchOptions.SearchType == IManifest::ESearchType::Before)
	{
		// The 'before' search is used to locate the segment containing the search time, which could be
		// either this segment or the preceeding one.
		// The 'same' search is used exactly like 'before'. The segment is required that contains the search time.
		// Nothing to do. We are already in that segment.
	}
	else if (InSearchOptions.SearchType == IManifest::ESearchType::StrictlyBefore)
	{
		// The 'strictlybefore' search is used to locate the segment just before the one the search time is in.
		// The caller is not expected to adjust the time to search for to do that since we are returning
		// the earlier segment if it exists. If not the same segment will be returned.
		if (SegmentNum > 0)
		{
			--SegmentNum;
		}
	}
	else
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Unsupported segment search mode!")), ERRCODE_DASH_MPD_INTERNAL);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}

	// Past the last segment?
	if (SegmentNum >= MaxSegmentsInPeriod)
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::PastEOS;
	}

	OutSegmentInfo.Time = PTO + EPTdelta + SegmentNum * SegmentDuration;
	OutSegmentInfo.PTO = PTO;
	OutSegmentInfo.EPTdelta = EPTdelta;
	OutSegmentInfo.Duration = SegmentDuration;
	OutSegmentInfo.Number = StartNumber + SegmentNum;
	OutSegmentInfo.MediaLocalFirstAUTime = MediaLocalSearchTime + PTO;
	OutSegmentInfo.MediaLocalLastAUTime = MediaLocalPeriodDuration + PTO;
	OutSegmentInfo.Timescale = MPDTimescale;
	OutSegmentInfo.bMayBeMissing = SegmentNum + 1 >= MaxSegmentsInPeriod;
	if (!PrepareDownloadURLs(PlayerSessionServices, OutSegmentInfo, SegmentTemplate))
	{
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}
	else
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::Found;
	}
}

FManifestDASHInternal::FRepresentation::ESearchResult FManifestDASHInternal::FRepresentation::FindSegment_Timeline(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& OutSegmentInfo, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, const FSegmentSearchOption& InSearchOptions, const TSharedPtrTS<FDashMPD_RepresentationType>& MPDRepresentation, const TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>>& SegmentTemplate, const TSharedPtrTS<FDashMPD_SegmentTimelineType>& SegmentTimeline)
{
	// Segment timeline must not be empty.
	auto Selements = SegmentTimeline->GetS_Elements();
	if (Selements.Num() == 0)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" has an empty <SegmentTimeline>!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}
	else if (!Selements[0].HaveD)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> does not have mandatory 'd' element!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}

	uint64 PTO = GET_ATTR(SegmentTemplate, GetPresentationTimeOffset(), IsSet(), TMediaOptionalValue<uint64>(0)).Value();
	uint32 MPDTimescale = GET_ATTR(SegmentTemplate, GetTimescale(), IsSet(), TMediaOptionalValue<uint32>(1)).Value();
	uint32 StartNumber = GET_ATTR(SegmentTemplate, GetStartNumber(), IsSet(), TMediaOptionalValue<uint32>(1)).Value();
	uint32 EndNumber = GET_ATTR(SegmentTemplate, GetEndNumber(), IsSet(), TMediaOptionalValue<uint32>(~0U)).Value();
	//TMediaOptionalValue<uint32> Duration = GET_ATTR(SegmentTemplate, GetDuration(), IsSet(), TMediaOptionalValue<uint32>());

	// The timescale should in all likelihood not be 1. While certainly allowed an accuracy of only one second is more likely to be
	// an oversight when building the MPD.
	if (MPDTimescale == 0)
	{
		PostError(PlayerSessionServices, FString::Printf(TEXT("Timescale for Representation \"%s\" is invalid!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
		bIsUsable = false;
		return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
	}
	else if (MPDTimescale == 1)
	{
		LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Timescale for Representation \"%s\" is given as 1. Is this intended?"), *MPDRepresentation->GetID()));
	}

	// Get the period local time into media local timescale and add the PTO.
	int64 MediaLocalSearchTime = InSearchOptions.PeriodLocalTime.GetAsTimebase(MPDTimescale) + PTO;
	if (MediaLocalSearchTime < 0)
	{
		MediaLocalSearchTime = 0;
	}
	int64 MediaLocalPeriodEnd = InSearchOptions.PeriodDuration.IsValid() && !InSearchOptions.PeriodDuration.IsInfinity() ? InSearchOptions.PeriodDuration.GetAsTimebase(MPDTimescale) + PTO : TNumericLimits<int64>::Max();

	// Note: The DASH standard has been extended with a <FailoverContent> element. If this exists we should see if the time we want falls into
	//       content that is not present in this <SegmentTimeline> (failover content does not provide actual content. It gives times for which there is no content available here!)
	//       If the failover content is not on AdaptationSet level we can look for another representation (of lower quality) for which there is content available and then use that one.
	//       Otherwise, knowing that there is no content for any representation we could create a filler segment here.

	int64 CurrentT = Selements[0].HaveT ? Selements[0].T : 0;
	int64 CurrentN = Selements[0].HaveN ? Selements[0].N : StartNumber;
	int32 CurrentR = Selements[0].HaveR ? Selements[0].R : 0;
	int64 CurrentD = Selements[0].D;
	bool bIsCurrentlyAGap = false;

	bool bFound = false;
	// Search for the segment. It is possible that already the first segment has a larger T value than we are searching for.
	if (CurrentT > MediaLocalSearchTime)
	{
		// The first segment starts in the future. What we do now may depend on several factors.
		// If we use it the PTS will jump forward. What happens exactly depends on how the other active representations behave.
		// We could set up a dummy segment request to insert filler data for the duration until the first segment actually starts.
		// This may depend on how big of a gap we are talking about.
		double MissingContentDuration = FTimeValue(CurrentT - MediaLocalSearchTime, MPDTimescale).GetAsSeconds();
		if (MissingContentDuration > 0.1 && !bWarnedAboutTimelineStartGap)
		{
			bWarnedAboutTimelineStartGap = true;
			LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> starts with %.3f seconds of missing content that will be skipped over and might lead to playback issues."), *MPDRepresentation->GetID(), MissingContentDuration));
		}
		bFound = true;
	}
	else
	{
		int64 PreviousD = CurrentD;
		int64 PreviousN = CurrentN - 1;			// start with -1 so we can test if the current N is the previous+1 !
		int64 PreviousT = CurrentT - CurrentD;	// same for T
		for(int32 nIndex=0; !bFound && nIndex<Selements.Num(); ++nIndex)
		{
			if (!Selements[nIndex].HaveD)
			{
				PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> does not have mandatory 'd' element!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
				bIsUsable = false;
				return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
			}

			CurrentT = Selements[nIndex].HaveT ? Selements[nIndex].T : CurrentT;
			CurrentN = Selements[nIndex].HaveN ? Selements[nIndex].N : CurrentN;
			CurrentR = Selements[nIndex].HaveR ? Selements[nIndex].R : 0;
			CurrentD = Selements[nIndex].D;

			if (CurrentD == 0)
			{
				PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> has an entry with 'd'=0, which is invalid."), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
				bIsUsable = false;
				return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
			}

			// There is a conflict in the DASH standard in that the S@n element is an unsignedLong but both @startNumber and @endNumber are only unsignedInt.
			if (CurrentN > MAX_uint32 && !bWarnedAboutTimelineNumberOverflow)
			{
				bWarnedAboutTimelineNumberOverflow = true;
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> 'n' value exceeds unsignedInt (32 bits)."), *MPDRepresentation->GetID()));
			}

			// Warn if explicit numbering results in a gap or overlap. We do nothing besides warn about this.
			if (CurrentN != PreviousN+1)
			{
				if (!bWarnedAboutInconsistentNumbering)
				{
					bWarnedAboutInconsistentNumbering = true;
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> 'n' value %lld is not the expected %d. This may cause playback issues"), *MPDRepresentation->GetID(), (long long int)CurrentN, (long long int)PreviousN+1));
				}
			}

			bIsCurrentlyAGap = false;
			// Check for timeline gaps or overlaps.
			int64 ExpectedT = PreviousT + PreviousD;
			if (CurrentT != ExpectedT)
			{
				// There could be an actual gap in the timeline due to a missing segment, which is probably the most common cause.
				// Another reason could be that a preceeding entry was using 'r'=-1 to repeat until the new 't' but the repeated 'd' value
				// does not result in hitting the new 't' value exactly.
				// It is also possible that the 't' value goes backwards a bit, overlapping with the preceeding segment.
				// In general it is also possible for there to be marginal rounding errors in the encoder pipeline somewhere, so small
				// enough discrepancies we will simply ignore.
				if (FTimeValue(Utils::AbsoluteValue(CurrentT - ExpectedT), MPDTimescale).GetAsMilliseconds() >= 20)
				{
					// An overlap (going backwards in time) we merely log a warning for. There is not a whole lot we can do about this.
					if (CurrentT < ExpectedT)
					{
						if (!bWarnedAboutTimelineOverlap)
						{
							bWarnedAboutTimelineOverlap = true;
							LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> 't' value %lld overlaps with preceeding segment (ends at %lld). This may cause playback issues"), *MPDRepresentation->GetID(), (long long int)CurrentT, (long long int)ExpectedT));
						}
					}
					else
					{
						// Since we do not support <FailoverContent> - and there is no guarantee that it even exists - and we also do not support
						// switching to a different representation - mostly because we have to assume the <SegmentTimeline> exists on
						// the AdaptationSet and therefor applies to all representations equally so there is no point - we need to get over this
						// gap by creating a filler data request.
						// To do this we adjust the current values to what is missing and take note for this iteration that it is missing.
						// Should we find the search time to fall into this missing range the request will be set up accordingly.
						CurrentD = CurrentT - ExpectedT;
						CurrentT = ExpectedT;
						--CurrentN;
						CurrentR = 0;
						bIsCurrentlyAGap = true;
						// We need to repeat this index!
						--nIndex;
					}
				}
			}

			if (CurrentR < 0)
			{
				// Limit the repeat count to where we are going to end.
				// This is either the next element that is required to have a 't', if it exists, or the end of the period.
				// In case the period has no end this is limited to the AvailabilityEndTime of the MPD.
				int64 EndTime = MediaLocalPeriodEnd;
				if (nIndex+1 < Selements.Num())
				{
					if (!Selements[nIndex+1].HaveT)
					{
						if (!bWarnedAboutTimelineNoTAfterNegativeR)
						{
							bWarnedAboutTimelineNoTAfterNegativeR = true;
							LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> element following after a 'r'=-1 repeat count does not have a new 't' value!"), *MPDRepresentation->GetID()));
						}
					}
					else
					{
						EndTime = Selements[nIndex + 1].T;
					}
				}

				CurrentR = (EndTime - CurrentT + CurrentD - 1) / CurrentD - 1;

				if (EndTime == TNumericLimits<int64>::Max())
				{
					PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> repeats until infinity as last period is open-ended which is not currently supported."), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_INTERNAL);
					bIsUsable = false;
					return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
				}

				if (CurrentR < 0)
				{
					PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> repeat count of -1 failed to resolved to a positive value."), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_INTERNAL);
					bIsUsable = false;
					return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
				}
			}

			while(!bFound && CurrentR >= 0)
			{
				if (CurrentT >= MediaLocalSearchTime)
				{
					bFound = true;
					// If this segment consists of subsegments we fail. This is not currently supported.
					if (Selements[nIndex].HaveK)
					{
						PostError(PlayerSessionServices, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> uses 'k' element which is not currently supported!"), *MPDRepresentation->GetID()), ERRCODE_DASH_MPD_BAD_REPRESENTATION);
						bIsUsable = false;
						return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
					}

					if (InSearchOptions.SearchType == IManifest::ESearchType::Closest)
					{
						if (CurrentN > StartNumber)
						{
							if (MediaLocalSearchTime - PreviousT < CurrentT - MediaLocalSearchTime)
							{
								--CurrentN;
								CurrentD = PreviousD;
								CurrentT = PreviousT;
							}
						}
						break;
					}
					else if (InSearchOptions.SearchType == IManifest::ESearchType::After || InSearchOptions.SearchType == IManifest::ESearchType::StrictlyAfter)
					{
						// The 'after' search is used to locate the next segment. For that reason the search time has been adjusted by the caller
						// to be larger than the start time of the preceeding segment.
						// Therefor, since this segment here has a larger or equal start time than the time we are searching for this segment here
						// must be the one 'after'.
						if (CurrentT >= MediaLocalPeriodEnd)
						{
							return FManifestDASHInternal::FRepresentation::ESearchResult::PastEOS;
						}
						break;
					}
					else if (InSearchOptions.SearchType == IManifest::ESearchType::Same || InSearchOptions.SearchType == IManifest::ESearchType::Before)
					{
						// The 'before' search is used to locate the segment containing the search time, which could be
						// either this segment or the preceeding one.
						// The 'same' search is used exactly like 'before'. The segment is required that contains the search time.
						if (CurrentT > MediaLocalSearchTime && CurrentN > StartNumber)
						{
							// Not this segment, must be the preceeding one.
							--CurrentN;
							CurrentD = PreviousD;
							CurrentT = PreviousT;
						}
						break;
					}
					else if (InSearchOptions.SearchType == IManifest::ESearchType::StrictlyBefore)
					{
						// The 'strictlybefore' search is used to locate the segment just before the one the search time is in.
						// The caller is not expected to adjust the time to search for to do that since we are returning
						// the earlier segment if it exists. If not the same segment will be returned.
						if (CurrentN > StartNumber)
						{
							--CurrentN;
							CurrentD = PreviousD;
							CurrentT = PreviousT;
						}
						break;
					}
					else
					{
						PostError(PlayerSessionServices, FString::Printf(TEXT("Unsupported segment search mode!")), ERRCODE_DASH_MPD_INTERNAL);
						bIsUsable = false;
						return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
					}
				}

				if (bFound)
				{
					break;
				}

				PreviousT = CurrentT;
				PreviousN = CurrentN;
				PreviousD = CurrentD;
				CurrentT += CurrentD;
				++CurrentN;
				--CurrentR;
			}
		}
	}

	// Did we find it?
	if (bFound && CurrentT < MediaLocalPeriodEnd)
	{
		OutSegmentInfo.Time = CurrentT;
		OutSegmentInfo.PTO = PTO;
		OutSegmentInfo.Duration = CurrentD;
		OutSegmentInfo.Number = CurrentN;
		OutSegmentInfo.MediaLocalFirstAUTime = MediaLocalSearchTime;
		OutSegmentInfo.MediaLocalLastAUTime = MediaLocalPeriodEnd;
		OutSegmentInfo.Timescale = MPDTimescale;
		OutSegmentInfo.bMayBeMissing = CurrentT + CurrentD >= MediaLocalPeriodEnd;
		if (bIsCurrentlyAGap)
		{
			OutSegmentInfo.bMayBeMissing = true;
			OutSegmentInfo.bIsMissing = true;
			LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation \"%s\" <SegmentTimeline> gap encountered for needed 't' value of %lld. Replacing with an empty filler segment."), *MPDRepresentation->GetID(), (long long int)CurrentT));
		}
		if (!PrepareDownloadURLs(PlayerSessionServices, OutSegmentInfo, SegmentTemplate))
		{
			bIsUsable = false;
			return FManifestDASHInternal::FRepresentation::ESearchResult::BadType;
		}
		else
		{
			return FManifestDASHInternal::FRepresentation::ESearchResult::Found;
		}
	}
	else
	{
		return FManifestDASHInternal::FRepresentation::ESearchResult::PastEOS;
	}
}



void FManifestDASHInternal::FRepresentation::GetSegmentInformation(TArray<IManifest::IPlayPeriod::FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet)
{
	FTimeValue TimeToGo(LookAheadTime);
	FTimeValue FixedSegmentDuration = FTimeValue(FTimeValue::MillisecondsToHNS(2000));
	const int32 FixedBitrate = GetBitrate();
	const FStreamSegmentRequestFMP4DASH* CurrentRequest = static_cast<const FStreamSegmentRequestFMP4DASH*>(CurrentSegment.Get());

	// This is the same as in FindSegment(), only with no error checking since this method here is not critical.
	TSharedPtrTS<FDashMPD_RepresentationType> MPDRepresentation = Representation.Pin();
	if (MPDRepresentation.IsValid())
	{
		TSharedPtrTS<FDashMPD_AdaptationSetType> MPDAdaptation = StaticCastSharedPtr<FDashMPD_AdaptationSetType>(MPDRepresentation->GetParentElement());
		if (MPDAdaptation.IsValid())
		{
			TSharedPtrTS<FDashMPD_PeriodType> MPDPeriod = StaticCastSharedPtr<FDashMPD_PeriodType>(MPDAdaptation->GetParentElement());
			if (MPDPeriod.IsValid())
			{
				TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>> SegmentBase({MPDRepresentation->GetSegmentBase(), MPDAdaptation->GetSegmentBase(), MPDPeriod->GetSegmentBase()});
				TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>> SegmentTemplate({MPDRepresentation->GetSegmentTemplate(), MPDAdaptation->GetSegmentTemplate(), MPDPeriod->GetSegmentTemplate()});
				SegmentBase.Remove(nullptr);
				SegmentTemplate.Remove(nullptr);
				if (SegmentBase.Num())
				{
					TSharedPtrTS<const IParserISO14496_12> SI = SegmentIndex;
					// If the segment index on this representation is not there we look for any segment index of another representation.
					// Since they need to be segmented the same we can at least use the segment durations from there.
					bool bExact = true;
					if (!SI.IsValid())
					{
						bExact = false;
						const FManifestDASHInternal::FAdaptationSet* ParentAdaptation = static_cast<const FManifestDASHInternal::FAdaptationSet*>(AdaptationSet.Get());
						for(int32 nR=0; nR<ParentAdaptation->GetNumberOfRepresentations(); ++nR)
						{
							FRepresentation* Rep = static_cast<FRepresentation*>(ParentAdaptation->GetRepresentationByIndex(nR).Get());
							if (Rep->SegmentIndex.IsValid())
							{
								SI = Rep->SegmentIndex;
								break;
							}
						}
					}
					if (SI.IsValid())
					{
						const IParserISO14496_12::ISegmentIndex* Sidx = SI->GetSegmentIndexByIndex(0);
						if (Sidx)
						{
							uint32 SidxTimescale = Sidx->GetTimescale();
							if (SidxTimescale)
							{
								int64 LocalSearchTime = CurrentRequest ? FTimeValue(CurrentRequest->Segment.Time + CurrentRequest->Segment.Duration, CurrentRequest->Segment.Timescale).GetAsTimebase(SidxTimescale) : 0;
								int64 CurrentT = 0;
								for(int32 nI=0, nIMax=Sidx->GetNumEntries(); nI<nIMax && TimeToGo>FTimeValue::GetZero(); ++nI)
								{
									const IParserISO14496_12::ISegmentIndex::FEntry& SegmentInfo = Sidx->GetEntry(nI);
									if (CurrentT >= LocalSearchTime)
									{
										FTimeValue sd(SegmentInfo.SubSegmentDuration, SidxTimescale);
										int64 ss = bExact ? SegmentInfo.Size : (int64)(FixedBitrate * sd.GetAsSeconds() / 8);
										OutSegmentInformation.Emplace(IManifest::IPlayPeriod::FSegmentInformation({sd, ss}));
										TimeToGo -= sd;
									}
									CurrentT += SegmentInfo.SubSegmentDuration;
								}
							}
						}
					}
				}
				else if (SegmentTemplate.Num())
				{
					TSharedPtrTS<FDashMPD_SegmentTimelineType> SegmentTimeline = GET_ATTR(SegmentTemplate, GetSegmentTimeline(), IsValid(), TSharedPtrTS<FDashMPD_SegmentTimelineType>());
					if (SegmentTimeline.IsValid())
					{
						uint32 MPDTimescale = GET_ATTR(SegmentTemplate, GetTimescale(), IsSet(), TMediaOptionalValue<uint32>(1)).Value();
						auto Selements = SegmentTimeline->GetS_Elements();
						if (MPDTimescale)
						{
							int64 LocalSearchTime = CurrentRequest ? FTimeValue(CurrentRequest->Segment.Time + CurrentRequest->Segment.Duration, CurrentRequest->Segment.Timescale).GetAsTimebase(MPDTimescale) : 0;
							int64 CurrentT = 0;
							for(int32 nI=0, nIMax=Selements.Num(); nI<nIMax && TimeToGo>FTimeValue::GetZero(); ++nI)
							{
								FTimeValue sd(Selements[nI].D, MPDTimescale);
								int64 ss = (int64)(FixedBitrate * sd.GetAsSeconds() / 8);
								int32 R = Selements[nI].R;
								// Note: If the repeat count is negative here then we don't care. We just repeat the same element until we are done.
								//       There is no real need to be that precise to complicate the logic here.
								//       We also do not care if there are gaps or overlaps in the timeline.
								if (R < 0)
								{
									R = MAX_int32;
								}
								for(int32 nJ=R; nJ>=0  && TimeToGo>FTimeValue::GetZero(); --nJ)
								{
									if (CurrentT >= LocalSearchTime)
									{
										OutSegmentInformation.Emplace(IManifest::IPlayPeriod::FSegmentInformation({sd, ss}));
										TimeToGo -= sd;
									}
									CurrentT += Selements[nI].D;
								}
							}
						}
					}
					else
					{
						// Plain SegmentTemplate is trivial in that we have a fixed duration and that's it.
						TMediaOptionalValue<uint32> Duration = GET_ATTR(SegmentTemplate, GetDuration(), IsSet(), TMediaOptionalValue<uint32>());
						uint32 MPDTimescale = GET_ATTR(SegmentTemplate, GetTimescale(), IsSet(), TMediaOptionalValue<uint32>(1)).Value();
						if (Duration.IsSet() && Duration.Value() && MPDTimescale)
						{
							FixedSegmentDuration.SetFromND(Duration.Value(), MPDTimescale);
						}
					}
				}
			}
		}
	}
	// Nothing we could get from the actual representation. Cook up some default values.
	int64 FixedSegmentSize = static_cast<int64>(FixedBitrate * (FixedSegmentDuration.GetAsSeconds() / 8));
	while(TimeToGo > FTimeValue::GetZero())
	{
		OutSegmentInformation.Emplace(IManifest::IPlayPeriod::FSegmentInformation({FixedSegmentDuration, FixedSegmentSize}));
		TimeToGo -= FixedSegmentDuration;
	}
	// Set up average duration.
	if (OutSegmentInformation.Num())
	{
		OutAverageSegmentDuration = (LookAheadTime - TimeToGo) / OutSegmentInformation.Num();
	}
}



void FManifestDASHInternal::FRepresentation::SegmentIndexDownloadComplete(TSharedPtrTS<FMPDLoadRequestDASH> LoadRequest, bool bSuccess)
{
	bool bOk = false;
	if (bSuccess)
	{
		check(LoadRequest.IsValid() && LoadRequest->Request.IsValid());
		if (LoadRequest->OwningManifest.Pin().IsValid() && LoadRequest->XLinkElement == Representation)
		{
			DashUtils::FMP4SidxBoxReader BoxReader;
			BoxReader.SetParseData(LoadRequest->Request->GetResponseBuffer());
			TSharedPtrTS<IParserISO14496_12> Index = IParserISO14496_12::CreateParser();
			UEMediaError parseError = Index->ParseHeader(&BoxReader, &BoxReader, LoadRequest->PlayerSessionServices);
			if (parseError == UEMEDIA_ERROR_OK || parseError == UEMEDIA_ERROR_END_OF_STREAM)
			{
				if (Index->PrepareTracks(nullptr) == UEMEDIA_ERROR_OK && Index->GetNumberOfSegmentIndices() > 0)
				{
					IElectraHttpManager::FParams::FRange r;
					r.Set(LoadRequest->Range);
					if (r.IsSet())
					{
						check(r.GetStart() >= 0);
						check(r.GetEndIncluding() > 0);
						SegmentIndexRangeStart = r.GetStart();
						SegmentIndexRangeSize = r.GetEndIncluding() + 1 - SegmentIndexRangeStart;
					}
					SegmentIndex = MoveTemp(Index);
					bOk = true;
					// Add this to the entity cache in case it needs to be retrieved again.
					IPlayerEntityCache::FCacheItem ci;
					ci.URL = LoadRequest->URL;
					ci.Range = LoadRequest->Range;
					ci.Parsed14496_12Data = SegmentIndex;
					LoadRequest->PlayerSessionServices->GetEntityCache()->CacheEntity(ci);
				}
				else
				{
					LogMessage(LoadRequest->PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation segment index is invalid. Marking representation as unusable.")));
				}
			}
			else
			{
				LogMessage(LoadRequest->PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation segment index parsing failed. Marking representation as unusable.")));
			}
		}
	}
	else
	{
		LogMessage(LoadRequest->PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Representation segment index download failed. Marking representation as unusable.")));
	}
	bIsUsable = bOk;
	PendingSegmentIndexLoadRequest.Reset();
}

} // namespace Electra


