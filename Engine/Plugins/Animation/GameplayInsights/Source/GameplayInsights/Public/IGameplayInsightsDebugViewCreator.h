// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IGameplayInsightsDebugView.h"

namespace TraceServices
{
	class IAnalysisSession;
}

// this class handles creating debug view widgets for the Rewind Debugger.
// systems can register a debug view creator function with a UObject type name, and when an object of that type is selected, that widget will be created and shown by the debugger.
class IGameplayInsightsDebugViewCreator
{
	public:
		DECLARE_DELEGATE_RetVal_ThreeParams(TSharedRef<IGameplayInsightsDebugView>, FCreateDebugView, uint64 /*ObjectId*/, double /*Time*/, const TraceServices::IAnalysisSession& InAnalysisSession);

		virtual void RegisterDebugViewCreator(FName TypeName, FCreateDebugView Creator) = 0;
		virtual void CreateDebugViews(uint64 ObjectId, double CurrentTime, const TraceServices::IAnalysisSession& InAnalysisSession, TArray<TSharedPtr<IGameplayInsightsDebugView>>& OutDebugViews) = 0;
};