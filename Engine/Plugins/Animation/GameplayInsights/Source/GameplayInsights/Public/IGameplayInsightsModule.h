// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IGameplayInsightsDebugViewCreator.h"


namespace GameplayInsightsTabs
{
	extern GAMEPLAYINSIGHTS_API const FName DocumentTab;
};


namespace TraceServices
{
	class IAnalysisSession;
}

class IGameplayInsightsDebugViewCreator;

class IGameplayInsightsModule
	: public IModuleInterface
{
public:
	
	virtual IGameplayInsightsDebugViewCreator* GetDebugViewCreator() = 0;
};
