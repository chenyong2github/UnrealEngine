// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "FavoriteFilterContainer.h"

class SWrapBox;

class SFavoriteFilterList : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SFavoriteFilterList)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TWeakObjectPtr<UFavoriteFilterContainer>& InModel);

private:

	TSharedPtr<SWrapBox> FilterList;
	
};
