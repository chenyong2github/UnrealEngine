// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

class UNREALED_API SWorldPartitionViewportWidget : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SWorldPartitionViewportWidget) {}
	SLATE_ARGUMENT(bool, Clickable)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	~SWorldPartitionViewportWidget();

	static EVisibility GetVisibility(UWorld* InWorld);
private:

	bool bClickable;
};