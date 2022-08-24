// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PackageTransmissionFilter.h"

class FEndpointToUserNameCache;
class SWidget;

namespace UE::MultiUserServer
{
	class FFrontendPackageTransmissionFilter;
	class FFrontendPackageTransmissionFilter_TextSearch;
	class FPackageTransmissionEntryTokenizer;

	/** A filter that contains multiple UI filters */
	class FPackageTransmissionFilter_FrontendRoot : public FPackageTransmissionFilter, public TSharedFromThis<FPackageTransmissionFilter_FrontendRoot>
	{
	public:

		FPackageTransmissionFilter_FrontendRoot(
			TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer,
			TArray<TSharedRef<FFrontendPackageTransmissionFilter>> InCustomFilters,
			const TArray<TSharedRef<FPackageTransmissionFilter>>& NonVisualFilters = {}
			);

		/** Builds the widget view for all contained filters */
		TSharedRef<SWidget> BuildFilterWidgets() const;
		
		//~ Begin IFilter Interface
		virtual bool PassesFilter(const FPackageTransmissionEntry& InItem) const override;
		//~ End IFilter Interface

		FORCEINLINE const TSharedRef<FFrontendPackageTransmissionFilter_TextSearch>& GetTextSearchFilter() const { return TextSearchFilter; }

	private:

		/** The text search filter. Also in FrontendFilters. Separate variable to build search bar in new line. */
		TSharedRef<FFrontendPackageTransmissionFilter_TextSearch> TextSearchFilter;

		/** AllFilters without special filters we have as properties above, such as TextSearchFilter. */
		TArray<TSharedRef<FFrontendPackageTransmissionFilter>> FrontendFilters;
		
		/** Filters that are combined using logical AND. */
		TArray<TSharedRef<FPackageTransmissionFilter>> AllFilters;

		/** Builds the widgets that go under the text */
		TSharedRef<SWidget> BuildCustomFilterListWidget() const;
	};

	/** Creates a filter for the global filter log window */
	TSharedRef<FPackageTransmissionFilter_FrontendRoot> MakeFilter(TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer);
}
