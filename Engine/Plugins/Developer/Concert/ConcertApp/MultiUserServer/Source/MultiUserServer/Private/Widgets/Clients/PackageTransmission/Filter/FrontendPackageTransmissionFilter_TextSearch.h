// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FrontendPackageTransmissionFilter.h"
#include "Misc/TextFilter.h"
#include "Widgets/Input/SSearchBox.h"

namespace UE::MultiUserServer
{
	class FPackageTransmissionEntryTokenizer;

	class FPackageTransmissionFilter_TextSearch : public FPackageTransmissionFilter
	{
	public:
		
		FPackageTransmissionFilter_TextSearch(TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer);

		//~ Begin FPackageTransmissionFilter Interface
		virtual bool PassesFilter(const FPackageTransmissionEntry& InItem) const override { return TextFilter.PassesFilter(InItem); }
		//~ End FPackageTransmissionFilter Interface
		
		void SetRawFilterText(const FText& InFilterText) { TextFilter.SetRawFilterText(InFilterText); }
	
	private:

		/** Does the actual string search */
		TTextFilter<const FPackageTransmissionEntry&> TextFilter;
		/** Helps in converting FConcertLog members into search terms */
		TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer;

		/** Parses InItem into a bunch of strings that can be searched */
		void GenerateSearchTerms(const FPackageTransmissionEntry& InItem, TArray<FString>& OutTerms) const;
	};
	
    class FFrontendPackageTransmissionFilter_TextSearch : public TFrontendPackageTransmissionFilterAggregate<FPackageTransmissionFilter_TextSearch, SSearchBox>
    {
		using Super = TFrontendPackageTransmissionFilterAggregate<FPackageTransmissionFilter_TextSearch, SSearchBox>;
    public:
    	
    	FFrontendPackageTransmissionFilter_TextSearch(TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer);

    	FText GetSearchText() const { return SearchText; }

    private:

    	FText SearchText;
    };
}

