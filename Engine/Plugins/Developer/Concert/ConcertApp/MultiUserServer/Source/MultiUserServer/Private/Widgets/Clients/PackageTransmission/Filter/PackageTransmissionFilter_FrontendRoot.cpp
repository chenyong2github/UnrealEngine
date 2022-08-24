// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageTransmissionFilter_FrontendRoot.h"

#include "Widgets/Clients/PackageTransmission/Model/PackageTransmissionEntry.h"
#include "Widgets/Clients/PackageTransmission/Filter/FrontendPackageTransmissionFilter_TextSearch.h"

#include "Algo/AllOf.h"
#include "Widgets/SBoxPanel.h"

namespace UE::MultiUserServer
{
	FPackageTransmissionFilter_FrontendRoot::FPackageTransmissionFilter_FrontendRoot(
		TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer,
		TArray<TSharedRef<FFrontendPackageTransmissionFilter>> InCustomFilters,
		const TArray<TSharedRef<FPackageTransmissionFilter>>& NonVisualFilters)
		: TextSearchFilter(MakeShared<FFrontendPackageTransmissionFilter_TextSearch>(MoveTemp(Tokenizer)))
		, FrontendFilters(MoveTemp(InCustomFilters))
		, AllFilters(FrontendFilters)
	{
		AllFilters.Add(TextSearchFilter);
		for (const TSharedRef<FPackageTransmissionFilter>& NonVisual : NonVisualFilters)
		{
			AllFilters.Add(NonVisual);
		}
	
		for (const TSharedRef<FPackageTransmissionFilter>& Filter : AllFilters)
		{
			Filter->OnChanged().AddLambda([this]()
			{
				OnChanged().Broadcast();
			});
		}
	}

	TSharedRef<SWidget> FPackageTransmissionFilter_FrontendRoot::BuildFilterWidgets() const
	{
		return SNew(SVerticalBox)

			// Search bar
			+SVerticalBox::Slot()
			.AutoHeight()
			[
				TextSearchFilter->GetFilterWidget()
			]

			+SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0, 2)
			[
				BuildCustomFilterListWidget()
			];
	}

	bool FPackageTransmissionFilter_FrontendRoot::PassesFilter(const FPackageTransmissionEntry& InItem) const
	{
		return Algo::AllOf(
			AllFilters,
			[&InItem](const TSharedRef<FPackageTransmissionFilter>& AndFilter){ return AndFilter->PassesFilter(InItem); }
		);
	}

	TSharedRef<SWidget> FPackageTransmissionFilter_FrontendRoot::BuildCustomFilterListWidget() const
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);

		bool bIsFirst = true;
		for (const TSharedRef<FFrontendPackageTransmissionFilter>& Filter : FrontendFilters)
		{
			const FMargin Margin = bIsFirst ? FMargin() : FMargin(8, 0, 0, 0);
			bIsFirst = false;
			Box->AddSlot()
				.AutoWidth()
				.Padding(Margin)
				.VAlign(VAlign_Center)
				[
					Filter->GetFilterWidget()
				];
		}

		return Box;
	}

	TSharedRef<FPackageTransmissionFilter_FrontendRoot> MakeFilter(TSharedRef<FPackageTransmissionEntryTokenizer> Tokenizer)
	{
		TArray<TSharedRef<FFrontendPackageTransmissionFilter>> FrontendFilters;
		return MakeShared<FPackageTransmissionFilter_FrontendRoot>(MoveTemp(Tokenizer), FrontendFilters);
	}
}
