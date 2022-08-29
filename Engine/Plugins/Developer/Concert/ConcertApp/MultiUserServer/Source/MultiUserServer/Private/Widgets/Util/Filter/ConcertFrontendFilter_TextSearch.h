// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ConcertFrontendFilter.h"
#include "Misc/TextFilter.h"
#include "Widgets/Input/SSearchBox.h"

class FConcertLogTokenizer;

namespace UE::MultiUserServer
{
	/** Allows advanced search by text. Implements Adapter pattern to wrap TTextFilter. */
	template<typename TFilterType>
	class TConcertFilter_TextSearch : public TConcertFilter<TFilterType>
	{
		using Super = TConcertFilter<TFilterType>;
	public:

		TConcertFilter_TextSearch()
			: TextFilter(TTextFilter<TFilterType>::template FItemToStringArray::CreateRaw(this, &TConcertFilter_TextSearch<TFilterType>::GenerateSearchTerms))
		{
			TextFilter.OnChanged().AddLambda([this]
			{
				Super::template OnChanged().Broadcast();
			});
		}
	
		//~ Begin FConcertLogFilter Interface
		virtual bool PassesFilter(TFilterType InItem) const override { return TextFilter.PassesFilter(InItem); }
		//~ End FConcertLogFilter Interface
	
		void SetRawFilterText(const FText& InFilterText) { TextFilter.SetRawFilterText(InFilterText); }

	protected:
		
		/** Parses InItem into a bunch of strings that can be searched */
		virtual void GenerateSearchTerms(TFilterType InItem, TArray<FString>& OutTerms) const = 0;
		
	private:

		/** Does the actual string search */
		TTextFilter<TFilterType> TextFilter;
	};

	/** Creates a search bar */
	template<typename TTextSearchFilterType /* Expected subtype of TConcertFilter_TextSearch */, typename TFilterType>
	class TConcertFrontendFilter_TextSearch : public TConcertFrontendFilterAggregate<TTextSearchFilterType, TFilterType, SSearchBox>
	{
		using Super = TConcertFrontendFilterAggregate<TTextSearchFilterType, TFilterType, SSearchBox>;
	public:

		template<typename... TArg>
		TConcertFrontendFilter_TextSearch(TArg&&... Arg)
			: Super(Forward<TArg>(Arg)...)
		{
			Super::template ChildSlot = SNew(SSearchBox)
				.OnTextChanged_Lambda([this](const FText& NewSearchText)
				{
					SearchText = NewSearchText;
					TTextSearchFilterType& Impl = Super::template Implementation;
					Impl.SetRawFilterText(NewSearchText);
				})
				.DelayChangeNotificationsWhileTyping(true);
		}
		
		const FText& GetSearchText() const { return SearchText; }

	private:

		FText SearchText;
	};
}


