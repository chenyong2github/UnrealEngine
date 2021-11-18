// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FUnrealInsightsLauncher : public TSharedFromThis<FUnrealInsightsLauncher>
{
public:
	FUnrealInsightsLauncher();
	~FUnrealInsightsLauncher();

	void RegisterMenus();

private:
	void OpenUnrealInsights();

private:
	/** The name of the Unreal Insights log listing. */
	FName LogListingName;
};
