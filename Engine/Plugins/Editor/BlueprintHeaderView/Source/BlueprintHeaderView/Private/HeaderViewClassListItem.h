// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "SBlueprintHeaderView.h"

class FMenuBuilder;

/** A header view list item that displays the class declaration */
struct FHeaderViewClassListItem : public FHeaderViewListItem
{
public:
	/** Creates a list item for the Header view representing a class declaration for the given blueprint */
	static FHeaderViewListItemPtr Create(TWeakObjectPtr<UBlueprint> InBlueprint);

	//~ FHeaderViewListItem Interface
	virtual void ExtendContextMenu(FMenuBuilder& InMenuBuilder, TWeakObjectPtr<UBlueprint> InBlueprint) override;
	//~ End FHeaderViewListItem Interface
protected:
	FString GetConditionalUClassSpecifiers(const UBlueprint* Blueprint) const;

	FHeaderViewClassListItem(TWeakObjectPtr<UBlueprint> InBlueprint);

	void OnRenameTextComitted(const FText& CommittedText, ETextCommit::Type TextCommitType, TWeakObjectPtr<UBlueprint> InBlueprint);

protected:
	/** Whether this class name is valid C++ (no spaces, special chars, etc) */
	bool bIsValidName = true;
};