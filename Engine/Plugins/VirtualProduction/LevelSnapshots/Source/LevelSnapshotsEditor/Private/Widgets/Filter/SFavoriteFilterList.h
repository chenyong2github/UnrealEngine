// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

#include "FavoriteFilterContainer.h"

class FLevelSnapshotsEditorFilters;
class UFavoriteFilterContainer;
class ULevelSnapshotsEditorData;
class SComboButton;
class SWrapBox;

class SFavoriteFilterList : public SCompoundWidget
{
public:

	~SFavoriteFilterList();
	
	SLATE_BEGIN_ARGS(SFavoriteFilterList)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UFavoriteFilterContainer* InModel, TWeakObjectPtr<ULevelSnapshotsEditorData> InEditorData);

private:

	TSharedPtr<SWrapBox> FilterList;
	TSharedPtr<SComboButton> ComboButton;

	TWeakObjectPtr<UFavoriteFilterContainer> FavoriteModel;
	FDelegateHandle ChangedFavoritesDelegateHandle;
};
