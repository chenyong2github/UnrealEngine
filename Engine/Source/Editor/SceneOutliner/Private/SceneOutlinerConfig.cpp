// Copyright Epic Games, Inc. All Rights Reserved.

#include "SceneOutlinerConfig.h"

TObjectPtr<UOutlinerConfig> UOutlinerConfig::Instance = nullptr;

void UOutlinerConfig::Initialize()
{
	Instance = NewObject<UOutlinerConfig>(); 
	Instance->AddToRoot();
}