// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"

class FDerivedDataCacheStatsNode;

/**  */
class UNREALED_API SDDCInformation : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDDCInformation) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static double GetDDCTimeSeconds(bool bGet, bool bLocal);
	static double GetDDCSizeBytes(bool bGet, bool bLocal);
	static bool	GetDDCHasLocalBackend();
	static bool	GetDDCHasRemoteBackend();
	static bool	GetShowDetailedInformation();

private:

	TSharedRef<SWidget> GetAssetGrid();
	TSharedRef<SWidget> GetSummaryGrid();
	TSharedRef<SWidget> GetCacheGrid();

	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);

	SVerticalBox::FSlot* SummaryGridSlot = nullptr;
	SVerticalBox::FSlot* AssetGridSlot = nullptr;
	SVerticalBox::FSlot* CacheGridSlot = nullptr;


};