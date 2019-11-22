// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class SComboButton;
class UDataprepFilter;

/**
 * The dataprep fetcher selector is widget made to change and display the current type of fetcher of a dataprep filter
 */
class SDataprepFetcherSelector : public SCompoundWidget, public FGCObject
{
	SLATE_BEGIN_ARGS(SDataprepFetcherSelector) {}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UDataprepFilter& InFilter);

private:
	// Those function are for the fetcher display
	TSharedRef<SWidget> GetFetcherTypeSelector() const;
	FText GetFetcherNameText() const;

	//~ FGCObject interface
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	//~ End FGCObject interface

	UDataprepFilter* Filter;
	TSharedPtr<SComboButton> FetcherTypeButton;
};
