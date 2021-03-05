// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PlayerCore.h"

#include "MPDElementsDASH.h"

#include "Player/AdaptiveStreamingPlayerResourceRequest.h"

namespace Electra
{
class FManifestDASHInternal;
class IParserISO14496_12;


struct FMPDLoadRequestDASH : public IHTTPResourceRequestObject
{
	DECLARE_DELEGATE_TwoParams(FOnRequestCompleted, TSharedPtrTS<FMPDLoadRequestDASH> /*Request*/, bool /*bSuccess*/);

	enum class ELoadType
	{
		MPD,
		MPDUpdate,
		XLink_Period,
		XLink_AdaptationSet,
		XLink_EventStream,
		XLink_SegmentList,
		XLink_URLQuery,
		XLink_InitializationSet,
		Callback,
		Segment
	};
	const TCHAR* const GetRequestTypeName() const
	{
		switch(LoadType)
		{
			case ELoadType::MPD:					return TEXT("MPD");
			case ELoadType::MPDUpdate:				return TEXT("MPD update");
			case ELoadType::XLink_Period:			return TEXT("remote Period");
			case ELoadType::XLink_AdaptationSet:	return TEXT("remote AdaptationSet");
			case ELoadType::XLink_EventStream:		return TEXT("remote EventStream");
			case ELoadType::XLink_SegmentList:		return TEXT("remote SegmentList");
			case ELoadType::XLink_URLQuery:			return TEXT("remote URLQueryParam");
			case ELoadType::XLink_InitializationSet:return TEXT("remote InitializationSet");
			case ELoadType::Callback:				return TEXT("Callback");
			case ELoadType::Segment:				return TEXT("Segment");
			default:								return TEXT("<unknown>");
		}
	}

	ELoadType GetLoadType() const
	{
		return LoadType;
	}

	FMPDLoadRequestDASH() : LoadType(ELoadType::MPD) {}
	FString			URL;	// For xlink requests this could be "urn:mpeg:dash:resolve-to-zero:2013" indicating removal of the element.
	FString			Range;
	TArray<HTTP::FHTTPHeader> Headers;
	FTimeValue		ExecuteAtUTC;

	FOnRequestCompleted	CompleteCallback;

	ELoadType		LoadType;
	// XLink specific information to which the remote element applies.
	TWeakPtrTS<IDashMPDElement> XLinkElement;
	// The manifest for which this request is made. Not set for an initial MPD fetch but set for everything else.
	// This allows checking if - after a dynamic MPD update - the requesting MPD is still valid and in use.
	TWeakPtrTS<FManifestDASHInternal> OwningManifest;

	IPlayerSessionServices* PlayerSessionServices = nullptr;

	const HTTP::FConnectionInfo* GetConnectionInfo() const
	{
		return Request.IsValid() ? Request->GetConnectionInfo() : nullptr;
	}
	FString GetErrorDetail() const
	{
		return GetConnectionInfo() ? GetConnectionInfo()->StatusInfo.ErrorDetail.GetMessage() : FString();
	}

	TSharedPtrTS<FHTTPResourceRequest>	Request;
	int32			Attempt = 0;
};



namespace DASHUrlHelpers
{
	bool IsAbsoluteURL(const FString& URL);
	void GetAllHierarchyBaseURLs(IPlayerSessionServices* InPlayerSessionServices, TArray<TSharedPtrTS<const FDashMPD_BaseURLType>>& OutBaseURLs, TSharedPtrTS<const IDashMPDElement> StartingAt, const TCHAR* PreferredServiceLocation);
	enum class EUrlQueryRequestType
	{
		Segment,
		Xlink,
		Mpd,
		Callback,
		Chaining,
		Fallback
	};
	void GetAllHierarchyUrlQueries(TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>>& OutUrlQueries, TSharedPtrTS<const IDashMPDElement> StartingAt, EUrlQueryRequestType ForRequestType, bool bInclude2014);
	FErrorDetail ApplyUrlQueries(IPlayerSessionServices* PlayerSessionServices, const FString& InMPDUrl, FString& InOutURL, FString& OutRequestHeader, const TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>>& UrlQueries);
	bool BuildAbsoluteElementURL(FString& OutURL, const FString& DocumentURL, const TArray<TSharedPtrTS<const FDashMPD_BaseURLType>>& BaseURLs, const FString& ElementURL);
	FString ApplyAnnexEByteRange(FString InURL, FString InRange, const TArray<TSharedPtrTS<const FDashMPD_BaseURLType>>& OutBaseURLs);
}




class FManifestDASHInternal
{
public:
	enum class EPresentationType
	{
		Static,
		Dynamic
	};

	struct FSegmentInformation
	{
		struct FURL
		{
			FString URL;
			FString Range;
			FString CDN;
			FString CustomHeader;
		};
		FURL InitializationURL;
		FURL MediaURL;
		FTimeValue AvailabilityTimeOffset;			//!< ATO, if applicable.
		int64 Time = 0;								//!< Time value T in timescale units without PTO, EPT or ELST applied
		int64 PTO = 0;								//!< PresentationTimeOffset
		int64 EPTdelta = 0;
		int64 Duration = 0;							//!< Duration of the segment. Not necessarily exact if <SegmentTemplate> is used).
		int64 Number = 0;							//!< Index of the segment.
		int64 SubIndex = 0;							//!< Subsegment index
		int64 NumberOfBytes = 0;
		int64 FirstByteOffset = 0;
		int64 MediaLocalFirstAUTime = 0;			//!< Time of the first AU to use in this segment in media local time
		int64 MediaLocalLastAUTime = 0;				//!< Time at which the last AU to use in thie segment ends in media local time
		uint32 Timescale = 0;						//!< Local media timescale
		bool bMayBeMissing = false;					//!< true if the last segment in <SegmentTemplate> that might not exist.
		bool bIsMissing = false;					//!< Set to true if known to be missing.
		bool bSawLMSG = false;						//!< Will be set to true by the stream reader if the 'lmsg' brand was found.
	};

	struct FSegmentSearchOption
	{
		FTimeValue PeriodLocalTime;
		FTimeValue PeriodDuration;
		int64 Number = -1;							//!< Explicit segment number
		int64 Time = -1;							//!< Explicit segment time
		IManifest::ESearchType SearchType = IManifest::ESearchType::Closest;
		int64 RequestID = 0;						//!< Sequential request ID across all segments during playback, needed to re-resolve potential UrlQueryInfo xlinks.
	};

	class FRepresentation : public IPlaybackAssetRepresentation, public TSharedFromThis<FRepresentation, ESPMode::ThreadSafe>
	{
	public:
		FRepresentation()
		{ }
		virtual ~FRepresentation() = default;

		enum ESearchResult
		{
			Found,									//!< Found
			PastEOS,								//!< Media is shorter than the period and no segment exists for the specified local time.
			NeedElement,							//!< An additional element is needed that must be loaded first. Execute all returned load requests and try again later.
			BadType,								//!< Representation is bad for some reason, most likely because it uses <SegmentList> addressing which is not supported.
			Gone									//!< Underlying MPD Representation (held by a weak pointer) has gone and the representation is no longer accessible.
		};
		
		ESearchResult FindSegment(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& OutSegmentInfo, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, const FSegmentSearchOption& InSearchOptions);

		void GetSegmentInformation(TArray<IManifest::IPlayPeriod::FSegmentInformation>& OutSegmentInformation, FTimeValue& OutAverageSegmentDuration, TSharedPtrTS<const IStreamSegment> CurrentSegment, const FTimeValue& LookAheadTime, const TSharedPtrTS<IPlaybackAssetAdaptationSet>& AdaptationSet);


		//----------------------------------------------
		// Methods from IPlaybackAssetRepresentation
		//
		virtual FString GetUniqueIdentifier() const override
		{
			TSharedPtrTS<FDashMPD_RepresentationType> MPDRepresentation = Representation.Pin();
			return MPDRepresentation.IsValid() ? MPDRepresentation->GetID() : FString(TEXT(" invalid name "));
		}
		virtual const FStreamCodecInformation& GetCodecInformation() const override
		{
			return CodecInfo;
		}
		virtual int32 GetBitrate() const override
		{
			TSharedPtrTS<FDashMPD_RepresentationType> MPDRepresentation = Representation.Pin();
			if (MPDRepresentation.IsValid())
			{
				return (int32) MPDRepresentation->GetBandwidth();
			}
			// Return max possible value to make sure this will never get chosen.
			return TNumericLimits<int32>::Max();
		}
		virtual int32 GetQualityIndex() const override
		{
			return QualityIndex;
		}
		virtual bool CanBePlayed() const override
		{
			return bIsEnabled && bIsUsable;
		}

	private:
		ESearchResult PrepareSegmentIndex(IPlayerSessionServices* PlayerSessionServices, const TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>>& SegmentBase, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests);

		ESearchResult FindSegment_Base(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& OutSegmentInfo, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, const FSegmentSearchOption& InSearchOptions, const TSharedPtrTS<FDashMPD_RepresentationType>& MPDRepresentation, const TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>>& SegmentBase);
		ESearchResult FindSegment_Template(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& OutSegmentInfo, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, const FSegmentSearchOption& InSearchOptions, const TSharedPtrTS<FDashMPD_RepresentationType>& MPDRepresentation, const TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>>& SegmentTemplate);
		ESearchResult FindSegment_Timeline(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& OutSegmentInfo, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, const FSegmentSearchOption& InSearchOptions, const TSharedPtrTS<FDashMPD_RepresentationType>& MPDRepresentation, const TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>>& SegmentTemplate, const TSharedPtrTS<FDashMPD_SegmentTimelineType>& SegmentTimeline);

		bool PrepareDownloadURLs(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& InOutSegmentInfo, const TArray<TSharedPtrTS<FDashMPD_SegmentBaseType>>& SegmentBase);
		bool PrepareDownloadURLs(IPlayerSessionServices* PlayerSessionServices, FSegmentInformation& InOutSegmentInfo, const TArray<TSharedPtrTS<FDashMPD_SegmentTemplateType>>& SegmentBase);
		FString ApplyTemplateStrings(FString TemplateURL, const FSegmentInformation& InSegmentInfo);

		void SegmentIndexDownloadComplete(TSharedPtrTS<FMPDLoadRequestDASH> Request, bool bSuccess);
		friend class FManifestDASHInternal;
		TWeakPtrTS<FDashMPD_RepresentationType> Representation;
		FStreamCodecInformation CodecInfo;
		int32 QualityIndex = -1;
		bool bIsUsable = false;
		bool bIsEnabled = true;
		bool bWarnedAboutTimelineStartGap = false;
		bool bWarnedAboutTimelineNoTAfterNegativeR = false;
		bool bWarnedAboutTimelineNumberOverflow = false;
		bool bWarnedAboutInconsistentNumbering = false;
		bool bWarnedAboutTimelineOverlap = false;
		//
		bool bNeedsSegmentIndex = true;
		TSharedPtrTS<const IParserISO14496_12> SegmentIndex;
		int64 SegmentIndexRangeStart = 0;
		int64 SegmentIndexRangeSize = 0;
		TSharedPtrTS<FMPDLoadRequestDASH> PendingSegmentIndexLoadRequest;
	};

	class FAdaptationSet : public IPlaybackAssetAdaptationSet
	{
	public:
		FAdaptationSet()
		{ }
		virtual ~FAdaptationSet() = default;

		const FStreamCodecInformation& GetCodec() const								{ return Codec; }
		const TArray<TSharedPtrTS<FRepresentation>>& GetRepresentations() const		{ return Representations; }
		const TArray<FString>& GetRoles() const										{ return Roles; }
		const TArray<FString>& GetAccessibilities() const							{ return Accessibilities; }
		const FTimeFraction& GetPAR() const											{ return PAR; }
		//const FString& GetLanguage() const											{ return Language; }
		int32 GetMaxBandwidth() const												{ return MaxBandwidth; }
		bool GetIsUsable() const													{ return bIsUsable; }

		//----------------------------------------------
		// Methods from IPlaybackAssetAdaptationSet
		//
		virtual FString GetUniqueIdentifier() const override
		{
			return FString::Printf(TEXT("%d"), IndexOfSelf);
		}
		virtual FString GetListOfCodecs() const override
		{
			return Codec.GetCodecSpecifierRFC6381();
		}
		virtual FString GetLanguage() const override
		{
			return Language;
		}
		virtual int32 GetNumberOfRepresentations() const override
		{
			return Representations.Num();
		}
		virtual TSharedPtrTS<IPlaybackAssetRepresentation> GetRepresentationByIndex(int32 RepresentationIndex) const override
		{
			if (RepresentationIndex < Representations.Num())
			{
				return Representations[RepresentationIndex];
			}
			return nullptr;
		}
		virtual TSharedPtrTS<IPlaybackAssetRepresentation> GetRepresentationByUniqueIdentifier(const FString& UniqueIdentifier) const override
		{
			for(int32 i=0; i<Representations.Num(); ++i)
			{
				if (Representations[i]->GetUniqueIdentifier().Equals(UniqueIdentifier))
				{
					return Representations[i];
				}
			}
			return nullptr;
		}
	private:
		friend class FManifestDASHInternal;
		TWeakPtrTS<FDashMPD_AdaptationSetType> AdaptationSet;
		FStreamCodecInformation Codec;
		TArray<TSharedPtrTS<FRepresentation>> Representations;
		TArray<FString> Roles;
		TArray<FString> Accessibilities;
		FTimeFraction PAR;
		FString Language;
		int32 MaxBandwidth = 0;
		int32 IndexOfSelf = 0;
		bool bIsUsable = false;
		bool bIsEnabled = true;
	};

	class FPeriod : public ITimelineMediaAsset
	{
	public:
		FPeriod()
		{ }
		virtual ~FPeriod() = default;

		const FString& GetID() const { return ID; }
		const FTimeValue& GetStart() const { return Start; }
		const FTimeValue& GetEnd() const { return End; }
		bool GetIsEarlyPeriod() const { return bIsEarlyPeriod; }
		const TArray<TSharedPtrTS<FAdaptationSet>>& GetAdaptationSets() const { return AdaptationSets; }

		TSharedPtrTS<FDashMPD_PeriodType> GetMPDPeriod() { return Period.Pin(); }

		//----------------------------------------------
		// Methods from ITimelineMediaAsset
		//
		virtual FTimeRange GetTimeRange() const override
		{
			return FTimeRange({Start, End});
		}
		virtual FTimeValue GetDuration() const override
		{
			return Duration;
		}
		virtual FString GetAssetIdentifier() const override
		{
			TSharedPtrTS<FDashMPD_PeriodType> MPDPeriod = Period.Pin();
			return MPDPeriod.IsValid() && MPDPeriod->GetAssetIdentifier().IsValid() ? MPDPeriod->GetAssetIdentifier()->GetValue() : FString();
		}
		virtual FString GetUniqueIdentifier() const override
		{
			return ID;
		}
		virtual int32 GetNumberOfAdaptationSets(EStreamType OfStreamType) const override
		{
			int32 Num=0;
			for(int32 i=0; i<AdaptationSets.Num(); ++i)
			{
				if (AdaptationSets[i]->GetCodec().GetStreamType() == OfStreamType)
				{
					++Num;
				}
			}
			return Num;
		}
		virtual TSharedPtrTS<IPlaybackAssetAdaptationSet> GetAdaptationSetByTypeAndIndex(EStreamType OfStreamType, int32 AdaptationSetIndex) const
		{
			int32 Num=0;
			for(int32 i=0; i<AdaptationSets.Num(); ++i)
			{
				if (AdaptationSets[i]->GetCodec().GetStreamType() == OfStreamType)
				{
					if (Num++ == AdaptationSetIndex)
					{
						return AdaptationSets[i];
					}
				}
			}
			return nullptr;
		}

	private:
		friend class FManifestDASHInternal;
		TWeakPtrTS<FDashMPD_PeriodType> Period;
		FString ID;
		FTimeValue Start;
		FTimeValue End;
		FTimeValue Duration;
		bool bIsEarlyPeriod = false;
		TArray<TSharedPtrTS<FAdaptationSet>> AdaptationSets;
	};

	FErrorDetail Build(IPlayerSessionServices* InPlayerSessionServices, TSharedPtr<FDashMPD_MPDType, ESPMode::ThreadSafe> InMPDRoot, TArray<TWeakPtrTS<IDashMPDElement>> InXLinkElements, const FStreamPreferences& InPreferences, const FParamDict& InOptions);

	FErrorDetail BuildAfterInitialRemoteElementDownload(IPlayerSessionServices* InPlayerSessionServices);

	void GetRemoteElementLoadRequests(TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests)
	{
		OutRemoteElementLoadRequests = PendingRemoteElementLoadRequests;
	}

	FErrorDetail ResolveInitialRemoteElementRequest(IPlayerSessionServices* PlayerSessionServices, TSharedPtrTS<FMPDLoadRequestDASH> RequestResponse, FString XMLResponse, bool bSuccess);

	EPresentationType GetPresentationType() const
	{
		return PresentationType;
	}

	const TArray<TSharedPtrTS<FPeriod>>& GetPeriods() const
	{
		return Periods;
	}

	TSharedPtrTS<const FDashMPD_MPDType> GetMPDRoot() const
	{
		return MPDRoot;
	}

	FErrorDetail PreparePeriodAdaptationSets(IPlayerSessionServices* PlayerSessionServices, TSharedPtrTS<FPeriod> InPeriod, bool bRequestXlink);

	ELECTRA_IMPL_DEFAULT_ERROR_METHODS(DASHManifest);

private:
	FErrorDetail PrepareRemoteElementLoadRequest(IPlayerSessionServices* PlayerSessionServices, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, TWeakPtrTS<IDashMPDElement> ElementWithXLink, int64 RequestID);

	int32 ReplaceElementWithRemoteEntities(TSharedPtrTS<IDashMPDElement> Element, const FDashMPD_RootEntities& NewRootEntities, int64 OldResolveID, int64 NewResolveID);


	TArray<TWeakPtrTS<IDashMPDElement>> RemoteElementsToResolve;
	TArray<TWeakPtrTS<FMPDLoadRequestDASH>> PendingRemoteElementLoadRequests;

	FStreamPreferences Preferences;
	FParamDict Options;

	// The parsed MPD.
	TSharedPtrTS<FDashMPD_MPDType> MPDRoot;

	// Type of the presentation.
	EPresentationType PresentationType;

	TArray<TSharedPtrTS<FPeriod>> Periods;

};


} // namespace Electra

