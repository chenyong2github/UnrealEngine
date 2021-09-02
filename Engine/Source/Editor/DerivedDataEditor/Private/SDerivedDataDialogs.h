// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"

class SDerivedDataRemoteStoreDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDerivedDataRemoteStoreDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	TSharedRef<SWidget> GetGridPanel();

	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);

	SVerticalBox::FSlot* GridSlot = nullptr;
};

class SDerivedDataCacheStatisticsDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDerivedDataCacheStatisticsDialog) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:

	TSharedRef<SWidget> GetGridPanel();
	
	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);

	SVerticalBox::FSlot* GridSlot = nullptr;
};

class SDerivedDataResourceUsageDialog : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SDerivedDataResourceUsageDialog) {}
	SLATE_END_ARGS()

		void Construct(const FArguments& InArgs);

private:

	TSharedRef<SWidget> GetGridPanel();
	
	EActiveTimerReturnType UpdateGridPanels(double InCurrentTime, float InDeltaTime);

	SVerticalBox::FSlot* GridSlot = nullptr;
};
