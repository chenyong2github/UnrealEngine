// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IGameplayInsightsDebugViewCreator.h"
#include "Containers/Map.h"

class FGameplayInsightsDebugViewCreator : public IGameplayInsightsDebugViewCreator
{
	public:
		virtual ~FGameplayInsightsDebugViewCreator() {};

		virtual void RegisterDebugViewCreator(FName TypeName, TSharedPtr<ICreateGameplayInsightsDebugView> Creator) override;
		virtual void CreateDebugViews(uint64 ObjectId, double CurrentTime,  const TraceServices::IAnalysisSession& InAnalysisSession, TArray<TSharedPtr<IGameplayInsightsDebugView>>& OutDebugViews)const override;
		virtual void EnumerateCreators(TFunctionRef<void(const TSharedPtr<ICreateGameplayInsightsDebugView>&)> Callback) const;
		virtual TSharedPtr<ICreateGameplayInsightsDebugView> GetCreator(FName CreatorName) const;
	private:
		struct FViewCreatorPair
		{
			FName TypeName;
			TSharedPtr<ICreateGameplayInsightsDebugView> Creator;
		};
		TArray<FViewCreatorPair> ViewCreators;
};