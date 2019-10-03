// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SourcesSearch.h"
#include "Widgets/Input/SSearchBox.h"
#include "Framework/Application/SlateApplication.h"

void FSourcesSearch::Initialize()
{
	SearchBox = SNew(SSearchBox)
		.OnTextChanged(this, &FSourcesSearch::OnSearchBoxTextChanged)
		.OnTextCommitted(this, &FSourcesSearch::OnSearchBoxTextCommitted);
}

void FSourcesSearch::ClearSearch()
{
	if (!SearchBox->GetSearchText().IsEmpty())
	{
		SearchBox->SetSearchText(FText::GetEmpty());
	}
}

void FSourcesSearch::SetHintText(const TAttribute<FText>& InHintText)
{
	SearchBox->SetHintText(InHintText);
}

TSharedRef<SWidget> FSourcesSearch::GetWidget() const
{
	return SearchBox.ToSharedRef();
}

void FSourcesSearch::OnSearchBoxTextChanged(const FText& InSearchText)
{
	TArray<FText> SearchErrors;
	OnSearchChangedDelegate.Broadcast(InSearchText, SearchErrors);

	if (SearchErrors.Num() == 0)
	{
		SearchBox->SetError(FText::GetEmpty());
	}
	else if (SearchErrors.Num() == 1)
	{
		SearchBox->SetError(SearchErrors[0]);
	}
	else
	{
		FTextBuilder CombinedError;
		for (const FText& SearchError : SearchErrors)
		{
			CombinedError.AppendLine(SearchError);
		}
		SearchBox->SetError(CombinedError.ToText());
	}
}

void FSourcesSearch::OnSearchBoxTextCommitted(const FText& InSearchText, ETextCommit::Type InCommitType)
{
	if (InCommitType == ETextCommit::OnCleared)
	{
		// Clear the search box
		ClearSearch();
		FSlateApplication::Get().ClearKeyboardFocus(EFocusCause::Cleared);
	}
}
