// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

class UMoviePipelineConfigBase;
class SMoviePipelinePanel;

class SMoviePipelineTabContent : public SCompoundWidget
{
public:

	SLATE_BEGIN_ARGS(SMoviePipelineTabContent){}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Set this tab up for a specific Pipeline preset. Passing nullptr will initialize a default pipeline. */
	void SetupForPipeline(UMoviePipelineConfigBase* BasePreset);

private:
	EActiveTimerReturnType OnActiveTimer(double InCurrentTime, float InDeltaTime);

private:
	TWeakPtr<SMoviePipelinePanel> WeakPanel;
};
