// Copyright Epic Games, Inc. All Rights Reserved.

#include "Utilities/URLParser.h"
#include "Utilities/URI.h"
#include "Utilities/StringHelpers.h"


namespace Electra
{

	class FURLParser : public IURLParser
	{
	public:
		FURLParser();
		virtual ~FURLParser();

		virtual UEMediaError ParseURL(const FString& URL) override;
		virtual bool IsAbsoluteURL() const override;
		virtual FString GetPath() const override;
		virtual void GetPathComponents(TArray<FString>& OutPathComponents) const override;

		virtual FString ResolveWith(const FString& RelativeURL) override;

		virtual FString GetURL() const
		{
			return CurrentURL;
		}

	private:
		Utilities::FURI* URLHelper;
		FString CurrentURL;
	};


	IURLParser* IURLParser::Create()
	{
		return new FURLParser;
	}



	FURLParser::FURLParser()
		: URLHelper(nullptr)
	{
	}

	FURLParser::~FURLParser()
	{
		delete URLHelper;
	}

	UEMediaError FURLParser::ParseURL(const FString& URL)
	{
		delete URLHelper;
		URLHelper = new Utilities::FURI(Utilities::FURI::Parse(URL));
		if (!URLHelper->IsValid())
		{
			return UEMEDIA_ERROR_FORMAT_ERROR;
		}
		CurrentURL = URL;
		return UEMEDIA_ERROR_OK;
	}

	bool FURLParser::IsAbsoluteURL() const
	{
		if (URLHelper)
		{
			return URLHelper->Scheme.Len() > 0;
		}
		return false;
	}


	FString FURLParser::GetPath() const
	{
		return URLHelper ? URLHelper->Path : FString();
	}

	void FURLParser::GetPathComponents(TArray<FString>& OutPathComponents) const
	{
		OutPathComponents.Empty();
		if (URLHelper)
		{
			static const FString kTextSlash(TEXT("/"));
			StringHelpers::SplitByDelimiter(OutPathComponents, URLHelper->Path, kTextSlash);
		}
	}

	FString FURLParser::ResolveWith(const FString& RelativeURL)
	{
		if (URLHelper)
		{
			CurrentURL = URLHelper->Resolve(Utilities::FURI::Parse(RelativeURL)).Format();
		}
		return CurrentURL;
	}

	/***************************************************************************************************************************************************/
	/***************************************************************************************************************************************************/
	/***************************************************************************************************************************************************/

	static inline void _SwapStrings(FString& a, FString& b)
	{
		FString s = MoveTemp(a);
		a = MoveTemp(b);
		b = MoveTemp(s);
	}

	void FURL_RFC3986::Empty()
	{
		Scheme.Empty();
		UserInfo.Empty();
		Host.Empty();
		Port.Empty();
		Path.Empty();
		Query.Empty();
		Fragment.Empty();
	}

	void FURL_RFC3986::Swap(FURL_RFC3986& Other)
	{
		_SwapStrings(Scheme, Other.Scheme);
		_SwapStrings(UserInfo, Other.UserInfo);
		_SwapStrings(Host, Other.Host);
		_SwapStrings(Port, Other.Port);
		_SwapStrings(Path, Other.Path);
		_SwapStrings(Query, Other.Query);
		_SwapStrings(Fragment, Other.Fragment);
	}

	bool FURL_RFC3986::Parse(const FString& InURL)
	{
		Empty();
		if (InURL.IsEmpty())
		{
			return true;
		}
		StringHelpers::FStringIterator it(InURL);
		if (*it != TCHAR('/') && *it != TCHAR('.') && *it != TCHAR('?') && *it != TCHAR('#'))
		{
			FString Component;
			Component.Empty();
			while(it && *it != TCHAR(':') && *it != TCHAR('?') && *it != TCHAR('#') && *it != TCHAR('/'))
			{
				Component += *it++;
			}
			if (it && *it == TCHAR(':'))
			{
				++it;
				if (!it)
				{
					// Scheme must be followed by authority or path
					return false;
				}
				Scheme = Component;
				if (*it == TCHAR('/'))
				{
					++it;
					if (it && *it == TCHAR('/'))
					{
						++it;
						if (!ParseAuthority(it))
						{
							return false;
						}
					}
					else
					{
						--it;
					}
				}
				if (!ParsePathAndQueryFragment(it))
				{
					return false;
				}
			}
			else
			{
				it.Reset();
				return ParsePathAndQueryFragment(it);
			}
		}
		else
		{
			return ParsePathAndQueryFragment(it);
		}
		return true;
	}

	FString FURL_RFC3986::GetScheme() const
	{
		return Scheme;
	}

	FString FURL_RFC3986::GetHost() const
	{
		return Host;
	}

	FString FURL_RFC3986::GetPath() const
	{
		return Path;
	}

	FString FURL_RFC3986::GetQuery() const
	{
		return Query;
	}

	FString FURL_RFC3986::GetFragment() const
	{
		return Fragment;
	}

	FString FURL_RFC3986::Get(bool bIncludeQuery, bool bIncludeFragment)
	{
		static const FString RequiredEscapeCharsPath(TEXT("?#"));

		FString URL;
		if (IsAbsolute())
		{
			FString Authority = GetAuthority();
			URL = Scheme;
			URL += TEXT(":");
			if (Authority.Len() || Scheme.Equals(TEXT("file")))
			{
				URL += TEXT("//");
				URL += Authority;
			}
			if (Path.Len())
			{
				if (Authority.Len() && Path[0] != TCHAR('/'))
				{
					URL += TEXT("/");
				}
				UrlEncode(URL, Path, RequiredEscapeCharsPath);
			}
			else if (Query.Len() || Fragment.Len())
			{
				URL += TEXT("/");
			}
		}
		else
		{
			UrlEncode(URL, Path, RequiredEscapeCharsPath);
		}
		if (Query.Len())
		{
			URL += TEXT("?");
			URL += Query;
		}
		if (Fragment.Len())
		{
			URL += TEXT("#");
			UrlEncode(URL, Fragment, FString());
		}
		return URL;
	}

	void FURL_RFC3986::GetQueryParams(TArray<FQueryParam>& OutQueryParams, const FString& InQueryParameters, bool bPerformUrlDecoding, bool bSameNameReplacesValue)
	{
		TArray<FString> ValuePairs;
		InQueryParameters.ParseIntoArray(ValuePairs, TEXT("&"), true);
		for(int32 i=0; i<ValuePairs.Num(); ++i)
		{
			int32 EqPos = 0;
			ValuePairs[i].FindChar(TCHAR('='), EqPos);
			FQueryParam qp;
			bool bOk = true;
			if (bPerformUrlDecoding)
			{
				bOk = UrlDecode(qp.Name, ValuePairs[i].Mid(0, EqPos)) && UrlDecode(qp.Value, ValuePairs[i].Mid(EqPos+1));
			}
			else
			{
				qp.Name = ValuePairs[i].Mid(0, EqPos);
				qp.Value = ValuePairs[i].Mid(EqPos+1);
			}
			if (bOk)
			{
				if (!bSameNameReplacesValue)
				{
					OutQueryParams.Emplace(MoveTemp(qp));
				}
				else
				{
					bool bFound = false;
					for(int32 j=0; j<OutQueryParams.Num(); ++j)
					{
						if (OutQueryParams[j].Name.Equals(qp.Name))
						{
							OutQueryParams[j] = MoveTemp(qp);
							bFound = true;
							break;
						}
					}
					if (!bFound)
					{
						OutQueryParams.Emplace(MoveTemp(qp));
					}
				}
			}
		}
	}

	void FURL_RFC3986::GetQueryParams(TArray<FQueryParam>& OutQueryParams, bool bPerformUrlDecoding, bool bSameNameReplacesValue)
	{
		GetQueryParams(OutQueryParams, Query, bPerformUrlDecoding, bSameNameReplacesValue);
	}

	// Appends or prepends additional query parameters.
	void FURL_RFC3986::AddQueryParameters(const FString& InQueryParameters, bool bAppend)
	{
		if (InQueryParameters.Len() > 0)
		{
			FString qp(InQueryParameters);
			if (qp[0] == TCHAR('?') || qp[0] == TCHAR('&'))
			{
				qp.RightChopInline(1, false);
			}
			if (qp.Len() && qp[qp.Len()-1] == TCHAR('&'))
			{
				qp.LeftChopInline(1, false);
			}
			if (bAppend)
			{
				if (Query.Len())
				{
					Query.AppendChar(TCHAR('&'));
				}
				Query.Append(qp);
			}
			else
			{
				if (Query.Len())
				{
					Query = qp + TCHAR('&') + Query;
				}
				else
				{
					Query = qp;
				}
			}
		}
	}


	bool FURL_RFC3986::IsAbsolute() const
	{
		return Scheme.Len() > 0;
	}

	void FURL_RFC3986::GetPathComponents(TArray<FString>& OutPathComponents) const
	{
		GetPathComponents(OutPathComponents, GetPath());
	}

	FString FURL_RFC3986::GetAuthority() const
	{
		FString Authority;
		if (UserInfo.Len())
		{
			Authority += UserInfo;
			Authority += TEXT("@");
		}
		// IPv6 address?
		int32 DummyPos = 0;
		if (Host.FindChar(TCHAR(':'), DummyPos))
		{
			Authority += TEXT("[");
			Authority += Host;
			Authority += TEXT("]");
		}
		else
		{
			Authority += Host;
		}
		if (Port.Len())
		{
			Authority += TEXT(":");
			Authority += Port;
		}
		return Authority;
	}


	bool FURL_RFC3986::HasSameOriginAs(const FURL_RFC3986& Other)
	{
		/* RFC 6454:
			5.  Comparing Origins

			   Two origins are "the same" if, and only if, they are identical.  In
			   particular:

			   o  If the two origins are scheme/host/port triples, the two origins
				  are the same if, and only if, they have identical schemes, hosts,
				  and ports.
		*/
		return Scheme.Equals(Other.Scheme) && Host.Equals(Other.Host) && Port.Equals(Other.Port);
	}


	bool FURL_RFC3986::ParseAuthority(StringHelpers::FStringIterator& it)
	{
		UserInfo.Empty();
		FString SubPart;
		while(it && *it != TCHAR('/') && *it != TCHAR('?') && *it != TCHAR('#'))
		{
			if (*it == TCHAR('@'))
			{
				UserInfo = SubPart;
				SubPart.Empty();
			}
			else
			{
				SubPart += *it;
			}
			++it;
		}
		StringHelpers::FStringIterator SubIt(SubPart);
		return ParseHostAndPort(SubIt);
	}

	bool FURL_RFC3986::ParseHostAndPort(StringHelpers::FStringIterator& it)
	{
		if (!it)
		{
			return true;
		}
		Host.Empty();
		// IPv6 adress?
		if (*it == TCHAR('['))
		{
			++it;
			while(it && *it != TCHAR(']'))
			{
				Host += *it++;
			}
			if (!it)
			{
				// Missing closing ']'
				return false;
			}
			++it;
		}
		else
		{
			while(it && *it != TCHAR(':'))
			{
				Host += *it++;
			}
		}
		if (it && *it == TCHAR(':'))
		{
			++it;
			Port.Empty();
			while(it)
			{
				Port += *it++;
			}
		}
		return true;
	}

	bool FURL_RFC3986::ParsePathAndQueryFragment(StringHelpers::FStringIterator& it)
	{
		if (it)
		{
			if (*it != TCHAR('?') && *it != TCHAR('#'))
			{
				if (!ParsePath(it))
				{
					return false;
				}
			}
			if (it && *it == TCHAR('?'))
			{
				++it;
				if (!ParseQuery(it))
				{
					return false;
				}
			}
			if (it && *it == TCHAR('#'))
			{
				++it;
				return ParseFragment(it);
			}
		}
		return true;
	}

	bool FURL_RFC3986::ParsePath(StringHelpers::FStringIterator& it)
	{
		Path.Empty();
		while(it && *it != TCHAR('?') && *it != TCHAR('#'))
		{
			Path += *it++;
		}
		FString Decoded;
		if (UrlDecode(Decoded, Path))
		{
			Path = Decoded;
			return true;
		}
		return false;
	}

	bool FURL_RFC3986::ParseQuery(StringHelpers::FStringIterator& it)
	{
		Query.Empty();
		while(it && *it != TCHAR('#'))
		{
			Query += *it++;
		}
		return true;
	}

	bool FURL_RFC3986::ParseFragment(StringHelpers::FStringIterator& it)
	{
		Fragment.Empty();
		while(it)
		{
			Fragment += *it++;
		}
		FString Decoded;
		if (UrlDecode(Decoded, Fragment))
		{
			Fragment = Decoded;
			return true;
		}
		return false;
	}

	bool FURL_RFC3986::UrlDecode(FString& OutResult, const FString& InUrlToDecode)
	{
		StringHelpers::FStringIterator it(InUrlToDecode);
		while(it)
		{
			TCHAR c = *it++;
			if (c == TCHAR('%'))
			{
				if (!it)
				{
					// '%' at the end with nothing following!
					return false;
				}
				TCHAR hi = *it++;
				if (!it)
				{
					// Only one char after '%'. Need two!!
					return false;
				}
				TCHAR lo = *it++;
				if (lo >= TCHAR('0') && lo <= TCHAR('9'))
				{
					lo -= TCHAR('0');
				}
				else if (lo >= TCHAR('a') && lo <= TCHAR('f'))
				{
					lo = lo - TCHAR('a') + 10;
				}
				else if (lo >= TCHAR('A') && lo <= TCHAR('F'))
				{
					lo = lo - TCHAR('A') + 10;
				}
				else
				{
					// Not a hex digit!
					return false;
				}
				if (hi >= TCHAR('0') && hi <= TCHAR('9'))
				{
					hi -= TCHAR('0');
				}
				else if (hi >= TCHAR('a') && hi <= TCHAR('f'))
				{
					hi = hi - TCHAR('a') + 10;
				}
				else if (hi >= TCHAR('A') && hi <= TCHAR('F'))
				{
					hi = hi - TCHAR('A') + 10;
				}
				else
				{
					// Not a hex digit!
					return false;
				}

				c = hi * 16 + lo;
			}
			OutResult += c;
		}
		return true;
	}

	bool FURL_RFC3986::UrlEncode(FString& OutResult, const FString& InUrlToEncode, const FString& InReservedChars)
	{
		static const FString RequiredEscapeChars(TEXT("%<>{}|\\\"^`"));
		int32 DummyPos = 0;
		for(StringHelpers::FStringIterator it(InUrlToEncode); it; ++it)
		{
			TCHAR c = *it;
			if ((c >= TCHAR('a') && c <= TCHAR('z')) || (c >= TCHAR('0') && c <= TCHAR('9')) || (c >= TCHAR('A') && c <= TCHAR('Z')) || c == TCHAR('-') || c == TCHAR('_') || c == TCHAR('.') || c == TCHAR('~'))
			{
				OutResult += c;
			}
			else if (c <= 0x20 || c >= 0x7f || RequiredEscapeChars.FindChar(c, DummyPos) || InReservedChars.FindChar(c, DummyPos))
			{
				OutResult += FString::Printf(TEXT("%%%02X"), c);
			}
			else
			{
				OutResult += c;
			}
		}
		return true;
	}

	void FURL_RFC3986::GetPathComponents(TArray<FString>& OutPathComponents, const FString& InPath)
	{
		TArray<FString> Components;
		InPath.ParseIntoArray(Components, TEXT("/"), true);
		OutPathComponents.Append(Components);
	}

	void FURL_RFC3986::ResolveWith(const FString& InChildURL)
	{
		FURL_RFC3986 Other;
		if (Other.Parse(InChildURL))
		{
			ResolveWith(Other);
		}
	}

	void FURL_RFC3986::ResolveAgainst(const FString& InParentURL)
	{
		FURL_RFC3986 Parent;
		if (Parent.Parse(InParentURL))
		{
			Parent.ResolveWith(*this);
			Swap(Parent);
		}
	}
	
	/**
	 * Resolve URL as per RFC 3986 section 5.2
	 */
	void FURL_RFC3986::ResolveWith(const FURL_RFC3986& Other)
	{
		if (!Other.Scheme.IsEmpty())
		{
			Scheme   = Other.Scheme;
			UserInfo = Other.UserInfo;
			Host     = Other.Host;
			Port     = Other.Port;
			Path     = Other.Path;
			Query    = Other.Query;
			RemoveDotComponents();
		}
		else
		{
			if (!Other.Host.IsEmpty())
			{
				UserInfo = Other.UserInfo;
				Host     = Other.Host;
				Port     = Other.Port;
				Path     = Other.Path;
				Query    = Other.Query;
				RemoveDotComponents();
			}
			else
			{
				if (Other.Path.IsEmpty())
				{
					if (!Other.Query.IsEmpty())
					{
						Query = Other.Query;
					}
				}
				else
				{
					if (Other.Path[0] == TCHAR('/'))
					{
						Path = Other.Path;
						RemoveDotComponents();
					}
					else
					{
						AppendPath(Other.Path);
					}
					Query = Other.Query;
				}
			}
		}
		Fragment = Other.Fragment;
	}

	void FURL_RFC3986::RebuildPathFromComponents(const TArray<FString>& Components, bool bAddLeadingSlash, bool bAddTrailingSlash)
	{
		Path.Empty();
		int32 DummyPos = 0;
		for(int32 i=0, iMax=Components.Num(); i<iMax; ++i)
		{
			if (i == 0)
			{
				if (bAddTrailingSlash)
				{
					Path = TEXT("/");
				}
				else if (Scheme.IsEmpty() && Components[0].FindChar(TCHAR(':'), DummyPos))
				{
					Path = TEXT("./");
				}
			}
			else
			{
				Path += TCHAR('/');
			}
			Path += Components[i];
		}
		if (bAddTrailingSlash)
		{
			Path += TCHAR('/');
		}
	}

	void FURL_RFC3986::AppendPath(const FString& InPathToAppend)
	{
		TArray<FString> Components;
		TArray<FString> NormalizedComponents;
		bool bAddLeadingSlash = false;
		if (!Path.IsEmpty())
		{
			GetPathComponents(Components);
			bool bEndsWithSlash = Path.EndsWith(TEXT("/"));
			if (!bEndsWithSlash && Components.Num())
			{
				Components.Pop();
			}
			bAddLeadingSlash = Path[0] == TCHAR('/');
		}
		GetPathComponents(Components, InPathToAppend);
		bAddLeadingSlash = bAddLeadingSlash || (InPathToAppend.Len() && InPathToAppend[0] == TCHAR('/'));
		bool bHasTrailingSlash = InPathToAppend.Len() && InPathToAppend.EndsWith(TEXT("/"));
		bool bAddTrailingSlash = false;
		for(int32 i=0,iMax=Components.Num(); i<iMax; ++i)
		{
			if (Components[i].Equals(TEXT("..")))
			{
				bAddTrailingSlash = true;
				if (NormalizedComponents.Num())
				{
					NormalizedComponents.Pop();
				}
			}
			else if (!Components[i].Equals(TEXT(".")))
			{
				bAddTrailingSlash = false;
				NormalizedComponents.Add(Components[i]);
			}
			else 
			{
				bAddTrailingSlash = true;
			}
		}
		RebuildPathFromComponents(NormalizedComponents, bAddLeadingSlash, bHasTrailingSlash || bAddTrailingSlash);
	}

	void FURL_RFC3986::RemoveDotComponents()
	{
		if (Path.Len())
		{
			bool bHasLeadingSlash = Path[0] == TCHAR('/');
			bool bHasTrailingSlash = Path.EndsWith(TEXT("/"));
			TArray<FString> Components;
			TArray<FString> NormalizedComponents;
			GetPathComponents(Components);
			for(int32 i=0,iMax=Components.Num(); i<iMax; ++i)
			{
				if (Components[i].Equals(TEXT("..")))
				{
					if (NormalizedComponents.Num())
					{
						if (NormalizedComponents.Last().Equals(TEXT("..")))
						{
							NormalizedComponents.Add(Components[i]);
						}
						else
						{
							NormalizedComponents.Pop();
						}
					}
				}
				else if (!Components[i].Equals(TEXT(".")))
				{
					NormalizedComponents.Add(Components[i]);
				}
			}
			RebuildPathFromComponents(NormalizedComponents, bHasLeadingSlash, bHasTrailingSlash);
		}
	}



} // namespace Electra


