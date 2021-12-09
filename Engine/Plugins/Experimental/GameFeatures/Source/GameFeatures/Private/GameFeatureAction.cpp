// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameFeatureAction.h"
#include "GameFeaturesSubsystem.h"

void UGameFeatureAction::OnGameFeatureActivating(FGameFeatureActivatingContext& Context)
{
	// Call older style if not overridden
	OnGameFeatureActivating();
}