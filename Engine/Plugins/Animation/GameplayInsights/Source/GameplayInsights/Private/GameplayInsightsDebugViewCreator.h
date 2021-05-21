// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IGameplayInsightsDebugViewCreator.h"
#include "Containers/Map.h"

class FGameplayInsightsDebugViewCreator : public IGameplayInsightsDebugViewCreator
{
	public:
		virtual ~FGameplayInsightsDebugViewCreator() {};

		virtual void RegisterDebugViewCreator(FName TypeName, FCreateDebugView Creator) override;
		virtual void CreateDebugViews(uint64 ObjectId, double CurrentTime,  const TraceServices::IAnalysisSession& InAnalysisSession, TArray<TSharedPtr<IGameplayInsightsDebugView>>& OutDebugViews) override;

	private:
		TMultiMap<FName, FCreateDebugView> ViewCreators;
};