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

private:
	FText GetSimpleCacheInformation() const;
	TSharedRef<SWidget> GetComplexDataGrid();
	EActiveTimerReturnType UpdateComplexDataGrid(double InCurrentTime, float InDeltaTime);

	SVerticalBox::FSlot* GridSlot = nullptr;
};