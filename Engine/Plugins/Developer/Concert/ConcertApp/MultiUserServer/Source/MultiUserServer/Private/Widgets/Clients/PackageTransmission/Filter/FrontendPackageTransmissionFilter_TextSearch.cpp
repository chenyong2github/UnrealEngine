// Copyright Epic Games, Inc. All Rights Reserved.

#include "FrontendPackageTransmissionFilter_TextSearch.h"

#include "Widgets/Clients/PackageTransmission/Util/PackageTransmissionEntryTokenizer.h"

namespace UE::MultiUserServer
{
	FPackageTransmissionFilter_TextSearch::FPackageTransmissionFilter_TextSearch(TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer)
		: TextFilter(TTextFilter<const FPackageTransmissionEntry&>::FItemToStringArray::CreateRaw(this, &FPackageTransmissionFilter_TextSearch::GenerateSearchTerms))
		, Tokenizer(MoveTemp(Tokenizer))
	{
		TextFilter.OnChanged().AddLambda([this]
		{
			OnChanged().Broadcast();
		});
	}

	void FPackageTransmissionFilter_TextSearch::GenerateSearchTerms(const FPackageTransmissionEntry& InItem, TArray<FString>& OutTerms) const
	{
		OutTerms.Add(Tokenizer->TokenizeTime(InItem));
		OutTerms.Add(Tokenizer->TokenizeOrigin(InItem));
		OutTerms.Add(Tokenizer->TokenizeDestination(InItem));
		OutTerms.Add(Tokenizer->TokenizeSize(InItem));
		OutTerms.Add(Tokenizer->TokenizeRevision(InItem));
		OutTerms.Add(Tokenizer->TokenizePackagePath(InItem));
		OutTerms.Add(Tokenizer->TokenizePackageName(InItem));
	}

	FFrontendPackageTransmissionFilter_TextSearch::FFrontendPackageTransmissionFilter_TextSearch(TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer)
		: Super(MoveTemp(Tokenizer))
	{
		ChildSlot = SNew(SSearchBox)
		.OnTextChanged_Lambda([this](const FText& NewSearchText)
		{
			Implementation.SetRawFilterText(NewSearchText);
			SearchText = NewSearchText;
		})
		.DelayChangeNotificationsWhileTyping(true);
	}
}
