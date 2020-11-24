// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

/* Display in the favorite filter list.
 * TODO: Can be drag & dropped to 1. create new AND chain or 2. add a condition to an AND chain.
 */
class SFavoriteFilter : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SFavoriteFilter)
	{}
		SLATE_ATTRIBUTE(FText, FilterName)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

};
