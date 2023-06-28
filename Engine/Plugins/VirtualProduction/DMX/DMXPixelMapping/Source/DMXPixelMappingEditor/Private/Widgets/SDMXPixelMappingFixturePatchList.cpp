// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXPixelMappingFixturePatchList.h"

#include "Algo/MaxElement.h"
#include "DMXPixelMapping.h"
#include "Components/DMXPixelMappingBaseComponent.h"
#include "Components/DMXPixelMappingFixtureGroupItemComponent.h"
#include "Components/DMXPixelMappingMatrixComponent.h"
#include "Library/DMXEntityFixturePatch.h"
#include "Library/DMXLibrary.h"
#include "Toolkits/DMXPixelMappingToolkit.h"


void SDMXPixelMappingFixturePatchList::Construct(const FArguments& InArgs, const TSharedPtr<FDMXPixelMappingToolkit>& InToolkit)
{
	if (!InToolkit.IsValid())
	{
		return;
	}
	WeakToolkit = InToolkit;

	SDMXReadOnlyFixturePatchList::Construct(SDMXReadOnlyFixturePatchList::FArguments()
		.ListDescriptor(InArgs._ListDescriptor)
		.OnContextMenuOpening(InArgs._OnContextMenuOpening)
		.OnRowDragged(InArgs._OnRowDragged));
}

void SDMXPixelMappingFixturePatchList::SelectAfter(const TArray<TSharedPtr<FDMXEntityFixturePatchRef>>& FixturePatches)
{
	const TSharedPtr<FDMXEntityFixturePatchRef>* MaxFixturePatchPtr = Algo::MaxElementBy(FixturePatches, [](const TSharedPtr<FDMXEntityFixturePatchRef>& FixturePatchRef)
		{
			const UDMXEntityFixturePatch* FixturePatch = FixturePatchRef.IsValid() ? FixturePatchRef->GetFixturePatch() : nullptr;
			return FixturePatch ? FixturePatch->GetUniverseID() * DMX_UNIVERSE_SIZE + FixturePatch->GetStartingChannel() : -1;
		});

	const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> Items = GetListItems();
	if (MaxFixturePatchPtr)
	{
		int32 IndexOfPatch = Items.IndexOfByPredicate([MaxFixturePatchPtr](const TSharedPtr<FDMXEntityFixturePatchRef>& Item)
			{			
				const UDMXEntityFixturePatch* FixturePatch = (*MaxFixturePatchPtr).IsValid() ? (*MaxFixturePatchPtr)->GetFixturePatch() : nullptr;
				return Item->GetFixturePatch() == FixturePatch;
			});

		if (Items.IsValidIndex(IndexOfPatch + 1))
		{
			const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> NewSelection{ Items[IndexOfPatch + 1] };
			SelectItems(NewSelection);
		}
		else if (Items.IsValidIndex(IndexOfPatch - 1))
		{
			const TArray<TSharedPtr<FDMXEntityFixturePatchRef>> NewSelection{ Items[IndexOfPatch - 1] };
			SelectItems(NewSelection);
		}
	}
}

void SDMXPixelMappingFixturePatchList::RefreshList() 
{
	SDMXReadOnlyFixturePatchList::RefreshList();

	// Always make a selection if possible
	if (GetSelectedFixturePatchRefs().IsEmpty() && !ListItems.IsEmpty())
	{
		SelectItems(TArray<TSharedPtr<FDMXEntityFixturePatchRef>>({ ListItems[0] }));
	}
}
