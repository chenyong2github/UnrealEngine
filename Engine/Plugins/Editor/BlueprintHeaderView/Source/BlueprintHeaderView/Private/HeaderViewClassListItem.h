// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SBlueprintHeaderView.h"

/** A header view list item that displays the class declaration */
struct FHeaderViewClassListItem : public FHeaderViewListItem
{
public:
	/** Creates a list item for the Header view representing a class declaration for the given blueprint */
	static FHeaderViewListItemPtr Create(TWeakObjectPtr<UBlueprint> InBlueprint);

protected:
	FString GetConditionalUClassSpecifiers(const UBlueprint* Blueprint) const;

	FHeaderViewClassListItem(TWeakObjectPtr<UBlueprint> InBlueprint);

};