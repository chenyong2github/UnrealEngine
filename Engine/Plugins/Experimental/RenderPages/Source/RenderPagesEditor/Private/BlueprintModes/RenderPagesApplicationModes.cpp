// Copyright Epic Games, Inc. All Rights Reserved.

#include "BlueprintModes/RenderPagesApplicationModes.h"


// Mode constants
const FName UE::RenderPages::Private::FRenderPagesApplicationModes::ListingMode("ListingName");
const FName UE::RenderPages::Private::FRenderPagesApplicationModes::LogicMode("LogicName");


FText UE::RenderPages::Private::FRenderPagesApplicationModes::GetLocalizedMode(const FName InMode)
{
	static TMap<FName, FText> LocModes;
	if (LocModes.Num() == 0)
	{
		LocModes.Add(ListingMode, NSLOCTEXT("RenderPagesBlueprintModes", "ListingMode", "Listing"));
		LocModes.Add(LogicMode, NSLOCTEXT("RenderPagesBlueprintModes", "LogicMode", "Logic"));
	}
	
	check(InMode != NAME_None);
	const FText* OutDesc = LocModes.Find(InMode);
	check(OutDesc);
	
	return *OutDesc;
}
