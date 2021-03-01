// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Player/Manifest.h"
#include "Player/PlaybackTimeline.h"

#include "Player/PlayerSessionServices.h"
#include "Player/PlayerStreamFilter.h"
#include "Player/PlayerEntityCache.h"

#include "ManifestBuilderDASH.h"
#include "ManifestParserDASH.h"
#include "MPDElementsDASH.h"

#include "Utilities/StringHelpers.h"
#include "SynchronizedClock.h"
#include "StreamTypes.h"
#include "ErrorDetail.h"

#include "Utilities/URLParser.h"
#include "Utilities/Utilities.h"
#include "Utilities/TimeUtilities.h"


#define ERRCODE_DASH_MPD_BUILDER_INTERNAL							1
#define ERRCODE_DASH_MPD_BUILDER_UNSUPPORTED_PROFILE				100
#define ERRCODE_DASH_MPD_BUILDER_UNSUPPORTED_ESSENTIAL_PROPERTY		101
#define ERRCODE_DASH_MPD_BUILDER_MISSING_REQUIRED_ATTRIBUTE			102
#define ERRCODE_DASH_MPD_BUILDER_UNREQUIRED_ATTRIBUTE				103
#define ERRCODE_DASH_MPD_BUILDER_XLINK_NOT_SUPPORTED_ON_ELEMENT		104
#define ERRCODE_DASH_MPD_BUILDER_URL_FAILED_TO_RESOLVE				105
#define ERRCODE_DASH_MPD_BUILDER_MISSING_REQUIRED_ELEMENT			106


#define ERRCODE_DASH_MPD_BUILDER_BAD_PERIOD_START					200
#define ERRCODE_DASH_MPD_BUILDER_EARLY_PERIODS_MUST_BE_LAST			201
#define ERRCODE_DASH_MPD_BUILDER_MEDIAPRESENTATIONDURATION_NEEDED	202
#define ERRCODE_DASH_MPD_BUILDER_BAD_PERIOD_DURATION				203


namespace Electra
{

namespace
{
	const TCHAR* const XLinkActuateOnLoad = TEXT("onLoad");
	const TCHAR* const XLinkActuateOnRequest = TEXT("onRequest");
	const TCHAR* const XLinkResolveToZero = TEXT("urn:mpeg:dash:resolve-to-zero:2013");

	const TCHAR* const SchemeHTTP = TEXT("http://");
	const TCHAR* const SchemeHTTPS = TEXT("https://");
	const TCHAR* const SchemeDATA = TEXT("data:");

	const TCHAR* const SupportedProfiles[] =
	{
		TEXT("urn:mpeg:dash:profile:isoff-on-demand:2011"),
		TEXT("urn:mpeg:dash:profile:isoff-live:2011"),
//		TEXT("urn:mpeg:dash:profile:isoff-ext-on-demand:2014"),			// ??
//		TEXT("urn:mpeg:dash:profile:isoff-ext-live:2014"),				// full xlink required
//		TEXT("urn:mpeg:dash:profile:isoff-common:2014"),				// may contain both ext-on-demand and ext-live additions
//		TEXT("urn:mpeg:dash:profile:isoff-broadcast:2015"),				// RandomAccess and Switching support required
		// Possibly add DASH-IF-IOP profiles?
//		TEXT(""),
		// DVB-DASH profiles
		TEXT("urn:dvb:dash:profile:dvb-dash:2014"),
		TEXT("urn:dvb:dash:profile:dvb-dash:isoff-ext-live:2014"),
	};

	const TCHAR* const ScanTypeInterlace = TEXT("interlace");

	const TCHAR* const AudioChannelConfigurationLegacy = TEXT("urn:mpeg:dash:23003:3:audio_channel_configuration:2011");
	const TCHAR* const AudioChannelConfiguration = TEXT("urn:mpeg:mpegB:cicp:ChannelConfiguration");
	const TCHAR* const AudioChannelConfigurationDolby = TEXT("tag:dolby.com,2014:dash:audio_channel_configuration:2011");

	const TCHAR* const DASHRole = TEXT("urn:mpeg:dash:role:2011");

	const TCHAR* const SupportedEssentialProperties[] = 
	{
		TEXT("urn:mpeg:dash:urlparam:2014"),
		TEXT("urn:mpeg:dash:urlparam:2016"),
	};

}

namespace DASHUrlHelpers
{
	ELECTRA_IMPL_DEFAULT_ERROR_METHODS(DASHMPDBuilder);

	bool IsAbsoluteURL(const FString& URL)
	{
		// For simplicities sake we check if the last element starts with 'http://' or 'https://' to determine whether it is
		// an absolute or relative URL. We also allow for data URLs
		return URL.StartsWith(SchemeHTTPS) || URL.StartsWith(SchemeHTTP) || URL.StartsWith(SchemeDATA);
	}

	/**
	 * Collects BaseURL elements beginning at the specified element up the MPD hierarchy.
	 * At most one BaseURL per level is added to the output. If a preferred service location is specified the BaseURL element matching this will
	 * be added. If no BaseURL on that level matching the preferred service location the first BaseURL element is used.
	 * Results are added in reverse hierarchy order, eg. Representation, Adaptation, Period, MPD.
	 */
	void GetAllHierarchyBaseURLs(IPlayerSessionServices* InPlayerSessionServices, TArray<TSharedPtrTS<const FDashMPD_BaseURLType>>& OutBaseURLs, TSharedPtrTS<const IDashMPDElement> StartingAt, const TCHAR* PreferredServiceLocation)
	{
		while(StartingAt.IsValid())
		{
			const TArray<TSharedPtrTS<FDashMPD_BaseURLType>>& ElementBaseURLs = StartingAt->GetBaseURLs();
			if (PreferredServiceLocation == nullptr || *PreferredServiceLocation == TCHAR(0))
			{
				if (ElementBaseURLs.Num())
				{
					OutBaseURLs.Emplace(ElementBaseURLs[0]);
				}
			}
			else
			{
				for(int32 i=ElementBaseURLs.Num()-1; i>=0; --i)
				{
					if (i == 0 || ElementBaseURLs[i]->GetServiceLocation().Equals(PreferredServiceLocation))
					{
						OutBaseURLs.Emplace(ElementBaseURLs[i]);
						break;
					}
				}
			}
			StartingAt = StartingAt->GetParentElement();
		}
	}

	/**
	 * Collects UrlQueryInfo and/or ExtUrlQueryInfo/ExtHttpHeaderInfo elements from EssentialProperty and SupplementalPropery elements
	 * beginning at the specified element up the MPD hierarchy.
	 * At most one element matching the requested type will be returned per hierarchy level in accordance with the specification (see I.2.1).
	 * Results are added in hierarchy order, eg. MPD, Period, Adaptation, Representation
	 */
	void GetAllHierarchyUrlQueries(TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>>& OutUrlQueries, TSharedPtrTS<const IDashMPDElement> StartingAt, EUrlQueryRequestType ForRequestType, bool bInclude2014)
	{
		while(StartingAt.IsValid())
		{
			TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>> UrlQueries;
			// Get the essential and supplemental properties into a single list for processing.
			TArray<TSharedPtrTS<FDashMPD_DescriptorType>> Desc;
			Desc = StartingAt->GetEssentialProperties().FilterByPredicate([bInclude2014](const TSharedPtrTS<FDashMPD_DescriptorType>& d)
				{ return d->GetSchemeIdUri().Equals(TEXT("urn:mpeg:dash:urlparam:2016")) || (bInclude2014 && d->GetSchemeIdUri().Equals(TEXT("urn:mpeg:dash:urlparam:2014"))); });
			Desc.Append(StartingAt->GetSupplementalProperties().FilterByPredicate([bInclude2014](const TSharedPtrTS<FDashMPD_DescriptorType>& d)
				{ return d->GetSchemeIdUri().Equals(TEXT("urn:mpeg:dash:urlparam:2016")) || (bInclude2014 && d->GetSchemeIdUri().Equals(TEXT("urn:mpeg:dash:urlparam:2014"))); }));
			for(int32 i=0; i<Desc.Num(); ++i)
			{
				const TArray<TSharedPtrTS<IDashMPDElement>>& WellKnown = Desc[i]->GetWellKnownDescriptors();
				for(int32 j=0; j<WellKnown.Num(); ++j)
				{
					if (WellKnown[j]->GetElementType() == IDashMPDElement::EType::URLQueryInfo)
					{
						TSharedPtrTS<FDashMPD_UrlQueryInfoType> uq = StaticCastSharedPtr<FDashMPD_UrlQueryInfoType>(WellKnown[j]);
						const TArray<FString>& IncIn = uq->GetIncludeInRequests();
						for(int32 k=0; k<IncIn.Num(); ++k)
						{
							if ((ForRequestType == EUrlQueryRequestType::Segment  && IncIn[k].Equals(TEXT("segment"))) ||
								(ForRequestType == EUrlQueryRequestType::Xlink    && IncIn[k].Equals(TEXT("xlink"))) ||
								(ForRequestType == EUrlQueryRequestType::Mpd      && IncIn[k].Equals(TEXT("mpd"))) ||
								(ForRequestType == EUrlQueryRequestType::Callback && IncIn[k].Equals(TEXT("callback"))) ||
								(ForRequestType == EUrlQueryRequestType::Chaining && IncIn[k].Equals(TEXT("chaining"))) ||
								(ForRequestType == EUrlQueryRequestType::Fallback && IncIn[k].Equals(TEXT("fallback"))))
							{
								UrlQueries.Emplace(MoveTemp(uq));
							}
						}
					}
				}
			}
			OutUrlQueries.Insert(UrlQueries, 0);
			StartingAt = StartingAt->GetParentElement();
		}
	}

	/**
	 * Applies the URL query elements to the specified URL in hierarchy order.
	 */
	FErrorDetail ApplyUrlQueries(IPlayerSessionServices* PlayerSessionServices, const FString& InMPDUrl, FString& InOutURL, FString& OutRequestHeader, const TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>>& UrlQueries)
	{
		FErrorDetail Error;
		// Short circuit zero queries.
		if (UrlQueries.Num() == 0)
		{
			return Error;
		}

		// Short circuit data URLs. Since the URL is plain text data of the content we must not append any query parameters
		if (InOutURL.StartsWith(SchemeDATA))
		{
			return Error;
		}

		bool bAllowedToUse = true;
		bool bMpdUrlParsed = false;
		bool bInUrlParsed = false;
		FURL_RFC3986 MpdUrl, InUrl;

		FString TotalFinalQueryString;
		FString TotalFinalHttpRequestHeader;
		int32 NumBadQueryTemplates = 0;

		// The URL queries are in correct hierarchy order.
		for(int32 nUrlQuery=0; nUrlQuery<UrlQueries.Num(); ++nUrlQuery)
		{
			const TSharedPtrTS<FDashMPD_UrlQueryInfoType>& uq = UrlQueries[nUrlQuery];

			// FIXME: Remote elements need to be dereferenced but we do not support xlink on them at the moment.
			if (uq->GetXLink().IsSet())
			{
				Error = CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("xlink is not supported on UrlQueryInfo / ExtUrlQueryInfo elements!")), ERRCODE_DASH_MPD_BUILDER_XLINK_NOT_SUPPORTED_ON_ELEMENT);
				return Error;
			}

			// The standard does not say what to do when @queryTemplate is empty. Since everything is selected when
			// it is set to "$querypart$" we have to assume that if it is empty nothing is to be selected in which
			// case we skip over this item.
			FString queryTemplate = uq->GetQueryTemplate();
			if (queryTemplate.IsEmpty())
			{
				continue;
			}

			TArray<FURL_RFC3986::FQueryParam> QueryParamList;
			// Using @headerParamSource?
			if (uq->GetHeaderParamSources().Num() == 0)
			{
				// No. In this case @useMPDUrlQuery and/or @queryString may be set.
				// If the MPD URL is to be used add its query parameters to the list.
				if (uq->GetUseMPDUrlQuery())
				{
					if (!bMpdUrlParsed)
					{
						bMpdUrlParsed = true;
						MpdUrl.Parse(InMPDUrl);
					}
					MpdUrl.GetQueryParams(QueryParamList, false);
				}
				// Then, if there is an explicit query string, break it down and add it to the list.
				if (!uq->GetQueryString().IsEmpty())
				{
					FURL_RFC3986::GetQueryParams(QueryParamList, uq->GetQueryString(), false);
				}

				// Filter out any urn: schemes. We do not support any of them.
				for(int32 i=0; i<QueryParamList.Num(); ++i)
				{
					if (QueryParamList[i].Value.StartsWith(TEXT("$urn:")))
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Ignoring unsupported scheme \"%s\" in UrlQueryInfo."), *QueryParamList[i].Value));
						QueryParamList.RemoveAt(i);
						--i;
					}
				}

				// Same origin only?
				// Note: The standard states that if the parameters are instantiated the the MPD (@queryString) or the MPD URL (@useMPDUrlQuery)
				//       the origin is the MPD URL!
				if (uq->GetSameOriginOnly())
				{
					if (!bMpdUrlParsed)
					{
						bMpdUrlParsed = true;
						MpdUrl.Parse(InMPDUrl);
					}
					if (!bInUrlParsed)
					{
						bInUrlParsed = true;
						InUrl.Parse(InOutURL);
					}
					if (!InUrl.HasSameOriginAs(MpdUrl))
					{
						// Not the same origin. Skip over this element.
						continue;
					}
				}
			}
			else
			{
				TSharedPtrTS<IPlayerEntityCache> EntityCache = PlayerSessionServices ? PlayerSessionServices->GetEntityCache() : nullptr;
				if (!EntityCache.IsValid())
				{
					continue;
				}
				/*
					When @headerParamSource is used then neither @useMPDUrlQuery and @queryString must be set.
					We do not validate this, we merely ignore them.
					NOTE: There is a conflict in the standard. In I.3.2 Table I.3 states for @headerParamSource:
							"If this attribute is present then: (a) @queryTemplate attribute shall be present and shall contain the $header:<header-name>$ identifier,
							and (b) neither @useMPDUrlQuery nor @queryString attribute shall be present."
						  with (b) being the noteworthy part.
						  Yet in I.3.4.2 is stated "3) Otherwise the value of initialQueryString is given by @queryString."
						  which is contradicting as there is no @queryString to be used.
						  We go with the table and clause (b).
				*/
				for(int32 nHdrSrc=0; nHdrSrc<uq->GetHeaderParamSources().Num(); ++nHdrSrc)
				{
					FString ResponseHeaderURL;
					TArray<HTTP::FHTTPHeader> ResponseHeaders;
					if (uq->GetHeaderParamSources()[nHdrSrc].Equals(TEXT("segment")))
					{
						EntityCache->GetRecentResponseHeaders(ResponseHeaderURL, ResponseHeaders, IPlayerEntityCache::EEntityType::Segment);
					}
					else if (uq->GetHeaderParamSources()[nHdrSrc].Equals(TEXT("xlink")))
					{
						EntityCache->GetRecentResponseHeaders(ResponseHeaderURL, ResponseHeaders, IPlayerEntityCache::EEntityType::XLink);
					}
					else if (uq->GetHeaderParamSources()[nHdrSrc].Equals(TEXT("mpd")))
					{
						EntityCache->GetRecentResponseHeaders(ResponseHeaderURL, ResponseHeaders, IPlayerEntityCache::EEntityType::Document);
					}
					else if (uq->GetHeaderParamSources()[nHdrSrc].Equals(TEXT("callback")))
					{
						EntityCache->GetRecentResponseHeaders(ResponseHeaderURL, ResponseHeaders, IPlayerEntityCache::EEntityType::Callback);
					}
					else
					{
						// Unsupported source. Skip over it.
						continue;
					}
					// Is there something available?
					if (!ResponseHeaderURL.IsEmpty() && ResponseHeaders.Num())
					{
						// Same origin only?
						if (uq->GetSameOriginOnly())
						{
							FURL_RFC3986 HeaderUrl;
							HeaderUrl.Parse(ResponseHeaderURL);
							if (!bInUrlParsed)
							{
								bInUrlParsed = true;
								InUrl.Parse(InOutURL);
							}
							if (!InUrl.HasSameOriginAs(HeaderUrl))
							{
								// Not the same origin. Skip over this element.
								continue;
							}
						}
						// Add all response headers to the parameter list.
						for(int32 i=0; i<ResponseHeaders.Num(); ++i)
						{
							// Only add if it isn't there yet, otherwise update it.
							bool bUpdated = false;
							for(int32 j=0; j<QueryParamList.Num(); ++j)
							{
								if (QueryParamList[j].Name.Equals(*ResponseHeaders[i].Header))
								{
									QueryParamList[j].Value = ResponseHeaders[i].Value;
									bUpdated = true;
									break;
								}
							}
							if (!bUpdated)
							{
								QueryParamList.Emplace(FURL_RFC3986::FQueryParam({MoveTemp(ResponseHeaders[i].Header), MoveTemp(ResponseHeaders[i].Value)}));
							}
						}
					}
				}
			}

			// Process the elements as described by the query template.
			FString FinalQueryString;
			while(!queryTemplate.IsEmpty())
			{
				int32 tokenPos = INDEX_NONE;
				if (!queryTemplate.FindChar(TCHAR('$'), tokenPos))
				{
					// The queryTemplate needs to bring all required '&' with it. We do not have to add anything besides what is in the template.
					FinalQueryString.Append(queryTemplate);
					break;
				}
				else
				{
					// Append everything up to the first token.
					if (tokenPos)
					{
						FinalQueryString.Append(queryTemplate.Mid(0, tokenPos));
					}
					// Need to find another token.
					int32 token2Pos = queryTemplate.Find(TEXT("$"), ESearchCase::CaseSensitive, ESearchDir::FromStart, tokenPos+1);
					if (token2Pos != INDEX_NONE)
					{
						FString token(queryTemplate.Mid(tokenPos+1, token2Pos-tokenPos-1));
						queryTemplate.RightChopInline(token2Pos+1, false);
						// An empty token results from "$$" used to insert a single '$'.
						if (token.IsEmpty())
						{
							FinalQueryString.AppendChar(TCHAR('$'));
						}
						// $querypart$ ?
						else if (token.Equals(TEXT("querypart")))
						{
							FString queryPart;
							for(int32 i=0; i<QueryParamList.Num(); ++i)
							{
								if (i)
								{
									queryPart.AppendChar(TCHAR('&'));
								}
								queryPart.Append(QueryParamList[i].Name);
								queryPart.AppendChar(TCHAR('='));
								queryPart.Append(QueryParamList[i].Value);
							}
							FinalQueryString.Append(queryPart);
						}
						// $query:<param>$ ?
						else if (token.StartsWith(TEXT("query:"), ESearchCase::CaseSensitive))
						{
							token.RightChopInline(6, false);
							int32 Index = QueryParamList.IndexOfByPredicate([token](const FURL_RFC3986::FQueryParam& qp){ return qp.Name.Equals(token); });
							if (Index != INDEX_NONE)
							{
								// Emit a warning if there is no '=' to append the value to.
								if (FinalQueryString.IsEmpty() || (FinalQueryString.Len() && FinalQueryString[FinalQueryString.Len()-1] != TCHAR('=')))
								{
									LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("UrlQueryInfo does not provide a '=' to append the value of \"%s\" to."), *token));
								}

								FinalQueryString.Append(QueryParamList[Index].Value);
							}
						}
						// $header:<header-name>$ ?
						else if (token.StartsWith(TEXT("header:"), ESearchCase::CaseSensitive))
						{
							token.RightChopInline(7, false);
							int32 Index = QueryParamList.IndexOfByPredicate([token](const FURL_RFC3986::FQueryParam& qp){ return qp.Name.Equals(token); });
							if (Index != INDEX_NONE)
							{
								// Emit a warning if there is no '=' to append the value to.
								if (FinalQueryString.IsEmpty() || (FinalQueryString.Len() && FinalQueryString[FinalQueryString.Len()-1] != TCHAR('=')))
								{
									LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("UrlQueryInfo does not provide a '=' to append the value of \"%s\" to."), *token));
								}

								FinalQueryString.Append(QueryParamList[Index].Value);
							}
						}
						else
						{
							// Unknown. Skip over and continue.
						}
					}
					else
					{
						// Bad query template string. Ignore this UrlQueryInfo.
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("UrlQueryInfo has a malformed @queryTemplate string.")));
						FinalQueryString.Empty();
						++NumBadQueryTemplates;
						break;
					}
				}
			}

			// Where does the output go? URL query param or HTTP request header?
			if (uq->GetExtendedUrlInfoType() == FDashMPD_UrlQueryInfoType::EExtendedUrlInfoType::ExtUrlQueryInfo)
			{
				// Do we need to add a '&' before appending the queryPart to the finalQueryString?
				if (FinalQueryString.Len() && TotalFinalQueryString.Len() && TotalFinalQueryString[0] != TCHAR('&'))
				{
					TotalFinalQueryString.AppendChar(TCHAR('&'));
				}
				TotalFinalQueryString.Append(FinalQueryString);
			}
			else if (uq->GetExtendedUrlInfoType() == FDashMPD_UrlQueryInfoType::EExtendedUrlInfoType::ExtHttpHeaderInfo)
			{
				FString FinalHeaderString = FinalQueryString.Replace(TEXT("&"), TEXT(", "), ESearchCase::CaseSensitive);
				if (FinalHeaderString.Len() && TotalFinalHttpRequestHeader.Len() > 1 && TotalFinalHttpRequestHeader[0] != TCHAR(',') && TotalFinalHttpRequestHeader[1] != TCHAR(' '))
				{
					TotalFinalHttpRequestHeader.Append(TEXT(", "));
				}
				TotalFinalHttpRequestHeader.Append(FinalHeaderString);
			}
			else
			{
				// Unsupported. Ignore.
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Unsupported type of UrlQueryInfo")));
			}
		}

		// If there was a bad @queryTemplate we fail if all have failed. If even one succeeded we take it.
		if (NumBadQueryTemplates == UrlQueries.Num())
		{
			// This is not an actual error. Instead we pretend as if we do not understand the UrlQuery element.
			return Error;
		}

		if (!bInUrlParsed)
		{
			bInUrlParsed = true;
			InUrl.Parse(InOutURL);
		}
		InUrl.AddQueryParameters(TotalFinalQueryString, true);
		InOutURL = InUrl.Get();
		OutRequestHeader = TotalFinalHttpRequestHeader;

		return Error;
	}


	/**
	 * Resolves relative URLs against their parent BaseURL elements in the hierarchy.
	 * If this does not produce an absolute URL it will finally be resolved against the URL the MPD was loaded from.
	 * Returns true if an absolute URL could be generated, false if not.
	 * Since the MPD URL had to be an absolute URL this cannot actually fail.
	 */
	bool BuildAbsoluteElementURL(FString& OutURL, const FString& DocumentURL, const TArray<TSharedPtrTS<const FDashMPD_BaseURLType>>& BaseURLs, const FString& InElementURL)
	{
		FURL_RFC3986 UrlParser;
		FString ElementURL(InElementURL);
		// If the element URL is empty it is specified as the first entry in the BaseURL array.
		// We MUST NOT resolve against an empty URL because as per RFC 3986 section 5.2.2 the query string and fragment come from the relative
		// URL and if we were to start with an empty one we would lose those!
		int32 nBase = 0;
		if (ElementURL.IsEmpty())
		{
			// Must not be empty for this!
			if (BaseURLs.Num() == 0)
			{
				return false;
			}
			ElementURL = BaseURLs[0]->GetURL();
			++nBase;
		}
		if (UrlParser.Parse(ElementURL))
		{
			while(1)
			{
				if (UrlParser.IsAbsolute())
				{
					break;
				}
				else
				{
					if (nBase < BaseURLs.Num())
					{
						UrlParser.ResolveAgainst(BaseURLs[nBase++]->GetURL());
					}
					else
					{
						UrlParser.ResolveAgainst(DocumentURL);
						break;
					}
				}
			}
			OutURL = UrlParser.Get();
			return UrlParser.IsAbsolute();
		}
		return false;
	}

	FString ApplyAnnexEByteRange(FString InURL, FString InRange, const TArray<TSharedPtrTS<const FDashMPD_BaseURLType>>& BaseURLs)
	{
		// Short circuit missing parameters.
		if (InRange.IsEmpty() || BaseURLs.Num() == 0)
		{
			return MoveTemp(InURL);
		}
		// Find the first <BaseURL> element having a byteRange attribute.
		FString byteRange;
		for(int32 i=0; i<BaseURLs.Num(); ++i)
		{
			byteRange = BaseURLs[i]->GetByteRange();
			if (!byteRange.IsEmpty())
			{
				break;
			}
		}
		if (byteRange.IsEmpty())
		{
			return MoveTemp(InURL);
		}
		int32 DashPos = INDEX_NONE;
		if (!InRange.FindChar(TCHAR('-'), DashPos))
		{
			return MoveTemp(InURL);
		}

		int32 QueryPos = INDEX_NONE;
		InURL.FindChar(TCHAR('?'), QueryPos);
		// Process the elements as described by the byte range.
		FString NewURL;
		while(!byteRange.IsEmpty())
		{
			int32 tokenPos = INDEX_NONE;
			if (!byteRange.FindChar(TCHAR('$'), tokenPos))
			{
				NewURL.Append(byteRange);
				break;
			}
			else
			{
				// Append everything up to the first token.
				if (tokenPos)
				{
					NewURL.Append(byteRange.Mid(0, tokenPos));
				}
				// Need to find another token.
				int32 token2Pos = byteRange.Find(TEXT("$"), ESearchCase::CaseSensitive, ESearchDir::FromStart, tokenPos+1);
				if (token2Pos != INDEX_NONE)
				{
					FString token(byteRange.Mid(tokenPos+1, token2Pos-tokenPos-1));
					byteRange.RightChopInline(token2Pos+1, false);
					// An empty token results from "$$" used to insert a single '$'.
					if (token.IsEmpty())
					{
						NewURL.AppendChar(TCHAR('$'));
					}
					// $base$ ?
					else if (token.Equals(TEXT("base")))
					{
						NewURL.Append(InURL.Mid(0, QueryPos != INDEX_NONE ? QueryPos : MAX_int32));
					}
					// $query$ ?
					else if (token.Equals(TEXT("query")))
					{
						FString query = InURL.Mid(QueryPos != INDEX_NONE ? QueryPos + 1 : MAX_int32);
						if (query.IsEmpty())
						{
							// Remove preceeding separator character (which we assume it to be meant to be the ampersand)
							if (NewURL.Len() && NewURL[NewURL.Len() - 1] == TCHAR('&'))
							{
								NewURL.LeftChopInline(1, false);
							}
							// If the next char in the template is a separator character it is to be removed.
							if (byteRange.Len() && byteRange[0] == TCHAR('&'))
							{
								byteRange.RightChopInline(1, false);
							}
						}
						else
						{
							NewURL.Append(query);
						}
					}
					// $first$ ?
					else if (token.Equals(TEXT("first")))
					{
						NewURL.Append(InRange.Mid(0, DashPos));
					}
					// $last$ ?
					else if (token.Equals(TEXT("last")))
					{
						NewURL.Append(InRange.Mid(DashPos + 1));
					}
					else
					{
						// Unknown. Skip over and continue.
					}
				}
				else
				{
					// Bad template string. Ignore this.
					return InURL;
				}
			}
		}
		return NewURL;
	}


} // namespace DASHUrlHelpers



class FManifestBuilderDASH : public IManifestBuilderDASH
{
public:
	FManifestBuilderDASH(IPlayerSessionServices* InPlayerSessionServices);
	virtual ~FManifestBuilderDASH() = default;

	/**
	 * Builds a new internal manifest from a DASH MPD
	 *
	 * @param OutMPD
	 * @param InOutMPDXML
	 * @param SourceRequest
	 * @param Preferences
	 * @param Options
	 *
	 * @return
	 */
	virtual FErrorDetail BuildFromMPD(TSharedPtrTS<FManifestDASHInternal>& OutMPD, TCHAR* InOutMPDXML, TSharedPtrTS<FMPDLoadRequestDASH> SourceRequest, const FStreamPreferences& Preferences, const FParamDict& Options) override;
private:
	ELECTRA_IMPL_DEFAULT_ERROR_METHODS(DASHMPDBuilder);

	IPlayerSessionServices* PlayerSessionServices = nullptr;
};


/*********************************************************************************************************************/

IManifestBuilderDASH* IManifestBuilderDASH::Create(IPlayerSessionServices* InPlayerSessionServices)
{
	return new FManifestBuilderDASH(InPlayerSessionServices);
}

/*********************************************************************************************************************/
FManifestBuilderDASH::FManifestBuilderDASH(IPlayerSessionServices* InPlayerSessionServices)
	: PlayerSessionServices(InPlayerSessionServices)
{
}

FErrorDetail FManifestBuilderDASH::BuildFromMPD(TSharedPtrTS<FManifestDASHInternal>& OutMPD, TCHAR* InOutMPDXML, TSharedPtrTS<FMPDLoadRequestDASH> SourceRequest, const FStreamPreferences& Preferences, const FParamDict& Options)
{
	TSharedPtrTS<FDashMPD_MPDType> NewMPD;
	TArray<TWeakPtrTS<IDashMPDElement>> XLinkElements;
	FDashMPD_RootEntities RootEntities;

	FErrorDetail Error = IManifestParserDASH::BuildFromMPD(RootEntities, XLinkElements, InOutMPDXML, TEXT("MPD"), PlayerSessionServices);
	if (Error.IsSet())
	{
		return Error;
	}
	if (RootEntities.MPDs.Num() == 0)
	{
		return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("No root <MPD> element found")), ERRCODE_DASH_MPD_BUILDER_MISSING_REQUIRED_ELEMENT);
	}
	NewMPD = RootEntities.MPDs[0];
	NewMPD->SetDocumentURL(SourceRequest->URL);
	NewMPD->SetFetchTime(SourceRequest->GetConnectionInfo()->RequestStartTime);

	// Check if this MPD uses a profile we can handle. (If our array is empty we claim to handle anything)
	bool bHasUsableProfile = UE_ARRAY_COUNT(SupportedProfiles) == 0;
	const TArray<FString>& MPDProfiles = NewMPD->GetProfiles();
	if (MPDProfiles.Num() == 0)
	{
		return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("MPD@profiles is required")), ERRCODE_DASH_MPD_BUILDER_MISSING_REQUIRED_ATTRIBUTE);
	}
	for(int32 nSupportedProfile=0; !bHasUsableProfile && nSupportedProfile<UE_ARRAY_COUNT(SupportedProfiles); ++nSupportedProfile)
	{
		for(int32 nProfile=0; !bHasUsableProfile && nProfile<MPDProfiles.Num(); ++nProfile)
		{
			if (MPDProfiles[nProfile].Equals(SupportedProfiles[nSupportedProfile]))
			{
				bHasUsableProfile = true;
				break;
			}
		}
	}
	if (!bHasUsableProfile)
	{
		return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("MPD is of no supported profile")), ERRCODE_DASH_MPD_BUILDER_UNSUPPORTED_PROFILE);
	}

	// Check if this MPD requires essential properties we do not understand.
	const TArray<TSharedPtrTS<FDashMPD_DescriptorType>>& EssentialProperties = NewMPD->GetEssentialProperties();
	for(int32 nEssProp=0; nEssProp<EssentialProperties.Num(); ++nEssProp)
	{
		bool bIsSupported = false;
		for(int32 nSupportedEssProp=0; nSupportedEssProp<UE_ARRAY_COUNT(SupportedEssentialProperties); ++nSupportedEssProp)
		{
			if (EssentialProperties[nEssProp]->GetSchemeIdUri().Equals(SupportedEssentialProperties[nSupportedEssProp]))
			{
				bIsSupported = true;
				break;
			}
		}
		if (!bIsSupported)
		{
			return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("MPD requires unsupported EssentialProperty \"%s\" for playback"), *EssentialProperties[nEssProp]->GetSchemeIdUri()), ERRCODE_DASH_MPD_BUILDER_UNSUPPORTED_ESSENTIAL_PROPERTY);
		}
	}

	// For the purpose of loading an MPD from scratch we need to look only at xlink:actuate onLoad elements.
	// These MAY reference other onRequest elements (like UrlQueryInfo) in the process.
	for(int32 nRemoteElement=0; nRemoteElement<XLinkElements.Num(); ++nRemoteElement)
	{
		TSharedPtrTS<IDashMPDElement> XElem = XLinkElements[nRemoteElement].Pin();
		if (XElem.IsValid() && !XElem->GetXLink().GetActuate().Equals(XLinkActuateOnLoad))
		{
			XLinkElements.RemoveAt(nRemoteElement);
			--nRemoteElement;
		}
	}

	TSharedPtrTS<FManifestDASHInternal> NewManifest = MakeSharedTS<FManifestDASHInternal>();
	Error = NewManifest->Build(PlayerSessionServices, NewMPD, MoveTemp(XLinkElements), Preferences, Options);
	if (Error.IsOK() || Error.IsTryAgain())
	{
		OutMPD = MoveTemp(NewManifest);
	}
	return Error;
}



FErrorDetail FManifestDASHInternal::PrepareRemoteElementLoadRequest(IPlayerSessionServices* PlayerSessionServices, TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& OutRemoteElementLoadRequests, TWeakPtrTS<IDashMPDElement> InElementWithXLink, int64 RequestID)
{
	const TCHAR* PreferredServiceLocation = nullptr;

	FErrorDetail Error;
	TSharedPtrTS<IDashMPDElement> XElem = InElementWithXLink.Pin();
	if (XElem.IsValid())
	{
		IDashMPDElement::FXLink& xl = XElem->GetXLink();

		// There should not be a pending request when we get here.
		check(!xl.LoadRequest.IsValid());
		if (xl.LoadRequest.IsValid())
		{
			// If there is one we assume it is in flight and return without raising an error.
			return Error;
		}

		FMPDLoadRequestDASH::ELoadType LoadReqType = FMPDLoadRequestDASH::ELoadType::MPD;
		switch(XElem->GetElementType())
		{
			case IDashMPDElement::EType::Period:				LoadReqType = FMPDLoadRequestDASH::ELoadType::XLink_Period;				break;
			case IDashMPDElement::EType::AdaptationSet:			LoadReqType = FMPDLoadRequestDASH::ELoadType::XLink_AdaptationSet;		break;
			case IDashMPDElement::EType::URLQueryInfo:			LoadReqType = FMPDLoadRequestDASH::ELoadType::XLink_URLQuery;			break;
			case IDashMPDElement::EType::EventStream:			LoadReqType = FMPDLoadRequestDASH::ELoadType::XLink_EventStream;		break;
			case IDashMPDElement::EType::SegmentList:			LoadReqType = FMPDLoadRequestDASH::ELoadType::XLink_SegmentList;		break;
			case IDashMPDElement::EType::InitializationSet:		LoadReqType = FMPDLoadRequestDASH::ELoadType::XLink_InitializationSet;	break;
			default:
				return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("xlink on %s element is not supported"), *XElem->GetName()), ERRCODE_DASH_MPD_BUILDER_XLINK_NOT_SUPPORTED_ON_ELEMENT);
		}

		FString URL;
		FString RequestHeader;
		// Get the xlink:href
		FString XlinkHRef = xl.GetHref();
		if (!XlinkHRef.Equals(XLinkResolveToZero))
		{
			if (DASHUrlHelpers::IsAbsoluteURL(XlinkHRef))
			{
				URL = MoveTemp(XlinkHRef);
			}
			else
			{
				TArray<TSharedPtrTS<const FDashMPD_BaseURLType>> OutBaseURLs;
				DASHUrlHelpers::GetAllHierarchyBaseURLs(PlayerSessionServices, OutBaseURLs, XElem->GetParentElement(), PreferredServiceLocation);
				if (!DASHUrlHelpers::BuildAbsoluteElementURL(URL, MPDRoot->GetDocumentURL(), OutBaseURLs, XlinkHRef))
				{
					// Not resolving to an absolute URL is very unlikely as we had to load the MPD itself from somewhere.
					return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("xlink:href did not resolve to an absolute URL")), ERRCODE_DASH_MPD_BUILDER_URL_FAILED_TO_RESOLVE);
				}
			}

			// The URL query might need to be changed. Look for the UrlQuery properties.
			TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>> UrlQueries;
			DASHUrlHelpers::GetAllHierarchyUrlQueries(UrlQueries, XElem->GetParentElement(), DASHUrlHelpers::EUrlQueryRequestType::Xlink, false);
			// FIXME: pass xlink requests along to allow UrlQuery to do xlink as well (recursively).
			Error = DASHUrlHelpers::ApplyUrlQueries(PlayerSessionServices, MPDRoot->GetDocumentURL(), URL, RequestHeader, UrlQueries);
			if (Error.IsSet())
			{
				return Error;
			}
		}
		else
		{
			URL = XlinkHRef;
		}
		// Create the request.
		TSharedPtrTS<FMPDLoadRequestDASH> LoadReq = MakeSharedTS<FMPDLoadRequestDASH>();
		LoadReq->LoadType = LoadReqType;
		LoadReq->URL = URL;
		if (RequestHeader.Len())
		{
			LoadReq->Headers.Emplace(HTTP::FHTTPHeader({TEXT("MPEG-DASH-Param"), RequestHeader}));
		}
		LoadReq->XLinkElement = XElem;

		// Put the request on the xlink element so we know it has been requested and the result is pending.
		xl.LoadRequest = LoadReq;
		xl.LastResolveID = RequestID;

		// Add the request to the list to return for execution.
		OutRemoteElementLoadRequests.Emplace(LoadReq);
	}
	return Error;
}



FErrorDetail FManifestDASHInternal::Build(IPlayerSessionServices* PlayerSessionServices, TSharedPtr<FDashMPD_MPDType, ESPMode::ThreadSafe> InMPDRoot, TArray<TWeakPtrTS<IDashMPDElement>> InXLinkElements, const FStreamPreferences& InPreferences, const FParamDict& InOptions)
{
	RemoteElementsToResolve = MoveTemp(InXLinkElements);

	FErrorDetail Error;

	Preferences = InPreferences;
	Options = InOptions;

	MPDRoot = InMPDRoot;
	
	// What type of presentation is this?
	// We do not validate the MPD@type here. Anything not 'static' is handled as 'dynamic'.
	PresentationType = MPDRoot->GetType().Equals(TEXT("static")) ? EPresentationType::Static : EPresentationType::Dynamic;

	// Dynamic presentations require both MPD@availabilityStartTime and MPD@publishTime
	if (PresentationType == EPresentationType::Dynamic)
	{
		if (!MPDRoot->GetAvailabilityStartTime().IsValid() || !MPDRoot->GetPublishTime().IsValid())
		{
			return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("Dynamic presentations require both MPD@availabilityStartTime and MPD@publishTime to be valid")), ERRCODE_DASH_MPD_BUILDER_MISSING_REQUIRED_ATTRIBUTE);
		}
	}
	else
	{
		if (MPDRoot->GetMinimumUpdatePeriod().IsValid())
		{
			return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("Static presentations must not have MPD@minimumUpdatePeriod")), ERRCODE_DASH_MPD_BUILDER_UNREQUIRED_ATTRIBUTE);
		}
	}

	// MPD@minBufferTime is required.
	if (!MPDRoot->GetMinBufferTime().IsValid())
	{
		return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("MPD@minBufferTime is required")), ERRCODE_DASH_MPD_BUILDER_MISSING_REQUIRED_ATTRIBUTE);
	}

	// At least one Period is required.
	if (MPDRoot->GetPeriods().Num() == 0)
	{
		return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("At least one Period is required")), ERRCODE_DASH_MPD_BUILDER_MISSING_REQUIRED_ATTRIBUTE);
	}

	// Go over the list of remote elements we need to process.
	// Initially this list includes onLoad elements only but may get expanded if these elements reference additional onRequest elements.
	// As long as this list is not empty we are not done with the initial MPD setup.
	for(int32 nRemoteElement=0; nRemoteElement<RemoteElementsToResolve.Num(); ++nRemoteElement)
	{
		Error = PrepareRemoteElementLoadRequest(PlayerSessionServices, PendingRemoteElementLoadRequests, RemoteElementsToResolve[nRemoteElement], 1);
		if (Error.IsSet())
		{
			return Error;
		}
	}
	if (PendingRemoteElementLoadRequests.Num())
	{
		return Error.SetTryAgain();
	}
	return BuildAfterInitialRemoteElementDownload(PlayerSessionServices);
}

FErrorDetail FManifestDASHInternal::BuildAfterInitialRemoteElementDownload(IPlayerSessionServices* PlayerSessionServices)
{
	FErrorDetail Error;

	bool bWarnedPresentationDuration = false;
	// Go over the periods one at a time as XLINK attributes could bring in additional periods.
	FTimeValue CombinedPeriodDuration(FTimeValue::GetZero());
	const TArray<TSharedPtrTS<FDashMPD_PeriodType>>& MPDperiods = MPDRoot->GetPeriods();
	for(int32 nPeriod=0,nPeriodMax=MPDperiods.Num(); nPeriod<nPeriodMax; ++nPeriod)
	{
		TSharedPtrTS<FPeriod> p = MakeSharedTS<FPeriod>();

		// Keep a weak reference to the original period.
		p->Period = MPDperiods[nPeriod];

		p->ID = MPDperiods[nPeriod]->GetID();
		// We need an ID to track this period, even if there is no Period@id in the MPD.
		// In this case we make up an artificial name.
		if (p->ID.IsEmpty())
		{
			p->ID = FString::Printf(TEXT("$unnamed.%d$"), nPeriod);
		}

		p->Start = MPDperiods[nPeriod]->GetStart();
		p->Duration = MPDperiods[nPeriod]->GetDuration();
		// If the period start is not specified this could be an early available period.
		// See 5.3.2.1
		if (!p->Start.IsValid())
		{
			if (nPeriod == 0 && PresentationType == EPresentationType::Static)
			{
				p->Start.SetToZero();
			}
			else if (nPeriod && MPDperiods[nPeriod-1]->GetDuration().IsValid())
			{
				p->Start = Periods.Last()->Start + MPDperiods[nPeriod-1]->GetDuration();
			}
			else if (PresentationType == EPresentationType::Dynamic)
			{
				p->bIsEarlyPeriod = true;
			}
			else
			{
				return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("Period@start cannot be derived for period \"%s\""), *p->ID), ERRCODE_DASH_MPD_BUILDER_BAD_PERIOD_START);
			}
		}
		// Calculate a period end time for convenience. This is more accessible than dealing with start+duration.
		if (!p->bIsEarlyPeriod)
		{
			// Check if a regular period follows an early available period, which is a violation of 5.3.2.1
			if (Periods.Num() && Periods.Last()->bIsEarlyPeriod)
			{
				return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("Early available period \"%s\" must not be followed by a regular period (\"%s\")"), *Periods.Last()->ID, *p->ID), ERRCODE_DASH_MPD_BUILDER_EARLY_PERIODS_MUST_BE_LAST);
			}
			// If the previous period did not have a duration to calculate the end time with we set
			// its end time to the start time of this period.
			if (Periods.Num() && !Periods.Last()->End.IsValid())
			{
				Periods.Last()->End = p->Start;
			}

			if (MPDperiods[nPeriod]->GetDuration().IsValid())
			{
				p->End = p->Start + MPDperiods[nPeriod]->GetDuration();
			}
			else if (nPeriod < nPeriodMax-1)
			{
				if (MPDperiods[nPeriod + 1]->GetStart().IsValid())
				{
					p->End = MPDperiods[nPeriod + 1]->GetStart();
					if (!p->Duration.IsValid())
					{
						p->Duration = p->End - p->Start;
					}
				}
			}
			else if (nPeriod == nPeriodMax-1)
			{
				if (MPDRoot->GetMediaPresentationDuration().IsValid())
				{
					FTimeValue firstPeriodStart = Periods.Num() ? Periods[0]->Start : p->Start;
					p->End = firstPeriodStart + MPDRoot->GetMediaPresentationDuration();
					if (!p->Duration.IsValid())
					{
						p->Duration = p->End - p->Start;
					}
				}
				else if (PresentationType == EPresentationType::Static)
				{
					return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("Static presentations with the last Period@duration missing need to have MPD@mediaPresentationDuration set!")), ERRCODE_DASH_MPD_BUILDER_MEDIAPRESENTATIONDURATION_NEEDED);
				}
				else if (PresentationType == EPresentationType::Dynamic && !MPDRoot->GetMinimumUpdatePeriod().IsValid())
				{
					return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("Dynamic presentations with the last Period@duration missing and no MPD@minimumUpdateTime need to have MPD@mediaPresentationDuration set!")), ERRCODE_DASH_MPD_BUILDER_MEDIAPRESENTATIONDURATION_NEEDED);
				}
			}
			
			// Check for potential period overlap or gaps.
			if (Periods.Num())
			{
				// Check for period ordering. Start times must be increasing.
				if (p->Start.IsValid() && Periods.Last()->Start.IsValid() && p->Start < Periods.Last()->Start)
				{
					return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("Period@start times must be increasing for periods \"%s\" and \"%s\""), *p->ID, *Periods.Last()->ID), ERRCODE_DASH_MPD_BUILDER_MEDIAPRESENTATIONDURATION_NEEDED);
				}

				FTimeValue diff = p->Start - Periods.Last()->End;
				// Does this period cut into the preceeding one?
				if (diff.IsValid() && diff < FTimeValue::GetZero())
				{
					// Set the end time of the preceeding period to the start of this one.
					Periods.Last()->End = p->Start;
				}
				// Is there a gap?
				else if (diff.IsValid() && diff > FTimeValue::GetZero())
				{
					// This is not desireable. There will be no content in the preceeding period to cover the gap.
					// Depending on the duration of the gap this could be problem.
					if (diff.GetAsSeconds() > 0.5)
					{
						// Log a warning!
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("There is a gap of %.f seconds between periods \"%s\" and \"%s\"!"), diff.GetAsSeconds(), *Periods.Last()->ID, *p->ID));
					}
				}
			}
		}

		if (p->Start.IsValid() && p->End.IsValid())
		{
			FTimeValue periodDur = p->End - p->Start;
			if (periodDur >= FTimeValue::GetZero())
			{
				if (p->Duration.IsValid() && periodDur > p->Duration)
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Period \"%s\" has %.f seconds shorter duration than next period start indicates!"), *p->ID, (periodDur - p->Duration).GetAsSeconds()));
				}
				CombinedPeriodDuration += periodDur;
				// Do a check if the media presentation duration cuts into the periods or is longer than the last period.
				if (MPDRoot->GetMediaPresentationDuration().IsValid())
				{
					FTimeValue diff = MPDRoot->GetMediaPresentationDuration() - CombinedPeriodDuration;
					if (diff < FTimeValue::GetZero() && diff.GetAsSeconds() < -0.5 && !bWarnedPresentationDuration)
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("MPD@mediaPresentationDuration cuts into Period \"%s\" by %.f seconds!"), *p->ID, -diff.GetAsSeconds()));
						bWarnedPresentationDuration = true;
					}
					else if (nPeriod == nPeriodMax-1 && diff > FTimeValue::GetZero() && diff.GetAsSeconds() > 0.5 && !bWarnedPresentationDuration)
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("MPD@mediaPresentationDuration extends past the last period's end by %.f seconds. There is no content to play for that time!"), diff.GetAsSeconds()));
						bWarnedPresentationDuration = true;
					}
				}
			}
			else
			{
				return CreateErrorAndLog(PlayerSessionServices, FString::Printf(TEXT("Duration of Period \"%s\" is calculated as negative!"), *p->ID), ERRCODE_DASH_MPD_BUILDER_BAD_PERIOD_DURATION);
			}
		}

		// Add period to the list.
		Periods.Emplace(MoveTemp(p));
	}

	return Error;
}


FErrorDetail FManifestDASHInternal::PreparePeriodAdaptationSets(IPlayerSessionServices* PlayerSessionServices, TSharedPtrTS<FPeriod> Period, bool bRequestXlink)
{
	FErrorDetail Error;
	check(Period.IsValid());
	TSharedPtrTS<FDashMPD_PeriodType> MPDPeriod = Period->Period.Pin();
	if (MPDPeriod.IsValid())
	{
		Period->AdaptationSets.Empty();
		const TArray<TSharedPtrTS<FDashMPD_AdaptationSetType>>& MPDAdaptationSets = MPDPeriod->GetAdaptationSets();
		for(int32 nAdapt=0, nAdaptMax=MPDAdaptationSets.Num(); nAdapt<nAdaptMax; ++nAdapt)
		{
			const TSharedPtrTS<FDashMPD_AdaptationSetType>& MPDAdaptationSet = MPDAdaptationSets[nAdapt];
			TSharedPtrTS<FAdaptationSet> AdaptationSet = MakeSharedTS<FAdaptationSet>();
			// Store the index of this AdaptationSet in the array with the adaptation set.
			// AdaptationSets do not need to have an ID and we will filter out some sets without removing
			// them from the enclosing period (on purpose!!!).
			// Keeping the index helps locate the adaptation set in the enclosing period's adaptation set array.
			// Even with MPD updates the adaptation sets are not permitted to change so in theory the index will
			// be valid at all times.
			AdaptationSet->IndexOfSelf = nAdapt;
			AdaptationSet->AdaptationSet = MPDAdaptationSet;
			AdaptationSet->PAR = MPDAdaptationSet->GetPAR();
			AdaptationSet->Language = MPDAdaptationSet->GetLanguage();

			// Content components are not supported.
			if (MPDAdaptationSet->GetContentComponents().Num())
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("ContentComponent in AdaptationSet is not supported, ignoring this AdaptationSet.")));
				continue;
			}
			// Frame packing of any kind is not supported.
			else if (MPDAdaptationSet->GetFramePackings().Num())
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("FramePacking in AdaptationSet is not supported, ignoring this AdaptationSet.")));
				continue;
			}
			// Interlace video is not supported.
			else if (MPDAdaptationSet->GetScanType().Equals(ScanTypeInterlace))
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Interlace video in AdaptationSet is not supported, ignoring this AdaptationSet.")));
				continue;
			}
			// Ratings are not supported (as per DASH-IF-IOP)
			else if (MPDAdaptationSet->GetRatings().Num())
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Rating in AdaptationSet is not supported, ignoring this AdaptationSet.")));
				continue;
			}
			// We do not support Viewpoints.
			else if (MPDAdaptationSet->GetViewpoints().Num())
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Viewpoint in AdaptationSet is not supported, ignoring this AdaptationSet.")));
				continue;
			}

			// Check Roles for unsupported ones.
			bool bBadRole = false;
			for(int32 nRoles=0; nRoles<MPDAdaptationSet->GetRoles().Num(); ++nRoles)
			{
				if (MPDAdaptationSet->GetRoles()[nRoles]->GetSchemeIdUri().Equals(TEXT("urn:mpeg:dash:stereoid:2011")))
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Unsupported AdaptationSet Role found (\"%s\"), ignoring this AdaptationSet."), *MPDAdaptationSet->GetRoles()[nRoles]->GetSchemeIdUri()));
					bBadRole = true;
				}
				else if (MPDAdaptationSet->GetRoles()[nRoles]->GetSchemeIdUri().Equals(DASHRole))
				{
					// Check for role values we understand.
					FString Role = MPDAdaptationSet->GetRoles()[nRoles]->GetValue();
					if (Role.Equals(TEXT("main")) || Role.Equals(TEXT("alternate")) || Role.Equals(TEXT("supplementary")) || Role.Equals(TEXT("commentary")) || Role.Equals(TEXT("dub")) ||
						Role.Equals(TEXT("emergency")) || Role.Equals(TEXT("caption")) || Role.Equals(TEXT("subtitle")) || Role.Equals(TEXT("sign")) || Role.Equals(TEXT("description")))
					{
						AdaptationSet->Roles.Emplace(Role);
					}
					else
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Unsupported Role type \"%s\" found."), *Role));
					}
				}
			}
			if (bBadRole)
			{
				continue;
			}
			// By default the absence of a Role makes an AdaptationSet take on the "main" role.
			if (AdaptationSet->Roles.Num() == 0)
			{
				AdaptationSet->Roles.Emplace(FString(TEXT("main")));
			}

			// Check Accessibility
			for(int32 nAcc=0; nAcc<MPDAdaptationSet->GetAccessibilities().Num(); ++nAcc)
			{
				if (MPDAdaptationSet->GetAccessibilities()[nAcc]->GetSchemeIdUri().Equals(DASHRole))
				{
					FString Accessibility = MPDAdaptationSet->GetAccessibilities()[nAcc]->GetValue();
					if (Accessibility.Equals(TEXT("sign")) || Accessibility.Equals(TEXT("caption")) || Accessibility.Equals(TEXT("description")) || Accessibility.Equals(TEXT("enhanced-audio-intelligibility")))
					{
						AdaptationSet->Accessibilities.Emplace(Accessibility);
					}
					else
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Unsupported Accessibility type \"%s\" found."), *Accessibility));
					}
				}
				// Note: 608 captions are recognized but not 708 ones.
				else if (MPDAdaptationSet->GetAccessibilities()[nAcc]->GetSchemeIdUri().Equals(TEXT("urn:scte:dash:cc:cea-608:2015")))
				{
					// We do not parse this out here. We prepend a "608:" prefix and take the value verbatim for now.
					AdaptationSet->Accessibilities.Emplace(FString(TEXT("608:"))+MPDAdaptationSet->GetAccessibilities()[nAcc]->GetValue());
				}
			}

			// We do not rely on the AdaptationSet@contentType, AdaptationSet@mimeType and/or AdaptationSet@codecs as these are often not set.
			// First we go over the representations in the set and hope they have the @codecs attribute set.
			const TArray<TSharedPtrTS<FDashMPD_RepresentationType>>& MPDRepresentations = MPDAdaptationSet->GetRepresentations();
			TMultiMap<int32, TSharedPtrTS<FRepresentation>> RepresentationQualityIndexMap;
			for(int32 nRepr=0, nReprMax=MPDRepresentations.Num(); nRepr<nReprMax; ++nRepr)
			{
				const TSharedPtrTS<FDashMPD_RepresentationType>& MPDRepresentation = MPDRepresentations[nRepr];
				// Subrepresentations and dependent representations are not supported.
				if (MPDRepresentation->GetSubRepresentations().Num())
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("SubRepresentations are not supported, ignoring this Representation.")));
					continue;
				}
				else if (MPDRepresentation->GetDependencyIDs().Num())
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Representation dependencies are not supported, ignoring this Representation.")));
					continue;
				}
				else if (MPDRepresentation->GetMediaStreamStructureIDs().Num())
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Media stream structures are not supported, ignoring this Representation.")));
					continue;
				}
				// Frame packing of any kind is not supported.
				else if (MPDRepresentation->GetFramePackings().Num())
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("FramePacking in Representation is not supported, ignoring this Representation.")));
					continue;
				}
				// Interlace is not supported.
				else if (MPDRepresentation->GetScanType().Equals(ScanTypeInterlace))
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Interlace video in Representation is not supported, ignoring this Representation.")));
					continue;
				}

				// Encryption?
				if (MPDRepresentation->GetContentProtections().Num() || MPDAdaptationSet->GetContentProtections().Num())
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("ContentProtection is not supported, ignoring this Representation.")));
					continue;
				}


				// Is there a @codecs list on the representation itself?
				TArray<FString> MPDCodecs = MPDRepresentation->GetCodecs();
				if (MPDCodecs.Num() == 0)
				{
					// If not then there needs to be one on the AdaptationSet. However, this will specify the highest profile and level
					// necessary to decode any and all representations and is thus potentially too restrictive.
					MPDCodecs = MPDAdaptationSet->GetCodecs();
					// Need to have a codec now.
					if (MPDCodecs.Num() == 0)
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Neither @codecs found on Representation or AdaptationSet level, ignoring this Representation.")));
						continue;
					}
				}
				// Parse each of the codecs. There *should* be only one since we are not considering multiplexed streams (ContentComponent / SubRepresentation).
				if (MPDCodecs.Num() > 1)
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("More than one codec found for Representation, using only first codec.")));
				}


				TSharedPtrTS<FRepresentation> Representation = MakeSharedTS<FRepresentation>();
				Representation->Representation = MPDRepresentation;
				if (!Representation->CodecInfo.ParseFromRFC6381(MPDCodecs[0]))
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Could not parse Representation@codecs \"%s\", possibly unsupported codec. Ignoring this Representation."), *MPDCodecs[0]));
					continue;
				}

				// Propagate the language code from the AdaptationSet into the codec info
				Representation->CodecInfo.SetStreamLanguageCode(AdaptationSet->Language);

				// Set up codec details based on available attributes of the Representation or its enclosing AdaptationSet.
				if (Representation->CodecInfo.IsVideoCodec())
				{
					// Resolution on Representation (expected)?
					if (MPDRepresentation->GetWidth().IsSet() && MPDRepresentation->GetHeight().IsSet())
					{
						Representation->CodecInfo.SetResolution(FStreamCodecInformation::FResolution(MPDRepresentation->GetWidth().Value(), MPDRepresentation->GetHeight().Value()));
					}
					// Resolution inherited from AdaptationSet? (possible if resolution is the same for every Representation)
					else if (MPDAdaptationSet->GetWidth().IsSet() && MPDAdaptationSet->GetHeight().IsSet())
					{
						Representation->CodecInfo.SetResolution(FStreamCodecInformation::FResolution(MPDAdaptationSet->GetWidth().Value(), MPDAdaptationSet->GetHeight().Value()));
					}

					// Framerate?
					if (MPDRepresentation->GetFrameRate().IsValid())
					{
						Representation->CodecInfo.SetFrameRate(MPDRepresentation->GetFrameRate());
					}
					else if (MPDAdaptationSet->GetFrameRate().IsValid())
					{
						Representation->CodecInfo.SetFrameRate(MPDAdaptationSet->GetFrameRate());
					}

					// Aspect ratio?
					if (MPDRepresentation->GetSAR().IsValid())
					{
						Representation->CodecInfo.SetAspectRatio(FStreamCodecInformation::FAspectRatio(MPDRepresentation->GetSAR().GetNumerator(), MPDRepresentation->GetSAR().GetDenominator()));
					}
					else if (MPDAdaptationSet->GetSAR().IsValid())
					{
						Representation->CodecInfo.SetAspectRatio(FStreamCodecInformation::FAspectRatio(MPDAdaptationSet->GetSAR().GetNumerator(), MPDAdaptationSet->GetSAR().GetDenominator()));
					}

					// Update the audio codec in the adaptation set with that of the highest bandwidth.
					if (MPDRepresentation->GetBandwidth() > AdaptationSet->MaxBandwidth)
					{
						AdaptationSet->MaxBandwidth = (int32) MPDRepresentation->GetBandwidth();
						AdaptationSet->Codec = Representation->CodecInfo;
					}
				}
				else if (Representation->CodecInfo.IsAudioCodec())
				{
					// Audio sample rate tends to be a single value, but could be a range, in which case we use the lower bound.
					if (MPDRepresentation->GetAudioSamplingRate().Num())
					{
						Representation->CodecInfo.SetSamplingRate((int32) MPDRepresentation->GetAudioSamplingRate()[0]);
					}
					else if (MPDAdaptationSet->GetAudioSamplingRate().Num())
					{
						Representation->CodecInfo.SetSamplingRate((int32) MPDAdaptationSet->GetAudioSamplingRate()[0]);
					}
					
					// Get the audio channel configurations from both Representation and AdaptationSet.
					TArray<TSharedPtrTS<FDashMPD_DescriptorType>> AudioChannelConfigurations(MPDRepresentation->GetAudioChannelConfigurations());
					AudioChannelConfigurations.Append(MPDAdaptationSet->GetAudioChannelConfigurations());
					// It is also possible for audio descriptors to have ended in the Essential- or SupplementalProperty. Append those to the list as well.
					AudioChannelConfigurations.Append(MPDRepresentation->GetSupplementalProperties());
					AudioChannelConfigurations.Append(MPDRepresentation->GetEssentialProperties());
					AudioChannelConfigurations.Append(MPDAdaptationSet->GetSupplementalProperties());
					AudioChannelConfigurations.Append(MPDAdaptationSet->GetEssentialProperties());
					for(int32 nACC=0, nACCMax=AudioChannelConfigurations.Num(); nACC<nACCMax; ++nACC)
					{
						if (AudioChannelConfigurations[nACC]->GetSchemeIdUri().Equals(AudioChannelConfigurationLegacy))		// "urn:mpeg:dash:23003:3:audio_channel_configuration:2011"
						{
							// Value = channel config as per 23001-8:2013 table 8
							int32 v = 0;
							LexFromString(v, *AudioChannelConfigurations[nACC]->GetValue());
							Representation->CodecInfo.SetNumberOfChannels(v);
							break;
						}
						else if (AudioChannelConfigurations[nACC]->GetSchemeIdUri().Equals(AudioChannelConfiguration))		// "urn:mpeg:mpegB:cicp:ChannelConfiguration"
						{
							// Value = channel config as per 23001-8:2013 table 8
							int32 v = 0;
							LexFromString(v, *AudioChannelConfigurations[nACC]->GetValue());
							Representation->CodecInfo.SetNumberOfChannels(v);
							break;
						}
						else if (AudioChannelConfigurations[nACC]->GetSchemeIdUri().Equals(AudioChannelConfigurationDolby))	// "tag:dolby.com,2014:dash:audio_channel_configuration:2011"
						{
							// Ignored for now.
							continue;
						}
						else
						{
							/*
								Other audio related descriptors could be:
								   urn:mpeg:mpegB:cicp:OutputChannelPosition
								   urn:mpeg:mpegB:cicp:ProgramLoudness
								   urn:mpeg:mpegB:cicp:AnchorLoudness
								   tag:dolby.com,2014:dash:DolbyDigitalPlusExtensionType:2014
								   tag:dolby.com,2014:dash:complexityIndexTypeA:2014"
								and more.
							
								There is also information in the Accessibility descriptors like
								   urn:tva:metadata:cs:AudioPurposeCS:2007
							*/
						}
					}

					// Update the audio codec in the adaptation set with that of the highest bandwidth.
					if (MPDRepresentation->GetBandwidth() > AdaptationSet->MaxBandwidth)
					{
						AdaptationSet->MaxBandwidth = (int32) MPDRepresentation->GetBandwidth();
						AdaptationSet->Codec = Representation->CodecInfo;
					}
				}
				else if (Representation->CodecInfo.IsSubtitleCodec())
				{
					// ...

					if (MPDRepresentation->GetBandwidth() > AdaptationSet->MaxBandwidth)
					{
						AdaptationSet->MaxBandwidth = (int32) MPDRepresentation->GetBandwidth();
					}
					AdaptationSet->Codec = Representation->CodecInfo;
				}
				else
				{
					// ... ?
				}

				// Let the player decide if this representation can actually be used.
				bool bCanDecodeStream = true;
				IPlayerStreamFilter* StreamFilter = PlayerSessionServices->GetStreamFilter();
				if (StreamFilter && !StreamFilter->CanDecodeStream(Representation->CodecInfo))
				{
					// Skip it then.
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Verbose, FString::Printf(TEXT("Representation \"%s\" in Period \"%s\" rejected by application."), *MPDRepresentation->GetID(), *Period->GetID()));
					continue;
				}

				// For all intents and purposes we consider this Representation as usable now.
				Representation->bIsUsable = true;
				AdaptationSet->Representations.Emplace(Representation);
				// Add this representation to the bandwidth-to-index map.
				RepresentationQualityIndexMap.Add(MPDRepresentation->GetBandwidth(), Representation);
			}

			// If the adaptation set contains usable Representations we mark the AdaptationSet as usable as well.
			if (AdaptationSet->Representations.Num())
			{
				RepresentationQualityIndexMap.KeySort([](int32 A, int32 B){return A<B;});
				int32 CurrentQualityIndex = -1;
				int32 CurrentQualityBitrate = -1;
				for(auto& E : RepresentationQualityIndexMap)
				{
					if (E.Key != CurrentQualityBitrate)
					{
						CurrentQualityBitrate = E.Key;
						++CurrentQualityIndex;
					}
					E.Value->QualityIndex = CurrentQualityIndex;
				}
				AdaptationSet->bIsUsable = true;
				Period->AdaptationSets.Emplace(AdaptationSet);
			}

		}
	}
	return Error;
}



/**
 * Resolves an xlink request made by the initial load of the MPD.
 */
FErrorDetail FManifestDASHInternal::ResolveInitialRemoteElementRequest(IPlayerSessionServices* PlayerSessionServices, TSharedPtrTS<FMPDLoadRequestDASH> RequestResponse, FString XMLResponse, bool bSuccess)
{
	// Because this is intended solely for initial MPD entities we do not need to worry about anyone accessing our internal structures
	// while we update them. The player proper has not been informed yet that the initial manifest is ready for use.
	FErrorDetail Error;

	// Remove the request from the pending list. If it is not in there this is not a problem.
	PendingRemoteElementLoadRequests.Remove(RequestResponse);
	// Likewise for the pending element list.
	RemoteElementsToResolve.Remove(RequestResponse->XLinkElement);
	
	TSharedPtrTS<IDashMPDElement> XLinkElement = RequestResponse->XLinkElement.Pin();
	if (XLinkElement.IsValid())	
	{
		int64 LastResolveID = XLinkElement->GetXLink().LastResolveID;
		int64 NewResolveID = LastResolveID;
		FDashMPD_RootEntities RootEntities;
		TArray<TWeakPtrTS<IDashMPDElement>> NewXLinkElements;

		// Was this a dummy request to handle a resolve-to-zero in the same way as a real remote entity?
		if (XLinkElement->GetXLink().GetHref().Equals(XLinkResolveToZero))
		{
			int32 Result = ReplaceElementWithRemoteEntities(XLinkElement, RootEntities, LastResolveID, NewResolveID);
			// Result doesn't really matter as long as the element has been removed.
			if (Result < 0)
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("xlink element to resolve-to-zero was not found.")));
			}
			// Ideally the reference count of the element should now be one. If it is not the element is currently in use elsewhere,
			// which is not a problem as long as it is being released soon.
			if (!XLinkElement.IsUnique())
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("xlink element resolved-to-zero is presently being referenced.")));
			}
		}
		else if (bSuccess && !XMLResponse.IsEmpty())
		{
			// Parse the response. We expect the same root elements as is the element itself. Anything else results in an error.
			Error = IManifestParserDASH::BuildFromMPD(RootEntities, NewXLinkElements, XMLResponse.GetCharArray().GetData(), *XLinkElement->GetName(), PlayerSessionServices);
			if (Error.IsOK())
			{
				// There may not be any new xlink:actuate onLoad in the remote entities (see 5.5.3).
				bool bCircularXLink = false;
				for(int32 i=0; i<NewXLinkElements.Num(); ++i)
				{
					TSharedPtrTS<IDashMPDElement> xl = NewXLinkElements[i].Pin();
					if (xl.IsValid() && xl->GetXLink().GetActuate().Equals(XLinkActuateOnLoad))
					{
						bCircularXLink = true;
						break;
					}
				}
				// SegmentList can only have one occurrence anywhere it is a possible element.
				if (RootEntities.SegmentLists.Num() > 1)
				{
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Warning, FString::Printf(TEXT("Remote xlink element <SegmentList> cannot contain more than one instance. Ignoring all but the first.")));
				}
				if (!bCircularXLink)
				{
					int32 Result = ReplaceElementWithRemoteEntities(XLinkElement, RootEntities, LastResolveID, NewResolveID);
					// Result doesn't really matter as long as the element has been removed.
					if (Result == 0)
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("xlink element to be updated was not found. May have been removed through an MPD update.")));
					}
					else if (Result < 0)
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("xlink element could not be updated (%d)"), Result));
					}
					// Ideally the reference count of the element should now be one. If it is not the element is currently in use elsewhere,
					// which is not a problem as long as it is being released soon.
					if (!XLinkElement.IsUnique())
					{
						LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("xlink element being update is presently being referenced somewhere.")));
					}
				}
				else
				{
					XLinkElement->GetXLink().Clear();
					LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Got circular reference on remote entity (an xlink:actuate=onLoad). Invalidating xlink")));
				}
			}
			else
			{
				// An error means the original element stays in place and the xlink is removed (see 5.5.3).
				Error.Clear();
				XLinkElement->GetXLink().Clear();
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Got inappropriate target for remote entity. Invalidating xlink")));
			}
		}
		else
		{
			// Unsuccessful or an empty response. Leave the original element intact. (see 5.5.3) and remove the xlink.
			XLinkElement->GetXLink().Clear();
			if (bSuccess)
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Got empty remote entity. Invalidating xlink")));
			}
			else
			{
				LogMessage(PlayerSessionServices, IInfoLog::ELevel::Info, FString::Printf(TEXT("Failed to fetch remote entity. Invalidating xlink")));
			}
		}
	}
	// All done now?
	if (RemoteElementsToResolve.Num())
	{
		// No.
		Error.SetTryAgain();
	}
	return Error;
}


int32 FManifestDASHInternal::ReplaceElementWithRemoteEntities(TSharedPtrTS<IDashMPDElement> Element, const FDashMPD_RootEntities& NewRootEntities, int64 OldResolveID, int64 NewResolveID)
{
	if (Element.IsValid())
	{
		TSharedPtrTS<IDashMPDElement> Parent = Element->GetParentElement();
		if (Parent.IsValid())
		{
			return Parent->ReplaceChildElementWithRemoteEntities(Element, NewRootEntities, OldResolveID, NewResolveID);
		}
	}
	return 0;
}




} // namespace Electra

