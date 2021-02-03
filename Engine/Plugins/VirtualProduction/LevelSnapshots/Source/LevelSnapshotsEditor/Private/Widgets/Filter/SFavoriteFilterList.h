// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "FavoriteFilterContainer.h"

class FLevelSnapshotsEditorFilters;
class SWrapBox;
class UFavoriteFilterContainer;

class SFavoriteFilterList : public SCompoundWidget
{
public:

	~SFavoriteFilterList();
	
	SLATE_BEGIN_ARGS(SFavoriteFilterList)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UFavoriteFilterContainer* InModel, const TSharedRef<FLevelSnapshotsEditorFilters>& InFilters);

private:

	TSharedPtr<SWrapBox> FilterList;

	TWeakObjectPtr<UFavoriteFilterContainer> FavoriteModel;
	FDelegateHandle ChangedFavoritesDelegateHandle;
};
