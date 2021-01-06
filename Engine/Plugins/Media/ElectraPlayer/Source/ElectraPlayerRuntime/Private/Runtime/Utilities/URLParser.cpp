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
		virtual FString GetPath() const override;
		virtual void GetPathComponents(TArray<FString>& OutPathComponents) const override;

		virtual FString ResolveWith(const FString& RelativeURL) const override;


	private:
		Utilities::FURI* URLHelper;
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
		if (!URLHelper)
		{
			return UEMEDIA_ERROR_OOM;
		}
		return URLHelper->IsValid() ? UEMEDIA_ERROR_OK : UEMEDIA_ERROR_FORMAT_ERROR;
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

	FString FURLParser::ResolveWith(const FString& RelativeURL) const
	{
		if (URLHelper)
		{
			return URLHelper->Resolve(Utilities::FURI::Parse(RelativeURL)).Format();
		}
		return FString();
	}



} // namespace Electra


