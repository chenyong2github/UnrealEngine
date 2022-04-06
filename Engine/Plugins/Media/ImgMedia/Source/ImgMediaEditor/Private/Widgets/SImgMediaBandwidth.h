// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

class FImgMediaPlayer;
class STextBlock;
class SVerticalBox;

/**
 * SImgMediaCache manages caching for image sequences.
 */
class SImgMediaBandwidth : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SImgMediaBandwidth){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	//~ SWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:

	/**
	 * Update our list of players and the widgets accordingly.
	 */
	void RefreshPlayersContainer();

	/** Info for a single player. */
	struct FPlayerInfo
	{
		TWeakPtr<FImgMediaPlayer> Player;
		TSharedPtr<STextBlock> UrlTextBlock;
		TSharedPtr<STextBlock> BandwidthTextBlock;
	};

	/** Array of all players. */
	TArray<FPlayerInfo> PlayerInfos;
	/** Widget to hold all widgets for all the players. */
	TSharedPtr<SVerticalBox> PlayersContainer;

};
