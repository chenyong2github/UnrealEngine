// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PropertySelector/PropertySelectorFilter.h"
#include "PropertyHasNameFilter.generated.h"

UENUM()
namespace ENameMatchingRule
{
	enum Type
	{
		/* The property name must match the given name exactly. */
		MatchesExactly,
		/* The name must match the given name but ignores the case */
		MatchesIgnoreCase,
		/* The name must contains the following substring (case sensitive) */
		ContainsExactly,
		/* The name must contains the following substring (case insensitive) */
		ContainsIgnoreCase
	};
}

/**
 * Allows a property when is has a certain name
 * Use case: You only want to allow properties named "MyPropertyName"
 */
UCLASS(meta = (CommonSnapshotFilter))
class LEVELSNAPSHOTFILTERS_API UPropertyHasNameFilter : public UPropertySelectorFilter
{
	GENERATED_BODY()
public:

	//~ Begin ULevelSnapshotFilter Interface
	virtual EFilterResult::Type IsPropertyValid(const FIsPropertyValidParams& Params) const override;
	//~ End ULevelSnapshotFilter Interface

private:

	/* How to compare the property name to AllowedNames */
	UPROPERTY(EditAnywhere, Category = "Config")
	TEnumAsByte<ENameMatchingRule::Type> NameMatchingRule = ENameMatchingRule::ContainsIgnoreCase;

	/* The names to match the property name against. */
	UPROPERTY(EditAnywhere, Category = "Config")
	TSet<FString> AllowedNames;
	
};
